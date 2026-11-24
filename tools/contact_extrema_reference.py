#!/usr/bin/env python3
"""Bounded, independent extrema references for two synthetic supported impacts.

Public API
==========
``generate_reference(fixture, level, *, limits=None, raw_path=None,
checkpoint_path=None)`` returns a
JSON-safe dictionary. ``fixture`` is one of SUPPORTED_FIXTURES (or that exact
independent Fixture); ``level`` is R0, R1, or R2. Limits may only tighten the
declared defaults. A completed run still requires ``compare_refinement`` and
always has ``coverage=uncertified_independent_search``. No acceptance or PHY-002
completion is implied. The existing four-fixture tools are not changed.

With raw_path, mesh, normalized cubic coefficients, reconstruction residuals,
and the shared endpoint/root registry are retained in an exclusive NPZ file;
the result contains its byte hash and per-array hashes. Without raw_path those
arrays are retained as JSON lists in result['raw_arrays']. The JSON summary
otherwise contains every root candidate, branch transition, failure and peak,
but references mesh endpoints by array index instead of duplicating states.
``evaluate_reference(reference, time_s, *, side='right')`` evaluates those
retained cubics, including branch and physical/dense derivative diagnostics.
It accepts the returned dictionary (the raw NPZ path must remain accessible).
``compare_refinement([r0, r1, r2])`` reports both unchanged value differences,
peak-classification/cluster checks, and supplemental timing separately.

``reconstruct_cubic``, ``evaluate_polynomial``, ``retained_bisection`` and
``isolate_polynomial_roots`` are independently testable. Coefficients are in
ascending powers of u=(t-t0)/(t1-t0). Reconstruction uses only the public dense
evaluator; no private SciPy coefficient attributes are read. Polynomial roots
and a separate quarter-point sign scan are both retained. A floating root
without a sign bracket is explicitly unresolved, never a coverage certificate.

The CLI is also a worker entry point for an owning parent's subprocess timeout:
  python3 tools/contact_extrema_reference.py --fixture supported_impact \
      --level R0 --output /tmp/reference-R0.json --raw /tmp/reference-R0.npz
Cooperative limits cannot interrupt a stalled import/native solver call; the
parent must enforce its own wall deadline. Outputs are exclusive. The optional
NumPy/SciPy dependencies already belong to the independent offline auditor;
there are no new runtime dependencies or imported candidate trajectories.
"""

from __future__ import annotations

import argparse
from dataclasses import asdict, dataclass
import hashlib
import json
import math
import os
from pathlib import Path
import platform
import sys
import tempfile
import time
from types import MappingProxyType
from typing import Any, Callable, Mapping, Sequence

from validate_contact_reference import Fixture, fixtures, initial_vector, observe, rhs


SUPPORTED_FIXTURES = ("supported_impact", "supported_impact_scaled")
LEVEL_NAMES = ("R0", "R1", "R2")
COVERAGE = "uncertified_independent_search"
SCHEMA_VERSION = 1
BASE_ATOL = (1e-16, 1e-16, 1e-13, 1e-13, 1e-16, 1e-13,
             1e-16, 1e-16, 1e-16, 1e-16, 1e-16, 1e-16, 1e-16)
STATE_COORDINATES = ("q0_m", "q1_m", "v0_m_per_s", "v1_m_per_s",
                     "hammer_position_m", "hammer_velocity_m_per_s",
                     "modal_loss_j", "hammer_loss_j", "damper_loss_j",
                     "actuator_work_j", "launch_work_j", "hammer_impulse_ns",
                     "damper_impulse_ns")
PEAK_KEYS = ("hammer_compression", "hammer_force", "damper_compression", "damper_force")
ROOT_WIDTH_S = 1e-12
ROOT_ITERATIONS = 80
# These diagnose numerical resolution, not an acceptance tolerance or zero test.
FLAT_GAP_M = 1e-18
FLAT_SPEED_M_PER_S = 1e-14
ROOT_RESIDUAL_ULPS = 512
RECONSTRUCTION_ULPS = 512
CANDIDATE_ORIGINS = MappingProxyType({
    "mesh_endpoint": 1, "dense_endpoint": 2, "branch_boundary": 4,
    "physical_rhs_derivative": 8, "dense_derivative": 16,
    "plateau_endpoint": 32,
})


@dataclass(frozen=True)
class ReferenceLevel:
    maximum_step_s: float
    relative_tolerance: float
    absolute_vector_multiplier: float

    def settings(self) -> dict[str, Any]:
        return {**asdict(self), "absolute_tolerance_vector":
                [x * self.absolute_vector_multiplier for x in BASE_ATOL]}


LEVELS = MappingProxyType({
    "R0": ReferenceLevel(5e-6, 1e-10, 10.0),
    "R1": ReferenceLevel(2.5e-6, 1e-12, 1.0),
    "R2": ReferenceLevel(1.25e-6, 1e-13, 0.1),
})


@dataclass(frozen=True)
class ReferenceLimits:
    accepted_steps: int = 200_000
    rhs_evaluations: int = 5_000_000
    search_evaluations: int = 1_000_000
    candidates: int = 100_000
    wall_seconds: float = 300.0


DEFAULT_LIMITS = ReferenceLimits()


class ReferenceFailure(RuntimeError):
    """An explicit bounded numerical failure; partial evidence is retained."""


class BudgetExceeded(ReferenceFailure):
    pass


def _limits(value: Mapping[str, Any] | ReferenceLimits | None) -> ReferenceLimits:
    if value is None:
        return DEFAULT_LIMITS
    if not isinstance(value, (ReferenceLimits, Mapping)):
        raise ValueError("Reference limits must be a mapping or ReferenceLimits")
    fields = asdict(value) if isinstance(value, ReferenceLimits) else dict(value)
    unknown = set(fields) - set(asdict(DEFAULT_LIMITS))
    if unknown:
        raise ValueError("Unknown reference limits: " + ", ".join(sorted(unknown)))
    result = ReferenceLimits(**fields)
    for key, item in asdict(result).items():
        ceiling = getattr(DEFAULT_LIMITS, key)
        if key == "wall_seconds":
            valid = (type(item) in (int, float) and math.isfinite(item)
                     and 0 < item <= ceiling)
        else:
            valid = type(item) is int and 0 <= item <= ceiling
        if not valid:
            raise ValueError(f"{key} must be finite, nonnegative, and at most {ceiling}"
                             + (" (wall time must be positive)" if key == "wall_seconds" else ""))
    return result


class WorkBudget:
    """Counters charge before work, including RHS calls made for Jacobians.

    A search evaluation is one dense state, scalar polynomial, candidate state,
    or branch-midpoint evaluation. Algebra/degree<=3 eigensolves check time but
    are not misreported as function evaluations. Every accepted solver step is
    checked before calling step(), then charged only after acceptance.
    """

    def __init__(self, limits: Mapping[str, Any] | ReferenceLimits | None = None):
        self.limits = _limits(limits)
        self.started = time.monotonic()
        self.counts = {name: 0 for name in ("accepted_steps", "rhs_evaluations",
                                           "search_evaluations", "candidates")}

    def check(self) -> None:
        if time.monotonic() - self.started >= self.limits.wall_seconds:
            raise BudgetExceeded("wall_seconds budget exhausted")

    def ensure(self, key: str, amount: int = 1) -> None:
        self.check()
        if self.counts[key] + amount > getattr(self.limits, key):
            raise BudgetExceeded(key + " budget exhausted")

    def charge(self, key: str, amount: int = 1) -> None:
        self.ensure(key, amount)
        self.counts[key] += amount

    def report(self) -> dict[str, Any]:
        return {**self.counts, "elapsed_s": time.monotonic() - self.started}


def _canonical(value: Any) -> bytes:
    return json.dumps(value, sort_keys=True, separators=(",", ":"),
                      allow_nan=False).encode("utf-8")


def _sha(value: bytes) -> str:
    return hashlib.sha256(value).hexdigest()


def _numpy() -> Any:
    try:
        import numpy as np
    except ImportError as exc:
        raise ReferenceFailure("Optional reference backend NumPy is unavailable") from exc
    return np


def _fixture(value: str | Fixture) -> Fixture:
    name = value.name if isinstance(value, Fixture) else value
    if name not in SUPPORTED_FIXTURES:
        raise ValueError("Unsupported extrema reference fixture: " + str(name))
    frozen = fixtures()[name]
    if isinstance(value, Fixture) and value != frozen:
        raise ValueError("Only the exact independent frozen fixture is supported")
    if (frozen.kh != 20000.0 or frozen.ch != 0.0 or frozen.kd != 0.0 or frozen.cd != 0.0
            or frozen.relaunch or frozen.duration != 0.020 or frozen.face != "constant"
            or frozen.face_offset != 0.0 or frozen.face_velocity != 0.0):
        raise ValueError("Independent fixture no longer satisfies this narrow reference contract")
    return frozen


def evaluate_polynomial(coefficients: Any, u: Any) -> Any:
    """Horner evaluation; the last coefficient axis is ascending powers of u."""
    np = _numpy()
    c = np.asarray(coefficients, dtype=float)
    if c.ndim < 1 or not c.shape[-1] or not np.all(np.isfinite(c)):
        raise ValueError("Expected finite polynomial coefficients")
    result = np.zeros(c.shape[:-1], dtype=float)
    for coefficient in np.moveaxis(c, -1, 0)[::-1]:
        result = result * u + coefficient
    return result


def polynomial_derivative(coefficients: Any, *, time_scale_s: float = 1.0) -> Any:
    """Differentiate normalized coefficients with the physical 1/h factor."""
    np = _numpy()
    c = np.asarray(coefficients, dtype=float)
    if (c.ndim < 1 or not c.shape[-1] or not np.all(np.isfinite(c))
            or not math.isfinite(time_scale_s) or time_scale_s <= 0):
        raise ValueError("Invalid polynomial derivative input")
    if c.shape[-1] == 1:
        return np.zeros((*c.shape[:-1], 1))
    return c[..., 1:] * np.arange(1, c.shape[-1]) / time_scale_s


@dataclass
class DenseCubic:
    t0_s: float
    t1_s: float
    coefficients: Any
    residual_by_component: Any
    validation_limit_by_component: Any

    def state(self, time_s: float, *, budget: WorkBudget | None = None) -> Any:
        if not self.t0_s <= time_s <= self.t1_s:
            raise ValueError("Dense evaluation would extrapolate outside its segment")
        if budget:
            budget.charge("search_evaluations")
        return evaluate_polynomial(self.coefficients,
                                   (time_s - self.t0_s) / (self.t1_s - self.t0_s))


def reconstruct_cubic(evaluator: Callable[[float], Any], t0_s: float, t1_s: float,
                      *, budget: WorkBudget | None = None) -> DenseCubic:
    """Reconstruct from four public values; check 1/5, 1/2, 4/5 independently.

    A failed reconstruction raises ReferenceFailure instead of labeling a
    noncubic public evaluator cubic. Subtracting the initial value before the
    coefficient transform reduces cancellation for nearly constant states.
    """
    np = _numpy()
    if not (math.isfinite(t0_s) and math.isfinite(t1_s) and t0_s < t1_s):
        raise ValueError("Dense reconstruction needs a positive finite interval")
    h = t1_s - t0_s

    def sample(u: float) -> tuple[float, Any]:
        if budget:
            budget.charge("search_evaluations")
        t = t0_s + h*u
        value = np.asarray(evaluator(t), dtype=float)
        if value.ndim != 1 or not np.all(np.isfinite(value)):
            raise ReferenceFailure("Nonfinite or nonvector public dense evaluation")
        return (t-t0_s)/h, value

    sampled = [sample(u) for u in (0.0, 1 / 3, 2 / 3, 1.0)]
    nodes = np.array([u for u, _ in sampled])
    if len(set(nodes)) != 4:
        raise ReferenceFailure("Dense reconstruction sample times are not representable")
    samples = np.stack([value for _, value in sampled])
    # Use the actual representable abscissae. At late, tiny steps, assuming
    # t0+h/3 maps to exactly u=1/3 creates a false noncubic residual.
    vandermonde = nodes[1:, None]**np.arange(1, 4)
    coefficients = np.empty((samples.shape[1], 4))
    coefficients[:, 0] = samples[0]
    coefficients[:, 1:] = np.linalg.solve(vandermonde, samples[1:]-samples[0]).T
    residual = np.zeros_like(samples[0])
    scale = np.max(np.abs(samples), axis=0)
    for u in (0.2, 0.5, 0.8):
        actual_u, actual = sample(u)
        scale = np.maximum(scale, np.abs(actual))
        if budget:
            budget.charge("search_evaluations")
        residual = np.maximum(residual, np.abs(evaluate_polynomial(coefficients, actual_u) - actual))
    limit = RECONSTRUCTION_ULPS * np.finfo(float).eps * np.maximum(scale, 1e-30)
    if not np.all(np.isfinite(coefficients)) or np.any(residual > limit):
        raise ReferenceFailure("Public dense output failed independent cubic reconstruction checks")
    return DenseCubic(t0_s, t1_s, coefficients, residual, limit)


def retained_bisection(function: Callable[[float], float], lo_s: float, hi_s: float,
                       *, width_s: float = ROOT_WIDTH_S, iterations: int = ROOT_ITERATIONS,
                       budget: WorkBudget | None = None) -> dict[str, Any]:
    """Retain a numerical sign bracket; fail on budget or unrepresentable midpoints."""
    if (not all(math.isfinite(v) for v in (lo_s, hi_s, width_s)) or lo_s > hi_s
            or width_s <= 0 or type(iterations) is not int or not 0 <= iterations <= ROOT_ITERATIONS):
        raise ValueError("Invalid retained-bisection controls")

    def evaluate(t: float) -> float:
        if budget:
            budget.charge("search_evaluations")
        y = float(function(t))
        if not math.isfinite(y):
            raise ReferenceFailure("Nonfinite scalar root evaluation")
        return y

    lo, hi = float(lo_s), float(hi_s)
    flo, fhi = evaluate(lo), evaluate(hi)
    initial_signs = [int(flo > 0) - int(flo < 0), int(fhi > 0) - int(fhi < 0)]
    if flo == 0.0:
        hi, fhi = lo, flo
    elif fhi == 0.0:
        lo, flo = hi, fhi
    elif not (flo < 0 < fhi or fhi < 0 < flo):
        raise ReferenceFailure("Root endpoints do not strictly bracket a sign change")
    used = 0
    while hi - lo > width_s:
        if used >= iterations:
            raise ReferenceFailure("root iteration budget exhausted")
        midpoint = lo + (hi - lo) / 2
        if not lo < midpoint < hi:
            raise ReferenceFailure("root midpoint is not representable")
        fm = evaluate(midpoint)
        used += 1
        if fm == 0.0:
            lo = hi = midpoint
            flo = fhi = fm
        elif (flo < 0 < fm) or (fm < 0 < flo):
            hi, fhi = midpoint, fm
        else:
            lo, flo = midpoint, fm
    return {"time_s": lo + (hi - lo) / 2, "bracket_lo_s": lo,
            "bracket_hi_s": hi, "bracket_residuals": [flo, fhi],
            "initial_signs": initial_signs, "iterations": used,
            "isolation": "endpoint_zero" if 0 in initial_signs else "sign_bracket",
            "status": "resolved_numerical"}


def isolate_polynomial_roots(coefficients: Sequence[float], t0_s: float = 0.0,
                             t1_s: float = 1.0, *, interval_u: tuple[float, float] = (0.0, 1.0),
                             flat_threshold: float = 0.0,
                             budget: WorkBudget | None = None) -> dict[str, Any]:
    """Enumerate degree<=3 roots plus an independent quarter-point sign scan.

    Neither the eigensolve nor retained float brackets certify the ODE. Even
    roots, near-real complex roots, residual failures and tiny nonzero functions
    are recorded as unresolved. Distinct nearby roots are never merged by
    decimal rounding or a fixed time-distance threshold. Matching the scan to
    a polynomial root uses its isolating cell, not rounded root times.
    """
    np = _numpy()
    c = np.asarray(coefficients, dtype=float)
    if (c.ndim != 1 or not 1 <= len(c) <= 4 or not np.all(np.isfinite(c))
            or not all(math.isfinite(v) for v in (t0_s, t1_s, *interval_u, flat_threshold))
            or not t0_s < t1_s or not 0 <= interval_u[0] < interval_u[1] <= 1
            or flat_threshold < 0):
        raise ValueError("Expected finite degree<=3 polynomial and ordered normalized interval")
    if budget:
        budget.check()
    while len(c) > 1 and c[-1] == 0.0:
        c = c[:-1]
    h = t1_s - t0_s
    left_u, right_u = interval_u
    left_t, right_t = t0_s + h*left_u, t0_s + h*right_u
    unresolved: list[dict[str, Any]] = []
    result: dict[str, Any] = {"roots": [], "unresolved": unresolved,
                              "plateau": False, "degree": len(c)-1,
                              "quarter_scan_sign_changes": 0,
                              "polynomial_roots_without_scan_sign_change": 0}
    if np.all(c == 0.0):
        result.update(plateau=True, plateau_interval_s=[left_t, right_t],
                      plateau_basis="validated_polynomial_identity_only")
        return result
    if float(np.sum(np.abs(c))) <= flat_threshold:
        unresolved.append({"reason": "numerically_flat_unresolved", "interval_s": [left_t, right_t],
                           "coefficient_l1": float(np.sum(np.abs(c))), "threshold": flat_threshold})

    def at_u(u: float) -> float:
        if budget:
            budget.charge("search_evaluations")
        value = float(evaluate_polynomial(c, u))
        if not math.isfinite(value):
            raise ReferenceFailure("Nonfinite polynomial evaluation")
        return value

    def at_t(t: float) -> float:
        # retained_bisection accounts for this evaluation itself.
        return float(evaluate_polynomial(c, (t-t0_s)/h))

    eps = np.finfo(float).eps
    derivative = polynomial_derivative(c)
    numerical_roots = np.polynomial.polynomial.polyroots(c) if len(c) > 1 else []
    real_roots: list[tuple[float, float]] = []
    for z in numerical_roots:
        re, im = float(np.real(z)), abs(float(np.imag(z)))
        if not math.isfinite(re) or not math.isfinite(im):
            raise ReferenceFailure("Polynomial root solver returned nonfinite roots")
        if left_u - 64*eps <= re <= right_u + 64*eps:
            if im <= 32*eps*max(1.0, abs(re)):
                real_roots.append((min(right_u, max(left_u, re)), im))
            elif im <= math.sqrt(eps)*max(1.0, abs(re)):
                real_roots.append((min(right_u, max(left_u, re)), im))
                unresolved.append({"reason": "near_real_root_cluster", "interval_s": [left_t, right_t],
                                   "root_u": re, "imaginary_part": im})
    real_roots.sort()
    # Exact endpoint zeros are explicit. Only exactly equal locations are
    # coalesced; an interior root arbitrarily near an endpoint remains visible.
    endpoint_values = {left_u: at_u(left_u), right_u: at_u(right_u)}
    endpoint_roots = [u for u, value in endpoint_values.items() if value == 0.0]
    for u in endpoint_roots:
        t = t0_s + h*u
        result["roots"].append({"u": u, "time_s": t, "bracket_lo_s": t, "bracket_hi_s": t,
                                "bracket_residuals": [0.0, 0.0], "iterations": 0,
                                "isolation": "endpoint_zero", "status": "resolved_numerical",
                                "methods": ["polynomial_endpoint", "quarter_sign_scan"],
                                "residual": 0.0, "crossing": "endpoint", "cell_u": [u, u]})
    interiors = [(u, im) for u, im in real_roots if u not in endpoint_roots]
    for index, (u, im) in enumerate(interiors):
        cell_lo = left_u if index == 0 else (interiors[index-1][0]+u)/2
        cell_hi = right_u if index+1 == len(interiors) else (u+interiors[index+1][0])/2
        residual = at_u(u)
        initial_residual = residual
        # Tiny but nonzero high-order interpolation coefficients can make the
        # companion eigensolve poorly scaled. Bounded Newton polishing restores
        # the small root before residual diagnostics; the independent sign
        # bracket below still supplies the retained location evidence.
        polished = 0
        for _ in range(8):
            if budget:
                budget.charge("search_evaluations")
            polish_slope = float(evaluate_polynomial(derivative, u))
            if polish_slope == 0.0 or not math.isfinite(polish_slope):
                break
            next_u = u-residual/polish_slope
            if not cell_lo <= next_u <= cell_hi or next_u == u:
                break
            next_residual = at_u(next_u)
            if abs(next_residual) > abs(residual):
                break
            u, residual = next_u, next_residual
            polished += 1
        if budget:
            budget.charge("search_evaluations")
        slope = float(evaluate_polynomial(derivative, u))
        scale = float(sum(abs(a)*abs(u)**j for j, a in enumerate(c)))
        residual_limit = ROOT_RESIDUAL_ULPS*eps*max(scale, 1e-300)
        root: dict[str, Any] = {"u": u, "time_s": t0_s+h*u,
                                "bracket_lo_s": None, "bracket_hi_s": None,
                                "isolation": "unbracketed_polynomial_candidate",
                                "status": "unresolved", "methods": ["polynomial_enumeration"],
                                "residual": residual, "residual_limit": residual_limit,
                                "initial_polynomial_residual": initial_residual,
                                "polishing_iterations": polished,
                                "slope_per_u": slope, "imaginary_part": im,
                                "polynomial_root_u": u,
                                "cell_u": [cell_lo, cell_hi], "crossing": "tangent_or_unresolved"}
        flo, fhi = at_u(cell_lo), at_u(cell_hi)
        if cell_lo < cell_hi and (flo < 0 < fhi or fhi < 0 < flo) and im == 0.0:
            root.update(retained_bisection(at_t, t0_s+h*cell_lo, t0_s+h*cell_hi, budget=budget))
            root["methods"].append("polynomial_cell_bisection")
            root["crossing"] = "negative_to_positive" if flo < fhi else "positive_to_negative"
            root["u"] = (root["time_s"]-t0_s)/h
            root["residual"] = at_u(root["u"])
            # A tiny bracket is an independent numerical locator, so its
            # midpoint residual need not satisfy the eigensolve residual test.
            root["polynomial_candidate_residual"] = residual
        else:
            unresolved.append({"reason": "unbracketed_root_or_tangency", "interval_s": [left_t, right_t],
                               "time_s": root["time_s"], "root_u": u,
                               "residual": residual, "slope_per_u": slope})
        if abs(residual) > residual_limit:
            root["polynomial_residual_status"] = "unresolved"
            unresolved.append({"reason": "polynomial_root_residual", "interval_s": [left_t, right_t],
                               "time_s": root["time_s"], "residual": residual,
                               "limit": residual_limit})
        else:
            root["polynomial_residual_status"] = "within_roundoff_diagnostic"
        if abs(slope) <= math.sqrt(eps)*max(float(np.sum(np.abs(derivative))), 1e-300):
            unresolved.append({"reason": "ill_conditioned_or_multiple_root", "interval_s": [left_t, right_t],
                               "time_s": root["time_s"], "slope_per_u": slope})
        result["roots"].append(root)

    scan_u = [left_u+(right_u-left_u)*j/4 for j in range(5)]
    scan_y = [endpoint_values[u] if u in endpoint_values else at_u(u) for u in scan_u]
    for a, b, fa, fb in zip(scan_u, scan_u[1:], scan_y, scan_y[1:]):
        if not (fa < 0 < fb or fb < 0 < fa):
            continue
        result["quarter_scan_sign_changes"] += 1
        scan = retained_bisection(at_t, t0_s+h*a, t0_s+h*b, budget=budget)
        u = (scan["time_s"]-t0_s)/h
        matches = [record for record in result["roots"]
                   if record["cell_u"][0] <= u <= record["cell_u"][1]]
        if len(matches) == 1:
            matches[0]["methods"].append("quarter_sign_scan")
            matches[0]["scan_bracket_s"] = [scan["bracket_lo_s"], scan["bracket_hi_s"]]
        else:
            result["roots"].append({**scan, "u": u, "methods": ["quarter_sign_scan"],
                                    "residual": at_u(u), "cell_u": [a, b],
                                    "crossing": "negative_to_positive" if fa < fb else "positive_to_negative"})
    for u, value in zip(scan_u[1:-1], scan_y[1:-1]):
        if value != 0.0:
            continue
        t = t0_s+h*u
        matches = [record for record in result["roots"] if record["time_s"] == t or
                   (record["bracket_lo_s"] is not None
                    and record["bracket_lo_s"] <= t <= record["bracket_hi_s"])]
        if len(matches) == 1:
            matches[0]["methods"].append("quarter_scan_zero")
        elif not matches:
            result["roots"].append({"u": u, "time_s": t, "bracket_lo_s": t, "bracket_hi_s": t,
                                    "bracket_residuals": [0.0, 0.0], "iterations": 0,
                                    "isolation": "sample_zero", "status": "unresolved",
                                    "methods": ["quarter_scan_zero"], "residual": 0.0,
                                    "cell_u": [u, u], "crossing": "tangent_or_unresolved"})
            unresolved.append({"reason": "scan_zero_without_isolating_bracket", "time_s": t,
                               "interval_s": [left_t, right_t]})
    result["polynomial_roots_without_scan_sign_change"] = sum(
        "polynomial_enumeration" in record["methods"] and "quarter_sign_scan" not in record["methods"]
        for record in result["roots"])
    result["roots"].sort(key=lambda record: record["time_s"])
    return result


def value_budget(key: str, value: float) -> float:
    if key not in PEAK_KEYS or not math.isfinite(value):
        raise ValueError("Unknown peak or nonfinite peak value")
    return (1e-8 + 1e-4*abs(value)) if key.endswith("compression") else (1e-4 + 2e-4*abs(value))


def _contact_coefficients(f: Fixture, segment: DenseCubic) -> tuple[Any, Any, Any]:
    c = segment.coefficients
    delta = c[4] - f.bh[0]*c[0] - f.bh[1]*c[1]
    delta = delta.copy()
    delta[0] -= f.clearance
    w = c[5] - f.bh[0]*c[2] - f.bh[1]*c[3]
    dense_derivative = polynomial_derivative(delta, time_scale_s=segment.t1_s-segment.t0_s)
    return delta, w, dense_derivative


class _CandidateRegistry:
    def __init__(self, fixture: Fixture, budget: WorkBudget):
        self.fixture, self.budget = fixture, budget
        self.records: list[dict[str, Any]] = []
        self.lookup: dict[tuple[float, bytes], int] = {}
        self.best = 0.0
        self.best_ids: list[int] = []

    def retain(self, t: float, y: Any, segment_id: int, origin: str,
               detail: dict[str, Any] | None = None) -> int:
        np = _numpy()
        state = np.asarray(y, dtype="<f8")
        if state.shape != (13,) or not np.all(np.isfinite(state)) or not math.isfinite(t):
            raise ReferenceFailure("Nonfinite candidate state or time")
        key = (float(t), state.tobytes())
        existing = self.lookup.get(key)
        if existing is not None:
            self.records[existing]["origin_bits"] |= CANDIDATE_ORIGINS[origin]
            if detail is not None:
                self.records[existing]["details"].append(detail)
            return existing
        self.budget.ensure("candidates")
        self.budget.charge("search_evaluations")
        gap = float(state[4] - self.fixture.bh[0]*state[0] - self.fixture.bh[1]*state[1]
                    - self.fixture.clearance)
        compression = max(0.0, gap)
        index = len(self.records)
        self.budget.charge("candidates")
        self.records.append({"time_s": float(t), "state": state.copy(), "segment_id": segment_id,
                             "origin_bits": CANDIDATE_ORIGINS[origin], "gap_m": gap,
                             "compression_m": compression, "details": [] if detail is None else [detail]})
        self.lookup[key] = index
        if compression > self.best:
            self.best, self.best_ids = compression, [index]
        elif compression == self.best:
            self.best_ids.append(index)
        return index


def _array_hashes(arrays: dict[str, Any]) -> dict[str, Any]:
    return {key: {"shape": list(value.shape), "dtype": value.dtype.str,
                  "sha256": _sha(value.tobytes(order="C"))}
            for key, value in sorted(arrays.items())}


def _raw_arrays(mesh_t: list[float], mesh_y: list[Any], segments: list[DenseCubic],
                registry: _CandidateRegistry) -> dict[str, Any]:
    np = _numpy()
    return {
        "mesh_t_s": np.asarray(mesh_t, dtype="<f8"),
        "mesh_y": np.asarray(mesh_y, dtype="<f8").reshape((-1, 13)),
        "dense_t0_s": np.asarray([s.t0_s for s in segments], dtype="<f8"),
        "dense_t1_s": np.asarray([s.t1_s for s in segments], dtype="<f8"),
        "dense_coefficients": np.asarray([s.coefficients for s in segments], dtype="<f8").reshape((-1, 13, 4)),
        "reconstruction_residual": np.asarray([s.residual_by_component for s in segments], dtype="<f8").reshape((-1, 13)),
        "reconstruction_limit": np.asarray([s.validation_limit_by_component for s in segments], dtype="<f8").reshape((-1, 13)),
        "candidate_time_s": np.asarray([r["time_s"] for r in registry.records], dtype="<f8"),
        "candidate_y": np.asarray([r["state"] for r in registry.records], dtype="<f8").reshape((-1, 13)),
        "candidate_segment_id": np.asarray([r["segment_id"] for r in registry.records], dtype="<i8"),
        "candidate_origin_bits": np.asarray([r["origin_bits"] for r in registry.records], dtype="<u8"),
    }


def _compact_candidate(record: dict[str, Any], candidate_id: int) -> dict[str, Any]:
    return {"candidate_id": candidate_id, "time_s": record["time_s"],
            "segment_id": record["segment_id"], "origin_bits": record["origin_bits"],
            "gap_m": record["gap_m"], "details": record["details"]}


def _peaks(f: Fixture, registry: _CandidateRegistry, completed: bool) -> dict[str, Any]:
    maxima = [_compact_candidate(registry.records[i], i) for i in registry.best_ids]
    times: list[list[float]] = []
    for maximum in maxima:
        brackets = [[d["bracket_lo_s"], d["bracket_hi_s"]] for d in maximum["details"]
                    if d.get("bracket_lo_s") is not None]
        for interval in brackets or [[maximum["time_s"], maximum["time_s"]]]:
            if interval not in times:
                times.append(interval)
    origin_bits = 0
    for maximum in maxima:
        origin_bits |= maximum["origin_bits"]
    classification = ("stationary" if origin_bits & (8 | 16) else
                      "branch_boundary" if origin_bits & 4 else "endpoint")
    common = {"kind": "isolated_candidate" if len(times) == 1 else "tied_candidates",
              "classification": classification, "maximizer_intervals_s": sorted(times),
              "maximizing_candidates": maxima, "coverage": COVERAGE,
              "value_status": "requires_refinement" if completed else "inconclusive_reference",
              "time_status": "numerical_candidates" if completed else "inconclusive_reference"}
    result = {"hammer_compression": {**common, "unit": "m", "value_m": registry.best},
              "hammer_force": {**common, "unit": "N", "value_n": f.kh*registry.best}}
    for key, unit in (("damper_compression", "m"), ("damper_force", "N")):
        result[key] = {"kind": "disabled", "classification": "disabled", "unit": unit,
                       "value_" + ("m" if unit == "m" else "n"): 0.0,
                       "maximizer_intervals_s": [[0.0, f.duration]],
                       "maximizing_candidates": [], "value_status": "analytic_disabled_identity",
                       "time_status": "disabled", "coverage": COVERAGE,
                       "identity": "kd=cd=0: diagnostic compression and force are identically zero"}
    return result


def generate_reference(fixture: str | Fixture, level: str = "R2", *,
                       limits: Mapping[str, Any] | ReferenceLimits | None = None,
                       raw_path: str | Path | None = None,
                       checkpoint_path: str | Path | None = None) -> dict[str, Any]:
    """Generate one independent full-duration Radau reference with live caps.

    Invalid API controls raise ValueError. Numerical/budget/backend failures
    return inconclusive_reference with partial arrays, roots and counters.
    An existing raw artifact is refused before integration; nothing is replaced.
    The 13-coordinate atol vector is unchanged for the signed scaled fixture.
    Optional checkpoints atomically retain a prefix every 2,048 steps or 10 s;
    a sibling .json starts before numerical imports, so a hard-killed worker
    retains its declared settings even if an import or native call stalls.
    """
    f = _fixture(fixture)
    if level not in LEVELS:
        raise ValueError("Unknown reference level: " + str(level))
    budget = WorkBudget(limits)
    settings = LEVELS[level].settings()
    path = Path(raw_path) if raw_path is not None else None
    if path is not None and (path.exists() or path.is_symlink()):
        raise FileExistsError("Refusing to overwrite raw reference artifact: " + str(path))
    checkpoint = Path(checkpoint_path) if checkpoint_path is not None else None
    checkpoint_json = checkpoint.with_suffix(".json") if checkpoint is not None else None
    destinations = [p.resolve() for p in (path, checkpoint, checkpoint_json) if p is not None]
    if len(destinations) != len(set(destinations)):
        raise ValueError("Raw and checkpoint destinations must be distinct")
    for destination in (checkpoint, checkpoint_json):
        if destination is not None and (destination.exists() or destination.is_symlink()):
            raise FileExistsError("Refusing to replace an existing checkpoint: " + str(destination))
    mesh_t: list[float] = []
    mesh_y: list[Any] = []
    segments: list[DenseCubic] = []
    roots: list[dict[str, Any]] = []
    unresolved: list[dict[str, Any]] = []
    branches: list[dict[str, Any]] = []
    registry = _CandidateRegistry(f, budget)
    solver = None
    failure = None
    completed = False
    versions = {"python": platform.python_version()}
    discontinuities: list[dict[str, Any]] = []
    derivative_discrepancy = 0.0
    scan_counts = {"quarter_scan_sign_changes": 0, "polynomial_roots_without_scan_sign_change": 0}
    plateau_count = 0
    current_interval = [0.0, 0.0]
    checkpoint_metadata = None
    last_checkpoint_step = 0
    last_checkpoint_time = budget.started
    checkpoint_identity: dict[Path, tuple[int, int]] = {}
    np = None

    def atomic_owned(destination: Path, writer: Callable[[Any], None]) -> None:
        """Replace only checkpoints created by this invocation, never inputs."""
        expected = checkpoint_identity.get(destination)
        if destination.exists() or destination.is_symlink():
            observed = destination.lstat()
            if expected != (observed.st_dev, observed.st_ino) or destination.is_symlink():
                raise ReferenceFailure("Checkpoint path ownership changed: " + str(destination))
        elif expected is not None:
            raise ReferenceFailure("Checkpoint disappeared during reference generation")
        temporary = None
        try:
            with tempfile.NamedTemporaryFile(mode="wb", dir=destination.parent,
                                             prefix=destination.name+".", suffix=".tmp", delete=False) as stream:
                temporary = Path(stream.name)
                writer(stream)
                stream.flush()
                os.fsync(stream.fileno())
            # Close first for platforms that forbid renaming open files.
            if expected is None:
                os.link(temporary, destination)
                temporary.unlink()
            else:
                os.replace(temporary, destination)
            stat = destination.lstat()
            checkpoint_identity[destination] = (stat.st_dev, stat.st_ino)
        finally:
            if temporary is not None and temporary.exists():
                temporary.unlink()

    if checkpoint_json is not None:
        initial_progress = {"fixture": f.name, "level": level, "settings": settings,
                            "limits": asdict(budget.limits), "status": "inconclusive_reference",
                            "coverage": COVERAGE, "phase": "before_numerical_imports",
                            "counts": budget.report(), "mesh_final_time_s": None}
        atomic_owned(checkpoint_json, lambda stream: stream.write(_canonical(initial_progress)+b"\n"))

    def save_checkpoint(*, force: bool = False) -> None:
        nonlocal checkpoint_metadata, last_checkpoint_step, last_checkpoint_time
        if checkpoint is None or np is None:
            return
        if not force and budget.counts["accepted_steps"]-last_checkpoint_step < 2048 and (
                time.monotonic()-last_checkpoint_time < 10.0):
            return
        data = _raw_arrays(mesh_t, mesh_y, segments, registry)
        metadata = {"fixture": f.name, "level": level, "settings": settings,
                    "limits": asdict(budget.limits), "counts": budget.report(),
                    "status": "inconclusive_reference", "coverage": COVERAGE,
                    "phase": "partial_integration_and_search", "arrays": _array_hashes(data),
                    "mesh_final_time_s": mesh_t[-1] if mesh_t else None,
                    "dense_final_time_s": segments[-1].t1_s if segments else None,
                    "searched_interval_s": list(current_interval),
                    "root_candidates": roots, "unresolved": unresolved, "branches": branches}
        data["checkpoint_metadata_utf8"] = np.frombuffer(_canonical(metadata), dtype="u1")
        atomic_owned(checkpoint, lambda stream: np.savez_compressed(stream, **data))
        checkpoint_metadata = {"path": str(checkpoint.resolve()), "sha256": _sha(checkpoint.read_bytes()),
                               "mesh_final_time_s": metadata["mesh_final_time_s"],
                               "dense_final_time_s": metadata["dense_final_time_s"],
                               "accepted_steps": budget.counts["accepted_steps"],
                               "status": "inconclusive_reference"}
        atomic_owned(checkpoint_json, lambda stream: stream.write(_canonical(
            {**metadata, "checkpoint": checkpoint_metadata})+b"\n"))
        last_checkpoint_step = budget.counts["accepted_steps"]
        last_checkpoint_time = time.monotonic()

    def append_branch(start: float, end: float, branch: str, segment_id: int) -> None:
        if branches and branches[-1]["branch"] == branch and branches[-1]["interval_s"][1] == start:
            branches[-1]["interval_s"][1] = end
            branches[-1]["last_segment_id"] = segment_id
        else:
            branches.append({"branch": branch, "interval_s": [start, end],
                             "first_segment_id": segment_id, "last_segment_id": segment_id})

    def retain_search(search: dict[str, Any], segment: DenseCubic, segment_id: int,
                      role: str) -> list[dict[str, Any]]:
        nonlocal plateau_count
        for key in scan_counts:
            scan_counts[key] += search[key]
        for item in search["unresolved"]:
            unresolved.append({**item, "segment_id": segment_id, "role": role})
        if search["plateau"]:
            plateau_count += 1
            interval = search["plateau_interval_s"]
            unresolved.append({"reason": "polynomial_plateau_without_ODE_identity",
                               "segment_id": segment_id, "role": role, "interval_s": interval})
            for t in interval:
                registry.retain(t, segment.state(t, budget=budget), segment_id, "plateau_endpoint")
        retained = []
        for root in search["roots"]:
            t = root["time_s"]
            # Roundoff in t0+h*u must never result in extrapolation.
            t = min(segment.t1_s, max(segment.t0_s, t))
            record = {**root, "time_s": t, "segment_id": segment_id, "role": role,
                      "contact": "hammer", "observables": ["hammer_compression", "hammer_force"]}
            state = segment.state(t, budget=budget)
            gap = float(state[4]-f.bh[0]*state[0]-f.bh[1]*state[1]-f.clearance)
            record["eligible_positive_branch"] = gap > 0.0
            record["gap_m"] = gap
            candidate_id = registry.retain(t, state, segment_id, role, {
                "role": role, "bracket_lo_s": root["bracket_lo_s"],
                "bracket_hi_s": root["bracket_hi_s"], "status": root["status"],
                "crossing": root["crossing"]})
            record["candidate_id"] = candidate_id
            roots.append(record)
            retained.append(record)
        return retained

    try:
        budget.check()
        np = _numpy()
        import scipy
        from scipy.integrate import Radau
        versions.update(numpy=np.__version__, scipy=scipy.__version__)
        budget.check()
        y0 = initial_vector(f)
        mesh_t.append(0.0)
        mesh_y.append(y0.copy())
        registry.retain(0.0, y0, -1, "mesh_endpoint")
        save_checkpoint(force=True)
        independent_rhs = rhs(f)

        def counted_rhs(t: float, y: Any) -> Any:
            budget.charge("rhs_evaluations")
            derivative = independent_rhs(t, y)
            if not np.all(np.isfinite(derivative)):
                raise ReferenceFailure("Nonfinite independent RHS")
            return derivative

        solver = Radau(counted_rhs, 0.0, y0, f.duration, max_step=settings["maximum_step_s"],
                       rtol=settings["relative_tolerance"],
                       atol=np.asarray(settings["absolute_tolerance_vector"]), vectorized=False)
        while solver.status == "running":
            budget.ensure("accepted_steps")
            t0, previous = float(solver.t), solver.y.copy()
            current_interval = [t0, f.duration]
            message = solver.step()
            if solver.status == "failed":
                raise ReferenceFailure("Radau step failed: " + str(message))
            t1, accepted = float(solver.t), solver.y.copy()
            if not t1 > t0 or not np.all(np.isfinite(accepted)):
                raise ReferenceFailure("Radau did not accept a finite advancing step")
            # If elapsed time expired within step(), preserve this accepted mesh
            # endpoint before the next cooperative check raises.
            budget.counts["accepted_steps"] += 1
            mesh_t.append(t1)
            mesh_y.append(accepted)
            current_interval = [t0, t1]
            budget.check()
            segment_id = len(segments)
            segment = reconstruct_cubic(solver.dense_output(), t0, t1, budget=budget)
            segments.append(segment)
            for t, exact in ((t0, previous), (t1, accepted)):
                registry.retain(t, exact, segment_id, "mesh_endpoint")
                dense_state = segment.state(t, budget=budget)
                discrepancy = np.abs(exact-dense_state)
                if np.any(discrepancy != 0.0):
                    registry.retain(t, dense_state, segment_id, "dense_endpoint")
                    discontinuities.append({"time_s": t, "segment_id": segment_id,
                                            "side": "right" if t == t0 else "left",
                                            "maximum_state_discrepancy": float(np.max(discrepancy))})
            delta, physical_w, dense_d = _contact_coefficients(f, segment)
            # A declared midpoint probe measures the two derivative notions;
            # their full root searches remain independent below.
            for u in (0.5,):
                budget.charge("search_evaluations", 2)
                difference = abs(float(evaluate_polynomial(physical_w, u))
                                 - float(evaluate_polynomial(dense_d, u)))
                derivative_discrepancy = max(derivative_discrepancy, difference)
            boundary_search = isolate_polynomial_roots(delta, t0, t1,
                                                        flat_threshold=FLAT_GAP_M, budget=budget)
            boundary_roots = retain_search(boundary_search, segment, segment_id, "branch_boundary")
            cuts = sorted({0.0, 1.0, *[min(1.0, max(0.0, root.get("polynomial_root_u", root["u"])))
                                      for root in boundary_roots]})
            for lo, hi in zip(cuts, cuts[1:]):
                budget.charge("search_evaluations")
                midpoint_gap = float(evaluate_polynomial(delta, lo+(hi-lo)/2))
                start, end = t0+(t1-t0)*lo, t0+(t1-t0)*hi
                branch = "positive_compression" if midpoint_gap > 0.0 else "separated"
                append_branch(start, end, branch, segment_id)
                if midpoint_gap <= 0.0:
                    continue
                for role, coefficients in (("physical_rhs_derivative", physical_w),
                                            ("dense_derivative", dense_d)):
                    search = isolate_polynomial_roots(coefficients, t0, t1, interval_u=(lo, hi),
                                                       flat_threshold=FLAT_SPEED_M_PER_S, budget=budget)
                    retain_search(search, segment, segment_id, role)
            save_checkpoint()
        completed = solver.status == "finished" and float(solver.t) == f.duration
        if not completed:
            raise ReferenceFailure("Radau did not cover the complete declared fixture duration")
    except (ReferenceFailure, ImportError, ValueError, RuntimeError, ArithmeticError) as exc:
        failure = {"type": type(exc).__name__, "reason": str(exc), "interval_s": current_interval}
        unresolved.append({**failure, "reason": "reference_execution_failure", "detail": str(exc)})
        save_checkpoint(force=True)

    # This finalization is bounded by the already enforced storage caps. It is
    # intentionally allowed after deadline so a cooperative stop retains data.
    arrays = _raw_arrays(mesh_t, mesh_y, segments, registry) if np is not None else {}
    hashes = _array_hashes(arrays)
    raw: dict[str, Any] = {"arrays": hashes, "basis": "ascending normalized u=(t-t0)/(t1-t0)",
                           "state_coordinates": list(STATE_COORDINATES),
                           "candidate_origin_bits": dict(CANDIDATE_ORIGINS)}
    if path is not None and np is not None:
        with path.open("xb") as stream:
            _numpy().savez_compressed(stream, **arrays)
        raw.update(path=str(path.resolve()), sha256=_sha(path.read_bytes()))
    fixture_definition = {**asdict(f), "evaluated_stiffness_n_per_m": list(f.stiffness()),
                          "evaluated_damping_ns_per_m": list(f.damping())}
    source_paths = (Path(__file__), Path(__file__).with_name("validate_contact_reference.py"))
    resolved_clusters = [r for r in roots if r["role"] == "physical_rhs_derivative"
                         and r["status"] == "resolved_numerical"
                         and r["crossing"] == "positive_to_negative" and r["eligible_positive_branch"]]
    onset = next((r for r in roots if r["role"] == "branch_boundary"
                  and r["crossing"] == "negative_to_positive"), None)
    diagnostic_complete = completed and failure is None
    result: dict[str, Any] = {
        "schema_version": SCHEMA_VERSION, "reference_kind": "independent_supported_contact_extrema",
        "fixture": f.name, "level": level, "status": "completed" if diagnostic_complete else "inconclusive_reference",
        "value_status": "requires_refinement" if diagnostic_complete else "inconclusive_reference",
        "coverage": COVERAGE, "settings": settings,
        "limits": asdict(budget.limits), "counts": budget.report(), "versions": versions,
        "solver": {"method": "scipy.integrate.Radau", "status": getattr(solver, "status", "not_created"),
                   "nfev": int(getattr(solver, "nfev", 0)), "njev": int(getattr(solver, "njev", 0)),
                   "nlu": int(getattr(solver, "nlu", 0)),
                   "rhs_count_includes_finite_difference_jacobian": True},
        "complete_duration": diagnostic_complete, "duration_s": f.duration,
        "final_time_s": mesh_t[-1] if mesh_t else None,
        "failure": failure, "peaks": _peaks(f, registry, diagnostic_complete),
        "root_candidates": roots, "resolved_peak_clusters": resolved_clusters,
        "branches": branches, "unresolved": unresolved, "raw": raw,
        "checkpoint": checkpoint_metadata,
        "fixture_definition": fixture_definition, "fixture_sha256": _sha(_canonical(fixture_definition)),
        "source_sha256": {p.name: _sha(p.read_bytes()) for p in source_paths},
        "search_statistics": {**scan_counts, "polynomial_plateau_count": plateau_count,
                               "maximum_physical_dense_derivative_discrepancy_m_per_s": derivative_discrepancy,
                               "derivative_discrepancy_probe": "one midpoint of every accepted dense segment",
                               "maximum_reconstruction_residual": float(max(
                                   (float(_numpy().max(s.residual_by_component)) for s in segments), default=0.0)),
                               "endpoint_discrepancy_count": len(discontinuities),
                               "maximum_endpoint_state_discrepancy": max(
                                   (d["maximum_state_discrepancy"] for d in discontinuities), default=0.0)},
        "endpoint_discrepancies": discontinuities,
        "analytic_checks": {"first_onset_expected_s": 0.0002,
                            "first_onset_observed_s": onset["time_s"] if onset else None,
                            "first_onset_absolute_error_s": abs(onset["time_s"]-0.0002) if onset else None,
                            "hammer_force_equals_k_times_compression": True,
                            "disabled_damper_exact_zero": True},
        "interpretation": "Independent numerical evidence only; no trajectory error enclosure, coverage certificate, candidate acceptance or physical-fidelity claim.",
    }
    if path is None:
        result["raw_arrays"] = {name: array.tolist() for name, array in arrays.items()}
    return result


def _load_arrays(reference: dict[str, Any]) -> dict[str, Any]:
    np = _numpy()
    metadata = reference["raw"]
    if "raw_arrays" in reference:
        arrays = {name: np.asarray(value, dtype=metadata["arrays"][name]["dtype"]).reshape(
            metadata["arrays"][name]["shape"]) for name, value in reference["raw_arrays"].items()}
    else:
        path = Path(metadata["path"])
        if _sha(path.read_bytes()) != metadata["sha256"]:
            raise ValueError("Raw reference byte hash mismatch")
        with np.load(path, allow_pickle=False) as archive:
            arrays = {key: archive[key] for key in archive.files}
    if _array_hashes(arrays) != metadata["arrays"]:
        raise ValueError("Raw reference per-array hash/shape/dtype mismatch")
    return arrays


def evaluate_reference(reference: dict[str, Any], time_s: float, *, side: str = "right") -> dict[str, Any]:
    """Evaluate retained dense state and independent physics at a reported time.

    Side chooses the dense segment at a shared endpoint, never t+/-epsilon.
    Raw bytes and per-array identities are verified before use. This deliberate
    per-call verification suits compact candidate batches, not audio processing.
    """
    np = _numpy()
    f = _fixture(reference["fixture"])
    if type(time_s) not in (int, float) or not math.isfinite(time_s) or not 0 <= time_s <= f.duration:
        raise ValueError("Reference evaluation time is outside the fixture")
    if side not in ("left", "right"):
        raise ValueError("Reference side must be left or right")
    arrays = _load_arrays(reference)
    starts, ends = arrays["dense_t0_s"], arrays["dense_t1_s"]
    if not len(starts):
        raise ValueError("Reference has no retained dense interval")
    index = int(np.searchsorted(starts, time_s, side=side)) - 1
    index = min(len(starts)-1, max(0, index))
    if not starts[index] <= time_s <= ends[index]:
        raise ValueError("Reference has no complete dense coverage at requested time")
    segment = DenseCubic(float(starts[index]), float(ends[index]), arrays["dense_coefficients"][index],
                         arrays["reconstruction_residual"][index], arrays["reconstruction_limit"][index])
    y = segment.state(float(time_s))
    state = observe(f, float(time_s), y)
    _, _, derivative = _contact_coefficients(f, segment)
    dense_speed = float(evaluate_polynomial(derivative,
                                           (time_s-segment.t0_s)/(segment.t1_s-segment.t0_s)))
    gap, speed = float(state["hammer_gap_m"]), float(state["hammer_relative_velocity_m_per_s"])
    return {"time_s": float(time_s), "side": side, "segment_id": index,
            "state": state, "branch": "positive_compression" if gap > 0 else "separated",
            "hammer_compression_m": max(0.0, gap), "hammer_force_n": f.kh*max(0.0, gap),
            "damper_compression_m": 0.0, "damper_force_n": 0.0,
            "physical_rhs_derivative_m_per_s": speed,
            "dense_derivative_m_per_s": dense_speed,
            "physical_force_derivative_n_per_s": f.kh*speed if gap > 0 else 0.0,
            "physical_dense_derivative_discrepancy_m_per_s": abs(speed-dense_speed),
            "coverage": COVERAGE}


def compare_refinement(references: Sequence[dict[str, Any]]) -> dict[str, Any]:
    """Compare all R0/R1/R2 values without changing a budget or claiming coverage.

    Physical maximum clusters must remain in order, of the same classification,
    and uniquely paired within the proposed supplemental timing budget. An
    unbracketed/tangent candidate remains in the unresolved ledger; if it could
    compete with a maximum the value comparison is inconclusive.
    """
    if len(references) != 3 or {r.get("level") for r in references} != set(LEVEL_NAMES):
        raise ValueError("Exactly one reference at each R0, R1 and R2 level is required")
    ordered = sorted(references, key=lambda r: LEVEL_NAMES.index(r["level"]))
    if len({r.get("fixture") for r in ordered}) != 1:
        raise ValueError("Refinement references must use the same fixture")
    f = _fixture(ordered[0]["fixture"])
    issues = []
    if len({r["fixture_sha256"] for r in ordered}) != 1:
        raise ValueError("Reference fixture definitions differ")
    if len({_canonical(r["source_sha256"]) for r in ordered}) != 1:
        raise ValueError("Reference source identities differ; regenerate a coherent R0/R1/R2 set")
    for reference in ordered:
        if reference["settings"] != LEVELS[reference["level"]].settings():
            raise ValueError("Reference settings differ from the declared level")
        if reference["status"] != "completed" or not reference["complete_duration"]:
            issues.append(reference["level"] + ": incomplete reference execution")
    cluster_stats = []
    branch_stats = []
    for coarse, fine in zip(ordered, ordered[1:]):
        left_branches = [b["branch"] for b in coarse["branches"]]
        right_branches = [b["branch"] for b in fine["branches"]]
        stable_branches = left_branches == right_branches
        branch_stats.append({"levels": [coarse["level"], fine["level"]],
                             "stable_classification": stable_branches,
                             "coarse_sequence": left_branches, "fine_sequence": right_branches})
        if not stable_branches:
            issues.append(coarse["level"]+"/"+fine["level"]+": changed branch sequence")
        left, right = coarse["resolved_peak_clusters"], fine["resolved_peak_clusters"]
        differences = []
        stable = len(left) == len(right)
        if stable:
            for a, b in zip(left, right):
                limit = 1e-7 + 1e-4*abs(b["time_s"])
                error = abs(a["time_s"]-b["time_s"])
                partners = [x for x in left if abs(x["time_s"]-b["time_s"]) <= limit]
                pair_ok = error <= limit and len(partners) == 1 and a["crossing"] == b["crossing"]
                stable = stable and pair_ok
                differences.append({"coarse_time_s": a["time_s"], "fine_time_s": b["time_s"],
                                    "absolute_difference_s": error, "supplemental_budget_s": limit,
                                    "unambiguous_pair": pair_ok})
        if not stable:
            issues.append(coarse["level"] + "/" + fine["level"] + ": lost or ambiguous physical peak cluster")
        cluster_stats.append({"levels": [coarse["level"], fine["level"]], "stable": stable,
                              "coarse_count": len(left), "fine_count": len(right), "pairs": differences})
    peaks = {}
    for key in PEAK_KEYS:
        field = "value_m" if key.endswith("compression") else "value_n"
        values = [r["peaks"][key][field] for r in ordered]
        budget = value_budget(key, values[2])
        differences = [abs(b-a) for a, b in zip(values, values[1:])]
        classifications = [r["peaks"][key]["classification"] for r in ordered]
        stable = len(set(classifications)) == 1
        complete = not issues and stable and all(d <= 0.1*budget for d in differences)
        # Any search pathology is preserved and blocks a resolved value claim
        # in this first narrow implementation. No sampled upper bound is used
        # to erase an unresolved interval.
        unresolved_count = sum(len(r["unresolved"]) for r in ordered)
        if unresolved_count:
            complete = False
        disabled = key.startswith("damper_")
        times = [r["peaks"][key]["maximizer_intervals_s"] for r in ordered]
        time_differences = []
        time_status = "inconclusive_reference"
        if disabled:
            time_status = "disabled"
        elif all(r["complete_duration"] for r in ordered):
            if any(len(intervals) != 1 for intervals in times):
                time_status = "inapplicable_tied_or_plateau"
            else:
                for left, right in zip(times, times[1:]):
                    a, b = left[0], right[0]
                    difference = abs((a[0]+a[1])/2-(b[0]+b[1])/2)
                    limit = 1e-7 + 1e-4*abs((b[0]+b[1])/2)
                    time_differences.append({"absolute_difference_s": difference,
                                             "supplemental_budget_s": limit,
                                             "within_budget": difference <= limit})
                if not issues and not unresolved_count and all(t["within_budget"] for t in time_differences):
                    time_status = "resolved_under_declared_refinement"
        peaks[key] = {"values": dict(zip(LEVEL_NAMES, values)), "successive_absolute_differences": differences,
                      "budget_at_R2": budget, "required_maximum_difference": 0.1*budget,
                      "successive_budget_fractions": [d/budget for d in differences],
                      "classifications": classifications, "classification_stable": stable,
                      "unresolved_count": unresolved_count,
                      "value_status": "analytic_disabled_identity" if disabled else (
                          "resolved_under_declared_refinement" if complete else "inconclusive_reference"),
                      "time_status": time_status,
                      "supplemental_time_refinements": time_differences,
                      "maximizer_intervals_by_level_s": dict(zip(LEVEL_NAMES, times)),
                      "coverage": COVERAGE,
                      "maximizer_intervals_s": ordered[2]["peaks"][key]["maximizer_intervals_s"]}
    resolved = not issues and all(p["value_status"] in ("resolved_under_declared_refinement", "analytic_disabled_identity")
                                  for p in peaks.values())
    return {"fixture": f.name, "status": "resolved_under_declared_refinement" if resolved else "inconclusive_reference",
            "coverage": COVERAGE, "peaks": peaks, "peak_cluster_refinements": cluster_stats,
            "branch_refinements": branch_stats,
            "issues": issues, "levels": list(LEVEL_NAMES),
            "derivative_discrepancy_m_per_s": {r["level"]: r["search_statistics"][
                "maximum_physical_dense_derivative_discrepancy_m_per_s"] for r in ordered},
            "counts": {r["level"]: r["counts"] for r in ordered},
            "interpretation": "Observed refinement agreement is not an ODE enclosure or a continuous-domain coverage proof."}


def self_check() -> dict[str, Any]:
    """Analytic locator/derivative/cap checks; no candidate or contact solver run."""
    np = _numpy()
    checks: dict[str, Any] = {}

    def require(condition: bool, message: str) -> None:
        if not condition:
            raise ReferenceFailure("Self-check failed: " + message)

    coefficients = np.array([[0.3, -2.0, 0.7, 4.0], [2.0, 0.0, 0.0, 0.0]])
    segment = reconstruct_cubic(lambda t: evaluate_polynomial(coefficients, (t-2)/3), 2.0, 5.0)
    require(bool(np.max(np.abs(segment.coefficients-coefficients)) < 2e-14), "known cubic reconstruction")
    derivative = polynomial_derivative(coefficients, time_scale_s=3.0)
    require(abs(float(evaluate_polynomial(derivative[0], 0.4))-(-2+1.4*0.4+12*0.4**2)/3) < 1e-15,
            "physical normalized derivative scaling")
    checks["known_cubic"] = {"maximum_coefficient_error": float(np.max(np.abs(segment.coefficients-coefficients)))}
    close_coeff = np.polynomial.polynomial.polyfromroots([0.41, 0.41002])
    close = isolate_polynomial_roots(close_coeff)
    require(len(close["roots"]) == 2 and close["quarter_scan_sign_changes"] == 0,
            "two nearby roots hidden from same-sign quarter scan")
    require(all(r["status"] == "resolved_numerical" for r in close["roots"]), "close roots have separate brackets")
    require(all(r["bracket_hi_s"]-r["bracket_lo_s"] <= ROOT_WIDTH_S for r in close["roots"]), "retained widths")
    checks["close_roots"] = {"root_times_s": [r["time_s"] for r in close["roots"]],
                             "quarter_scan_sign_changes": close["quarter_scan_sign_changes"]}
    even = isolate_polynomial_roots([0.25, -1.0, 1.0])
    require(bool(even["roots"] and even["unresolved"] and not even["plateau"]), "even root remains unresolved, not plateau")
    zero = isolate_polynomial_roots([0.0, 0.0, 0.0])
    flat = isolate_polynomial_roots([1e-20], flat_threshold=1e-18)
    require(zero["plateau"] and not zero["roots"], "exact zero polynomial interval")
    require(not flat["plateau"] and any(r["reason"] == "numerically_flat_unresolved" for r in flat["unresolved"]),
            "nonzero near-flat function is not zero plateau")
    endpoints = isolate_polynomial_roots([0.0, -1.0, 1.0])
    require([r["time_s"] for r in endpoints["roots"]] == [0.0, 1.0], "shared endpoint zeros retained exactly")
    checks["special_roots"] = {"even_unresolved_count": len(even["unresolved"]),
                                "zero_plateau": zero["plateau"], "near_flat_reason": flat["unresolved"][0]["reason"]}
    # q=u^2 has dense derivative 2u. A separately interpolated velocity
    # 3u-1 vanishes at 1/3, exposing the distinction without an ODE solver.
    dense_roots = isolate_polynomial_roots(polynomial_derivative([0.0, 0.0, 1.0]))
    physical_roots = isolate_polynomial_roots([-1.0, 3.0])
    require(dense_roots["roots"][0]["time_s"] == 0.0 and
            abs(physical_roots["roots"][0]["time_s"]-1/3) < ROOT_WIDTH_S, "physical and dense roots stay distinct")
    checks["derivative_distinction"] = {"dense_root_s": 0.0, "physical_root_s": physical_roots["roots"][0]["time_s"]}
    for counter in ("accepted_steps", "rhs_evaluations", "search_evaluations", "candidates"):
        cap = WorkBudget({counter: 1})
        cap.charge(counter)
        try:
            cap.charge(counter)
        except BudgetExceeded:
            require(cap.counts[counter] == 1, counter+" cap stops before extra work")
        else:
            raise ReferenceFailure("Self-check failed: cap did not stop " + counter)
    checks["live_counters"] = "all four counters stopped before the second charge at cap one"
    try:
        reconstruct_cubic(lambda t: np.array([t**4]), 0.0, 1.0)
    except ReferenceFailure:
        checks["noncubic_rejection"] = True
    else:
        raise ReferenceFailure("Self-check failed: noncubic evaluator was accepted")
    return {"status": "pass", "scope": "independent analytic helper checks only", "checks": checks,
            "coverage": COVERAGE}


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--fixture", choices=SUPPORTED_FIXTURES)
    parser.add_argument("--level", choices=LEVEL_NAMES, default="R2")
    parser.add_argument("--output", type=Path, help="exclusive compact result JSON; optional for self-check")
    parser.add_argument("--raw", type=Path, help="exclusive NPZ (defaults to output with .npz suffix)")
    parser.add_argument("--limits-json", help="JSON object tightening declared live caps")
    parser.add_argument("--self-check", action="store_true")
    args = parser.parse_args()
    if not args.self_check and not args.fixture:
        parser.error("Specify --fixture or --self-check")
    if args.fixture and args.output is None:
        parser.error("Reference worker requires --output to retain its result")
    if args.output is not None and (args.output.exists() or args.output.is_symlink()):
        parser.error("Refusing to overwrite an existing JSON artifact")
    if args.fixture and args.raw is not None and args.raw.resolve() == args.output.resolve():
        parser.error("JSON and NPZ output destinations must be distinct")
    try:
        result = self_check() if args.self_check else generate_reference(
            args.fixture, args.level, limits=json.loads(args.limits_json) if args.limits_json else None,
            raw_path=args.raw or args.output.with_suffix(".npz"),
            checkpoint_path=(args.raw or args.output.with_suffix(".npz")).with_name(
                (args.raw or args.output.with_suffix(".npz")).stem+".partial.npz"))
        payload = json.dumps(result, indent=2, allow_nan=False)+"\n"
        if args.output:
            with args.output.open("x", encoding="utf-8") as stream:
                stream.write(payload)
            print(json.dumps({"status": result["status"], "report": str(args.output),
                              "coverage": COVERAGE, "counts": result.get("counts")}))
        else:
            print(payload, end="")
        return 0 if result["status"] in ("completed", "pass") else 2
    except (ValueError, OSError, ReferenceFailure, TypeError) as exc:
        print(json.dumps({"status": "inconclusive_reference", "error": str(exc), "coverage": COVERAGE}))
        return 2


if __name__ == "__main__":
    sys.exit(main())
