#!/usr/bin/env python3
"""Independent analytic peak references for four frozen synthetic contact cases.

Values need only the standard library. Optional damped timing executes this
module's own bounded SciPy worker; it never executes or reads a candidate solver.
"""
from __future__ import annotations

from dataclasses import asdict
from decimal import Decimal, localcontext
from fractions import Fraction
import hashlib
import json
import math
from pathlib import Path
import subprocess
import sys
import time

from validate_contact_reference import fixtures

SUPPORTED_FIXTURES = ("conservative_impact", "damped_impact", "forced_clamp",
                      "preload_equilibrium")
PEAK_KEYS = ("hammer_compression", "hammer_force", "damper_compression", "damper_force")
ROOT_WIDTHS = (1e-10, 1e-12, 1e-14)
ROOT_ITERATIONS = 80
DECIMAL_PRECISION = 80
QUAD_LEVELS = ((1e-11, 1e-9), (1e-13, 1e-11), (1e-15, 1e-13))
QUAD_SUBINTERVAL_LIMIT = 128
SERIES_TERMS = 128
MAX_EVALUATIONS = 10000
WALL_SECONDS = 10.0


def _canonical(value):
    return json.dumps(value, sort_keys=True, separators=(",", ":"), allow_nan=False).encode()


def _peak(kind, unit, interval, times, *, value_status="analytic", time_status="analytic",
          coverage="independent_analytic_domain_argument", details=None):
    return {"kind": kind, "unit": unit, "value_interval": interval,
            "value_status": value_status, "maximizer_intervals_s": times,
            "time_status": time_status, "coverage": coverage, "details": details or {}}


def _zero_peaks(duration, *, disabled):
    kind = "disabled" if disabled else "plateau"
    time_status = "disabled" if disabled else "analytic"
    return {quantity: _peak(kind, unit, [0.0, 0.0], [[0.0, duration]],
                            time_status=time_status,
                            details={"enabled": not disabled,
                                     "whole_zero_maximizer_interval": True})
            for quantity, unit in (("compression", "m"), ("force", "N"))}


def _inconclusive(peaks, reason):
    for record in peaks.values():
        record.update(value_interval=None, value_status="inconclusive_reference",
                      maximizer_intervals_s=[], time_status="inconclusive_reference",
                      coverage="unresolved_reference")
        record["details"]["reason"] = reason
    return peaks


def _require(condition, message):
    if not condition:
        raise ArithmeticError(message)


def _free_domain(f):
    _require(f.mass[0] > 0 and f.hammer_mass > 0 and f.kh > 0,
             "free-impact masses and stiffness must be positive")
    _require(f.frequency[0] == 0 and f.beta == (0.0, 0.0) and f.bh == (1.0, 0.0)
             and f.q == (0.0, 0.0) and f.v == (0.0, 0.0) and not f.relaunch
             and f.kd == 0 and f.clearance == 0,
             "fixture no longer satisfies the free-impact analytic assumptions")


def _conservative(f):
    _free_domain(f)
    _require(f.ch == 0 and f.x == 0 and f.vh > 0,
             "conservative fixture requires zero compression and positive incident speed")
    mu = f.mass[0] * f.hammer_mass / (f.mass[0] + f.hammer_mass)
    omega = math.sqrt(f.kh / mu)
    compression = f.vh / omega
    peak_time, separation = math.pi / (2 * omega), math.pi / omega
    _require(separation < f.duration, "fixture does not cover the full impact")
    detail = {"reduced_mass_kg": mu, "angular_frequency_rad_per_s": omega,
              "separation_time_s": separation,
              "domain_argument": "One positive sinusoidal maximum before separation; negative relative speed afterwards excludes recontact."}
    return {"hammer_compression": _peak("isolated", "m", [compression, compression],
                [[peak_time, peak_time]], details=detail.copy()),
            "hammer_force": _peak("isolated", "N", [f.kh * compression] * 2,
                [[peak_time, peak_time]], details=detail.copy()),
            **{"damper_" + k: v for k, v in _zero_peaks(f.duration, disabled=True).items()}}


def _forced_clamp(f):
    _free_domain(f)
    _require(f.x > 0 and f.vh < 0 and f.kh + f.ch * f.vh <= 0,
             "forced-clamp fixture no longer has a monotonically decreasing clipped gap")
    detail = {"initial_gap_m": f.x, "relative_velocity_m_per_s": f.vh,
              "raw_factor_n_per_m": f.kh + f.ch * f.vh,
              "domain_argument": "Clipped force remains zero while the positive gap decreases linearly; after separation it stays zero."}
    return {"hammer_compression": _peak("isolated", "m", [f.x, f.x], [[0.0, 0.0]],
                                        details=detail),
            "hammer_force": _zero_peaks(f.duration, disabled=False)["force"],
            **{"damper_" + k: v for k, v in _zero_peaks(f.duration, disabled=True).items()}}


def _phi_decimal(x):
    # The series avoids cancellation near zero. Its 128 terms at |x| <= 1/8
    # have a tail far below the declared 80-digit working precision.
    if abs(x) <= Decimal("0.125"):
        power = x * x
        total = Decimal(0)
        for n in range(2, SERIES_TERMS + 2):
            total += power / n if n % 2 == 0 else -power / n
            power *= x
        return total
    return x - (1 + x).ln()


def _damped_values(f):
    _free_domain(f)
    _require(f.ch > 0 and f.x == 0 and f.vh > 0,
             "damped fixture requires positive contact damping and incident speed")
    refinements = []
    with localcontext() as context:
        context.prec = DECIMAL_PRECISION
        m, mh, k, c, w0 = map(Decimal.from_float,
                              map(float, (f.mass[0], f.hammer_mass, f.kh, f.ch, f.vh)))
        mu = m * mh / (m + mh)
        alpha = c / k
        invariant = _phi_decimal(alpha * w0)
        prefactor = 2 * mu * k / (c * c)

        def residual(w):
            return _phi_decimal(alpha * w) + alpha * w / 2 - invariant

        compression = (prefactor * invariant).sqrt()
        # Phi'(w) increases on [0,w0]. The regularized secant average is
        # therefore at least Phi(w0)/w0, giving this conservative loading-time
        # upper bound without importing a numerical integration backend.
        loading_time_upper = 2 * mu * w0 / (k * compression)
        _require(loading_time_upper < Decimal.from_float(float(f.duration)),
                 "damped fixture duration does not cover the loading maximum")
        for width in ROOT_WIDTHS:
            lo, hi = Decimal(0), w0
            flo, fhi = residual(lo), residual(hi)
            _require(flo < 0 < fhi, "damped root endpoints do not bracket a root")
            iterations = 0
            target = Decimal(str(width))
            while hi - lo > target:
                _require(iterations < ROOT_ITERATIONS, "damped scalar iteration budget exhausted")
                middle = (lo + hi) / 2
                _require(lo < middle < hi, "damped scalar bisection stagnated")
                value = residual(middle)
                _require(value.is_finite() and value != 0,
                         "damped scalar sign unresolved at working precision")
                if value > 0:
                    hi, fhi = middle, value
                else:
                    lo, flo = middle, value
                iterations += 1
            w_peak = (lo + hi) / 2
            delta = (prefactor * (invariant - _phi_decimal(alpha * w_peak))).sqrt()
            force = delta * (k + c * w_peak)
            refinements.append({"target_width_m_per_s": width, "iterations": iterations,
                "bracket_m_per_s": [float(lo), float(hi)],
                "bracket_decimal_m_per_s": [str(lo), str(hi)],
                "residual_signs": [-1, 1], "residual_decimal": [str(flo), str(fhi)],
                "velocity_m_per_s": float(w_peak), "compression_maximum_m": float(compression),
                "force_maximum_n": float(force),
                "loading_time_upper_bound_s": float(loading_time_upper)})
        for left, right in zip(refinements, refinements[1:]):
            for field, absolute, relative in (("compression_maximum_m", 1e-8, 1e-4),
                                               ("force_maximum_n", 1e-4, 2e-4)):
                difference = abs(right[field] - left[field])
                budget = 0.1 * (absolute + relative * abs(right[field]))
                _require(difference <= budget, "damped scalar refinements do not resolve the value budget")
                right.setdefault("successive_differences", {})[field] = {
                    "absolute": difference, "required_maximum": budget}
        parameters = {"mu": float(mu), "k": float(k), "c": float(c), "w0": float(w0),
                      "force_velocity": refinements[-1]["velocity_m_per_s"]}
    return refinements[-1]["compression_maximum_m"], refinements[-1]["force_maximum_n"], refinements, parameters


def _time_worker(parameters):
    try:
        import scipy
        from scipy.integrate import quad
    except ImportError as error:
        return {"status": "not_available", "reason": "Optional SciPy backend is unavailable: " + str(error)}
    mu, k, c, w0 = (parameters[name] for name in ("mu", "k", "c", "w0"))
    alpha, start = c / k, time.monotonic()
    a = 1 + alpha * w0
    prefactor = 2 * mu * k / (c * c)
    evaluations = 0
    records = {}

    def integrand(u):
        nonlocal evaluations
        if evaluations >= MAX_EVALUATIONS:
            raise ArithmeticError("damped quadrature evaluation budget exhausted")
        if time.monotonic() - start >= WALL_SECONDS:
            raise ArithmeticError("damped quadrature worker wall budget exhausted")
        evaluations += 1
        r = alpha * u * u / a
        _require(0 <= r <= 2 / 3 + 4 * math.ulp(2 / 3),
                 "regularized series left the verified fixture domain")
        power, series = r, 0.0
        for n in range(1, SERIES_TERMS + 1):
            series += power / (n + 1)
            power *= r
        rho = alpha * alpha * w0 / a - alpha / a * series
        value = 2 * mu / ((k + c * (w0 - u * u)) * math.sqrt(prefactor * rho))
        _require(math.isfinite(value) and value > 0, "nonfinite regularized time integrand")
        return value

    try:
        for key, speed in (("hammer_compression", 0.0),
                           ("hammer_force", parameters["force_velocity"])):
            levels = []
            for epsabs, epsrel in QUAD_LEVELS:
                before = evaluations
                result = quad(integrand, 0.0, math.sqrt(w0 - speed), epsabs=epsabs,
                              epsrel=epsrel, limit=QUAD_SUBINTERVAL_LIMIT, full_output=1)
                value, error, info = result[:3]
                message = "" if len(result) == 3 else str(result[3])
                finite_value, finite_error = math.isfinite(value), math.isfinite(error)
                level = {"epsabs": epsabs, "epsrel": epsrel,
                         "limit": QUAD_SUBINTERVAL_LIMIT,
                         "value_s": float(value) if finite_value else None,
                         "estimated_absolute_error_s": float(error) if finite_error else None,
                         "evaluations": evaluations - before,
                         "reported_evaluations": int(info["neval"]),
                         "subintervals": int(info["last"]), "convergence_message": message}
                levels.append(level)
                records[key] = levels
                if not finite_value or not finite_error:
                    level["nonfinite_result"] = {"value_s": repr(value),
                                                 "estimated_absolute_error_s": repr(error)}
                _require(finite_value and finite_error and value >= 0 and error >= 0,
                         "nonfinite quadrature value/error")
                _require(not message, "SciPy returned a quadrature convergence message")
                _require(info["last"] <= QUAD_SUBINTERVAL_LIMIT,
                         "quadrature subinterval budget exhausted")
                if len(levels) > 1:
                    difference = abs(value - levels[-2]["value_s"])
                    budget = 0.1 * (1e-7 + 1e-4 * abs(value))
                    level["successive_difference_s"] = difference
                    level["required_maximum_difference_s"] = budget
                    _require(difference <= budget, "time refinements do not resolve supplemental budget")
        return {"status": "resolved_under_declared_refinement", "backend": "SciPy",
                "backend_version": scipy.__version__, "evaluations": evaluations,
                "worker_elapsed_s": time.monotonic() - start, "refinements": records,
                "error_interpretation": "QUADPACK error estimates and refinement agreement are not rigorous certificates."}
    except (ArithmeticError, ValueError, OverflowError) as error:
        return {"status": "inconclusive_reference", "reason": str(error),
                "backend": "SciPy", "backend_version": scipy.__version__,
                "evaluations": evaluations, "refinements": records,
                "worker_elapsed_s": time.monotonic() - start}


def _validate_time_refinements(result):
    """Validate every success record before accepting the private transport."""
    def nonnegative_number(value, label):
        if (type(value) not in (int, float) or not math.isfinite(value) or value < 0):
            raise ValueError("reference worker returned invalid " + label)
        return value

    def count(value, label, limit):
        if type(value) is not int or not 0 <= value <= limit:
            raise ValueError("reference worker returned invalid " + label)
        return value

    refinements = result.get("refinements")
    keys = ("hammer_compression", "hammer_force")
    if not isinstance(refinements, dict) or set(refinements) != set(keys):
        raise ValueError("reference worker returned invalid refinements")
    total_evaluations = 0
    for key in keys:
        levels = refinements[key]
        if not isinstance(levels, list) or len(levels) != len(QUAD_LEVELS):
            raise ValueError("reference worker returned incomplete refinements")
        previous = None
        for level, (epsabs, epsrel) in zip(levels, QUAD_LEVELS):
            if not isinstance(level, dict):
                raise ValueError("reference worker returned invalid refinement record")
            value = nonnegative_number(level.get("value_s"), "timing")
            nonnegative_number(level.get("estimated_absolute_error_s"), "error estimate")
            if (nonnegative_number(level.get("epsabs"), "absolute tolerance") != epsabs
                    or nonnegative_number(level.get("epsrel"), "relative tolerance") != epsrel
                    or type(level.get("limit")) is not int
                    or level["limit"] != QUAD_SUBINTERVAL_LIMIT):
                raise ValueError("reference worker returned altered quadrature settings")
            evaluations = count(level.get("evaluations"), "evaluation count", MAX_EVALUATIONS)
            reported = count(level.get("reported_evaluations"), "reported evaluation count", MAX_EVALUATIONS)
            subintervals = count(level.get("subintervals"), "subinterval count", QUAD_SUBINTERVAL_LIMIT)
            if evaluations != reported or subintervals == 0 or level.get("convergence_message") != "":
                raise ValueError("reference worker returned unresolved quadrature evidence")
            total_evaluations += evaluations
            if previous is not None:
                difference = abs(value - previous)
                budget = 0.1 * (1e-7 + 1e-4 * abs(value))
                if (nonnegative_number(level.get("successive_difference_s"), "refinement difference") != difference
                        or nonnegative_number(level.get("required_maximum_difference_s"), "refinement budget") != budget
                        or difference > budget):
                    raise ValueError("reference worker returned unresolved time refinements")
            previous = value
    if count(result.get("evaluations"), "aggregate evaluation count", MAX_EVALUATIONS) != total_evaluations:
        raise ValueError("reference worker returned inconsistent aggregate evaluations")
    nonnegative_number(result.get("worker_elapsed_s"), "worker elapsed time")


def _run_damped_time(parameters):
    started = time.monotonic()
    argv = [sys.executable, str(Path(__file__).resolve()), "--_damped-time-worker",
            json.dumps(parameters, allow_nan=False)]
    try:
        process = subprocess.run(argv, capture_output=True, text=True, encoding="utf-8",
                                 check=False, timeout=WALL_SECONDS)
        if process.returncode != 0:
            return {"status": "inconclusive_reference", "reason": "reference worker failed",
                    "returncode": process.returncode, "stderr": process.stderr[:2048]}
        def reject_nonfinite(token):
            raise ValueError("reference worker returned nonfinite JSON: " + token)

        def finite_float(token):
            value = float(token)
            if not math.isfinite(value):
                raise ValueError("reference worker returned an unrepresentable JSON number")
            return value

        def finite_integer(token):
            value = int(token)
            # An oversized integer is valid JSON, but cannot be used by this
            # binary64 timing interface. Overflow is handled locally below.
            if not math.isfinite(value):
                raise ValueError("reference worker returned an unrepresentable JSON integer")
            return value

        result = json.loads(process.stdout, parse_constant=reject_nonfinite,
                            parse_float=finite_float, parse_int=finite_integer)
        if not isinstance(result, dict) or result.get("status") not in {
                "resolved_under_declared_refinement", "not_available", "inconclusive_reference"}:
            raise ValueError("reference worker returned an invalid status")
        if result["status"] == "resolved_under_declared_refinement":
            _validate_time_refinements(result)
        result["parent_elapsed_s"] = time.monotonic() - started
        result["enforced_wall_cap_s"] = WALL_SECONDS
        return result
    except subprocess.TimeoutExpired:
        # subprocess.run kills and waits for this own reference worker before
        # raising. A slow import is covered by the same active parent deadline.
        return {"status": "inconclusive_reference", "reason": "reference worker exceeded wall cap",
                "enforced_wall_cap_s": WALL_SECONDS, "parent_elapsed_s": time.monotonic() - started,
                "evaluations": None, "refinements": {}, "worker_terminated": True}
    except (OSError, ValueError, TypeError, ArithmeticError, RecursionError) as error:
        return {"status": "inconclusive_reference", "reason": str(error),
                "parent_elapsed_s": time.monotonic() - started}


def _damped(f, requested):
    compression, force, refinements, parameters = _damped_values(f)
    timing = _run_damped_time(parameters) if requested else {
        "status": "not_available", "reason": "Supplemental damped timing was not requested."}
    if timing["status"] == "resolved_under_declared_refinement":
        if any(timing["refinements"][key][-1]["value_s"] > f.duration
               for key in ("hammer_compression", "hammer_force")):
            timing["status"] = "inconclusive_reference"
            timing["reason"] = "damped peak time is outside fixture duration"
    details = {"decimal_precision": DECIMAL_PRECISION, "root_refinements": refinements,
               "root_iteration_cap_per_level": ROOT_ITERATIONS,
               "domain_argument": "The force-root residual is strictly increasing on (0,w0); compression peaks at w=0. Force decreases on rebound, excluding a later larger maximum.",
               "damped_time_requested": requested, "timing": timing}
    result = {"damper_" + k: v for k, v in _zero_peaks(f.duration, disabled=True).items()}
    for key, unit, value in (("hammer_compression", "m", compression), ("hammer_force", "N", force)):
        times = []
        if timing["status"] == "resolved_under_declared_refinement":
            reference_time = timing["refinements"][key][-1]["value_s"]
            times = [[reference_time, reference_time]]
        result[key] = _peak("isolated", unit, [value, value], times,
                           value_status="resolved_under_declared_refinement",
                           time_status=timing["status"], coverage="analytic_domain_and_scalar_refinement",
                           details=details.copy())
    return result


def _outward(value, *, upper):
    result = float(value)
    represented = Fraction(result)
    if (upper and represented < value) or (not upper and represented > value):
        result = math.nextafter(result, math.inf if upper else -math.inf)
    _require(math.isfinite(result) and
             (Fraction(result) >= value if upper else Fraction(result) <= value),
             "outward conversion did not enclose its rational target")
    return result


def _preload(f):
    _require(not f.relaunch and f.vh == 0 and f.v == (0.0, 0.0) and f.face == "constant"
             and f.face_velocity == 0 and f.face_amplitude == 0,
             "rounded preload no longer satisfies its static-face, initially stationary assumptions")
    q0 = tuple(map(Fraction, f.q))
    k = tuple(map(Fraction, f.stiffness()))
    mass = tuple(map(Fraction, f.mass))
    b, h = tuple(map(Fraction, f.bd)), tuple(map(Fraction, f.bh))
    kd, cd, ell = map(Fraction, (f.kd, f.cd, f.face_offset))
    _require(all(x > 0 for x in k) and all(x > 0 for x in mass) and kd > 0 and cd >= 0
             and all(x >= 0 for x in f.damping()), "preload positivity assumptions failed")
    a00, a01, a11 = k[0] + kd * b[0] ** 2, kd * b[0] * b[1], k[1] + kd * b[1] ** 2
    determinant = a00 * a11 - a01 * a01
    _require(determinant > 0, "preload stiffness matrix is not positive definite")
    inv = ((a11 / determinant, -a01 / determinant),
           (-a01 / determinant, a00 / determinant))

    def quadratic(v):
        return sum(v[i] * inv[i][j] * v[j] for i in range(2) for j in range(2))

    compliance = sum(bi * bi / ki for bi, ki in zip(b, k))
    delta = -ell / (1 + kd * compliance)
    equilibrium = tuple(-bi * kd * delta / ki for bi, ki in zip(b, k))
    error = tuple(q - eq for q, eq in zip(q0, equilibrium))
    energy = (a00 * error[0] ** 2 + 2 * a01 * error[0] * error[1] + a11 * error[1] ** 2) / 2
    d_cap, w_cap, h_cap, f_cap = map(Fraction, ("1e-20", "3e-17", "1.1e-20", "1.3e-16"))
    compression_squared = 2 * energy * quadratic(b)
    velocity_squared = 2 * energy * sum(bi * bi / mi for bi, mi in zip(b, mass))
    hammer_squared = 2 * energy * quadratic(h)
    force_variation = kd * d_cap + cd * (delta + d_cap) * w_cap
    hammer_gap = Fraction(f.x) - sum(hi * qi for hi, qi in zip(h, equilibrium)) - Fraction(f.clearance)
    checks = [
        ("positive_stiffness", all(x > 0 for x in k)),
        ("positive_masses", all(x > 0 for x in mass)),
        ("positive_a00", a00 > 0), ("positive_determinant", determinant > 0),
        ("nonnegative_perturbation_energy", energy >= 0),
        ("compression_energy_bound", d_cap ** 2 >= compression_squared),
        ("velocity_energy_bound", w_cap ** 2 >= velocity_squared),
        ("hammer_position_energy_bound", h_cap ** 2 >= hammer_squared),
        ("force_variation_bound", f_cap >= force_variation),
        ("positive_compression_branch", delta - d_cap > 0),
        ("positive_force_factor_branch", kd - cd * w_cap > 0),
        ("separated_hammer_branch", hammer_gap + h_cap < 0),
    ]
    _require(all(passed for _, passed in checks), "rounded preload enclosure or branch certificate failed")
    details = {"rational_checks": [{"name": name, "passed": passed} for name, passed in checks],
        "exact_rational": {"stiffness": list(map(str, k)), "equilibrium": list(map(str, equilibrium)),
            "equilibrium_compression": str(delta), "perturbation_energy_j": str(energy),
            "compression_squared_bound": str(compression_squared),
            "velocity_squared_bound": str(velocity_squared), "hammer_squared_bound": str(hammer_squared),
            "force_variation_bound": str(force_variation),
            "hammer_gap_upper": str(hammer_gap + h_cap)},
        "caps": {"compression_m": float(d_cap), "relative_speed_m_per_s": float(w_cap),
                 "hammer_projection_m": float(h_cap), "force_n": float(f_cap)},
        "domain_argument": "Exact rational positive-definite energy bounds and strict branch margins imply nonincreasing perturbation energy; rounded initial state is not an exact positive plateau.",
        "positive_peak_time": "No isolated maximizer is asserted near the rounded equilibrium."}
    result = {"hammer_" + key: value for key, value in _zero_peaks(f.duration, disabled=False).items()}
    for key, unit, center, cap in (("damper_compression", "m", delta, d_cap),
                                   ("damper_force", "N", kd * delta, f_cap)):
        lower, upper = _outward(center - cap, upper=False), _outward(center + cap, upper=True)
        record_details = details.copy()
        record_details["outward_targets_exact"] = [str(center - cap), str(center + cap)]
        record_details["outward_rounding_verified"] = True
        result[key] = _peak("enclosure", unit, [lower, upper], [],
                            value_status="verified_enclosure", time_status="unresolved_near_equilibrium",
                            coverage="exact_rational_energy_enclosure", details=record_details)
    for key in ("hammer_compression", "hammer_force"):
        result[key]["coverage"] = "exact_rational_separated_branch_enclosure"
        result[key]["details"]["branch_proof"] = details
    return result


def reference_peaks(name: str, *, damped_time: bool = False) -> dict:
    """Return independent maxima; never infer reference parameters from candidates."""
    if name not in SUPPORTED_FIXTURES:
        raise ValueError("unsupported analytic peak fixture: " + str(name))
    if type(damped_time) is not bool:
        raise TypeError("damped_time must be a boolean")
    fixture = fixtures()[name]
    definition = asdict(fixture)
    definition["evaluated_stiffness_n_per_m"] = fixture.stiffness()
    definition["evaluated_background_damping_ns_per_m"] = fixture.damping()
    result = {"fixture": name, "duration_s": fixture.duration, "definition": definition,
              "definition_sha256": hashlib.sha256(_canonical(definition)).hexdigest(),
              "reference_settings": {"damped_time_requested": damped_time,
                  "decimal_precision": DECIMAL_PRECISION, "scalar_widths_m_per_s": list(ROOT_WIDTHS),
                  "scalar_iterations_per_level": ROOT_ITERATIONS, "regularized_series_terms": SERIES_TERMS,
                  "quad_levels": [{"epsabs": a, "epsrel": r} for a, r in QUAD_LEVELS],
                  "quad_subinterval_limit": QUAD_SUBINTERVAL_LIMIT,
                  "quad_evaluation_limit": MAX_EVALUATIONS, "enforced_wall_cap_s": WALL_SECONDS,
                  "candidate_independent": True}, "peaks": {}}
    try:
        if name == "conservative_impact":
            peaks = _conservative(fixture)
        elif name == "forced_clamp":
            peaks = _forced_clamp(fixture)
        elif name == "damped_impact":
            peaks = _damped(fixture, damped_time)
        else:
            peaks = _preload(fixture)
        result["peaks"] = peaks
    except (ArithmeticError, ValueError) as error:
        peaks = {key: _peak("enclosure", "m" if key.endswith("compression") else "N", None, [])
                 for key in PEAK_KEYS}
        result["peaks"] = _inconclusive(peaks, str(error))
    return result


if __name__ == "__main__":
    if len(sys.argv) == 3 and sys.argv[1] == "--_damped-time-worker":
        print(json.dumps(_time_worker(json.loads(sys.argv[2])), allow_nan=False))
    else:
        raise SystemExit("Import reference_peaks(); the private worker is not a candidate executable interface.")
