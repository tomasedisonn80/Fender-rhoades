#!/usr/bin/env python3
"""Independent Q6 audio validation against analytic, continuously filtered volts.

The case documents, physical trajectories and Q16/Q32/Q64 refinement protocol
are shared with the unchanged filtered-audio study. Comparison A here instead
uses a separately constructed SciPy six-point Gauss-Legendre rule; neither the
production coefficient header nor its generator is imported. Comparison B uses
the same resolved Q64 continuous reference and unchanged exploratory limits.

--help and --plan require only the Python standard library. Numerical runs need
optional offline NumPy/SciPy. Existing output paths are refused. Every run keeps
its complete plan, source/binary identities, probe streams, float64 arrays and
per-case observations. Exit codes: 0 pass, 1 numerical failure, 2 unresolved
reference only, 3 incomplete/operational failure. A pilot is not full coverage.
"""
from __future__ import annotations

import argparse
from collections import Counter
from datetime import datetime, timezone
from functools import lru_cache
import hashlib
import importlib.util
import json
import math
from pathlib import Path
import platform
import subprocess
import sys
from typing import Any


ROOT = Path(__file__).resolve().parents[1]
_SPEC = importlib.util.spec_from_file_location(
    "rhodes_filtered_audio_shared_for_quadrature",
    Path(__file__).with_name("validate_filtered_audio.py"))
if _SPEC is None or _SPEC.loader is None:
    raise RuntimeError("Cannot load the independent analytic reference")
reference = importlib.util.module_from_spec(_SPEC)
sys.modules[_SPEC.name] = reference
_SPEC.loader.exec_module(reference)

# Public aliases keep test/analysis callers on the exact original case contract.
Case, Event = reference.Case, reference.Event
build_cases = reference.build_cases
canonical_bytes, object_hash = reference.canonical_bytes, reference.object_hash
file_hash, array_hash = reference.file_hash, reference.array_hash
numerical_libraries = reference.numerical_libraries
RATES, ORDERS = reference.RATES, reference.ORDERS
GATES = dict(reference.GATES)
NODE_COUNT = 6
PROBE_MODEL = "synthetic-quadrature-audio"
OBSERVATION_SCHEME = "gauss-legendre-host-cell"
STUDY_IDENTITY = "synthetic-quadrature-audio-q6-20261123"
SCOPE = (
    "Independent synthetic Q6 loaded-harp voltage study; no gain, phase, onset "
    "or latency fitting. A compares C++ with analytic Q6 convolution; B compares "
    "with a separately refined continuous prototype. Neither is an alias-only "
    "bound, audibility criterion, measured-instrument result or RT-001 acceptance."
)
SOURCE_PATHS = tuple(dict.fromkeys((*reference.SOURCE_PATHS,
    "include/rhodes/quadrature_engine.hpp",
    "include/rhodes/quadrature_fir.hpp",
    "include/rhodes/quadrature_filter_coefficients.hpp",
    "tools/generate_quadrature_filter.py", "tools/generate_audio_filter.py",
    "tools/validate_quadrature_audio.py",
    "docs/specs/QUADRATURE_AUDIO_20261123.md",
)))


def make_plan(suite: str) -> dict[str, Any]:
    """New study identity; exact old event/case documents remain separately bound."""
    old = reference.make_plan(suite)
    plan = {key: value for key, value in old.items() if key != "plan_sha256"}
    plan.update({
        "study_identity": STUDY_IDENTITY, "scope": SCOPE,
        "legacy_filtered_plan_sha256": old["plan_sha256"],
        "case_matrix_sha256": object_hash(old["cases"]),
        "implementation_quadrature_nodes": NODE_COUNT,
        "quadrature_orders_per_unit_cell": [NODE_COUNT, *ORDERS],
        "observation_scheme": OBSERVATION_SCHEME,
        "comparison_a_reference": "Independent analytic Q6 cell convolution",
        "comparison_b_reference": "Independent resolved Q64 continuous-kernel convolution",
        "gates": dict(GATES),
    })
    return {**plan, "plan_sha256": object_hash(plan)}


@lru_cache(maxsize=1)
def quadrature_q6() -> Any:
    """Independent mathematical nodes; no production rounding/taps are read."""
    np, _ = numerical_libraries()
    from scipy.special import roots_legendre
    nodes, weights = roots_legendre(NODE_COUNT)
    delta, weights = (nodes + 1.0) / 2.0, weights / 2.0
    taps = weights[:, None] * reference.kernel(
        np.arange(reference.SUPPORT_FRAMES)[None, :] + delta[:, None])
    normalizer = math.fsum(float(value) for value in taps.reshape(-1))
    if not math.isfinite(normalizer) or normalizer <= 0:
        raise ArithmeticError("Independent Q6 normalizer is invalid")
    delta.setflags(write=False)
    taps.setflags(write=False)
    return reference.Quadrature(NODE_COUNT, delta, taps, normalizer)


class ReferenceBank:
    """Cache per-key analytic voltages, retaining local nonlinear conversion."""
    def __init__(self) -> None:
        self.rate: float | None = None
        self.cache: dict[Any, dict[str, Any]] = {}

    def curves(self, case: Case) -> dict[str, Any]:
        np, _ = numerical_libraries()
        if self.rate != case.sample_rate_hz:
            self.cache.clear()
            self.rate = case.sample_rate_hz
        epochs = reference.reset_epochs(case.frames, case.events)
        total: dict[str, Any] = {}
        for key in case.keys:
            local = tuple(event for event in case.events
                          if event.type not in ("on", "off") or event.key == key)
            signature = (key, case.frames, local)
            if signature not in self.cache:
                trajectory = reference.KeyTrajectory(case.sample_rate_hz, key, local)
                curves = {"quadrature_q6_volts": reference.continuous_reference(
                    trajectory.loaded_volts, case.frames, epochs, quadrature_q6())}
                for order in ORDERS:
                    curves[f"continuous_q{order}_volts"] = reference.continuous_reference(
                        trajectory.loaded_volts, case.frames, epochs, reference.quadrature(order))
                for values in curves.values():
                    if not np.all(np.isfinite(values)):
                        raise ArithmeticError("Nonfinite independent quadrature reference")
                    values.setflags(write=False)
                self.cache[signature] = curves
            for name, values in self.cache[signature].items():
                if name not in total:
                    total[name] = values.copy()
                else:
                    total[name] += values
        return total


def parse_probe_output(raw: bytes, case: Case) -> tuple[Any, dict[str, Any]]:
    np, _ = numerical_libraries()

    def reject_constant(token: str) -> None:
        raise ValueError(f"Nonfinite JSON token: {token}")

    def unique_object(pairs: list[tuple[str, Any]]) -> dict[str, Any]:
        result: dict[str, Any] = {}
        for name, value in pairs:
            if name in result:
                raise ValueError(f"Duplicate probe JSON field: {name}")
            result[name] = value
        return result

    # Native output is bounded by 4096 binary64 samples and small metadata.
    if len(raw) > 1024 * 1024:
        raise ValueError("Probe JSON exceeds the one-megabyte interpretation limit")
    document = json.loads(raw, parse_constant=reject_constant, object_pairs_hook=unique_object)
    if not isinstance(document, dict):
        raise ValueError("Probe output must be one JSON object")
    if document.get("model") != PROBE_MODEL:
        raise ValueError("Unexpected probe model identity")
    if document.get("observation_scheme") != OBSERVATION_SCHEME:
        raise ValueError("Unexpected quadrature observation scheme")
    if "oversampling_factor" in document or "internal_sample_rate_hz" in document:
        raise ValueError("Quadrature probe must not claim uniform internal sampling")
    for name, expected in {
        "schema_version": 1, "frames": case.frames,
        "event_count": len(case.events), "quadrature_nodes": NODE_COUNT,
        "latency_frames": reference.LATENCY_FRAMES,
    }.items():
        if type(document.get(name)) is not int or document[name] != expected:
            raise ValueError(f"Unexpected probe {name}")

    def finite_number(value: Any) -> float:
        if type(value) not in (float, int):
            raise ValueError("Probe values must be finite binary64 numbers")
        try:
            converted = float(value)
        except (OverflowError, ValueError) as exc:
            raise ValueError("Probe numeric conversion overflow") from exc
        if not math.isfinite(converted):
            raise ValueError("Probe values must be finite binary64 numbers")
        return converted

    if finite_number(document.get("sample_rate_hz")).hex() != case.sample_rate_hz.hex():
        raise ValueError("Unexpected probe sample_rate_hz")
    values = document.get("samples_volts")
    if not isinstance(values, list) or len(values) != case.frames:
        raise ValueError("Probe samples must have exactly the requested frame count")
    samples = np.asarray([finite_number(value) for value in values], dtype=np.float64)
    return samples, {key: value for key, value in document.items() if key != "samples_volts"}


def assess_case(case: Case, production: Any, curves: dict[str, Any]) -> dict[str, Any]:
    # Reuse only the pure reporting/gating operation. Its A input slot is named
    # for the historical filtered study; translate locally, never patch globals.
    inputs = {**curves, "same_rate_volts": curves["quadrature_q6_volts"]}
    result = reference.assess_case(case, production, inputs)
    result["comparison_a"]["reference"] = "quadrature_q6_volts"
    result["comparison_b"]["reference"] = "continuous_q64_volts"
    for field in ("raw_array_sha256_little_endian_float64", "raw_array_peak_absolute_volts"):
        del result[field]["same_rate_volts"]
    return result


def source_snapshot() -> dict[str, str]:
    # Test files are captured when present; adding/changing one during a study
    # invalidates the final unchanged-source check instead of hiding the change.
    names = set(SOURCE_PATHS)
    for pattern in ("tests/python/test_quadrature*.py", "tests/cpp/*quadrature*"):
        names.update(path.relative_to(ROOT).as_posix() for path in ROOT.glob(pattern)
                     if path.is_file())
    return {name: file_hash(ROOT / name) for name in sorted(names)}


def execute_case(probe: Path, output: Path, case: Case, bank: ReferenceBank,
                 source_sha256: str) -> dict[str, Any]:
    np, _ = numerical_libraries()
    input_data = reference.probe_input(case)
    reference.write_exclusive(output / "probe" / f"{case.id}.stdin.txt", input_data)
    try:
        raw, stderr = reference.run_probe(probe, case)
    except reference.ProbeFailure as exc:
        reference.write_exclusive(output / "probe" / f"{case.id}.stdout.json", exc.stdout)
        reference.write_exclusive(output / "probe" / f"{case.id}.stderr.txt", exc.stderr)
        raise
    reference.write_exclusive(output / "probe" / f"{case.id}.stdout.json", raw)
    reference.write_exclusive(output / "probe" / f"{case.id}.stderr.txt", stderr)
    production, metadata = parse_probe_output(raw, case)
    curves = bank.curves(case)
    arrays_path = output / "raw" / f"{case.id}.npz"
    with arrays_path.open("xb") as stream:
        np.savez_compressed(stream, production_volts=production, **curves)
    return {
        **case.document(), "input_sha256": object_hash(case.document()),
        "source_snapshot_sha256": source_sha256,
        "probe_stdin_sha256": hashlib.sha256(input_data).hexdigest(),
        "probe_stdout_sha256": hashlib.sha256(raw).hexdigest(),
        "probe_stderr_sha256": hashlib.sha256(stderr).hexdigest(),
        "probe_metadata": metadata, "raw_arrays_path": f"raw/{case.id}.npz",
        "raw_npz_file_sha256": file_hash(arrays_path),
        **assess_case(case, production, curves),
    }


def run_study(probe: Path, output: Path, suite: str) -> int:
    np, scipy = numerical_libraries()
    probe = probe.resolve(strict=True)
    if not probe.is_file():
        raise ValueError("Probe must be an executable regular file")
    plan, cases = make_plan(suite), build_cases(suite)
    sources, binary_hash = source_snapshot(), file_hash(probe)
    normalizers = reference.kernel_normalizer_report()
    normalizers["independent_q6"] = quadrature_q6().normalizer
    normalizers["q6_q64_absolute_difference"] = abs(
        quadrature_q6().normalizer - reference.quadrature(64).normalizer)
    reference.create_output(output)
    reference.write_exclusive(output / "plan.json", canonical_bytes(plan) + b"\n")
    manifest = {
        "schema_version": 1, "study_identity": STUDY_IDENTITY, "scope": SCOPE,
        "suite": suite, "started_at_utc": datetime.now(timezone.utc).isoformat(),
        "plan_sha256": plan["plan_sha256"],
        "legacy_filtered_plan_sha256": plan["legacy_filtered_plan_sha256"],
        "case_matrix_sha256": plan["case_matrix_sha256"],
        "source_sha256": sources, "source_snapshot_sha256": object_hash(sources),
        "probe_binary": str(probe), "probe_binary_sha256": binary_hash,
        "binary_source_binding": "Binary and current source identities are retained; the tool does not attest that the binary was built from these sources.",
        "precision": "IEEE binary64; raw array hashes use C-order little-endian float64 bytes",
        "versions": {"python": sys.version, "numpy": np.__version__,
                     "scipy": scipy.__version__, "platform": platform.platform()},
        "kernel_dc_normalizers": normalizers,
        "reference_method": "Piecewise analytic modal trajectories; per-key nonlinear pickup before full loaded linear harp sum; independent SciPy Q6/Q16/Q32/Q64 nodes; ordinary causal convolution; no fitted alignment.",
        "normalizer_method": "Independent quadrature sums and separately adaptive scalar per-cell integration; production constants are not imported.",
    }
    reference.write_exclusive(output / "manifest.json", canonical_bytes(manifest) + b"\n")
    counts: dict[str, Counter[str]] = {"comparison_a": Counter(), "comparison_b": Counter()}
    failures: dict[str, list[str]] = {"comparison_a": [], "comparison_b": [], "inconclusive": []}
    completed, error = 0, None
    bank = ReferenceBank()
    maxima = {"comparison_a": 0.0, "comparison_b_resolved": 0.0,
              "q16_q32": 0.0, "q32_q64": 0.0}
    maximum_rms = dict(maxima)
    cases_path = output / "cases.jsonl"
    try:
        with cases_path.open("xb") as stream:
            for case in cases:
                row = execute_case(probe, output, case, bank, manifest["source_snapshot_sha256"])
                stream.write(canonical_bytes(row) + b"\n")
                stream.flush()
                completed += 1
                for name in counts:
                    status = row[name]["status"]
                    counts[name][status] += 1
                    if status == "fail":
                        failures[name].append(case.id)
                    elif status.startswith("inconclusive"):
                        failures["inconclusive"].append(case.id)
                metrics = {"comparison_a": row["comparison_a"]["metrics"],
                           "comparison_b_resolved": row["comparison_b"]["metrics"],
                           **row["reference_refinement"]["metrics"]}
                for name, value in metrics.items():
                    if value is not None:
                        maxima[name] = max(maxima[name], value["maximum_absolute_error_volts"])
                        maximum_rms[name] = max(maximum_rms[name], value["rms_error_volts"])
                print(f"[{completed}/{len(cases)}] {case.id}: A={row['comparison_a']['status']}; B={row['comparison_b']['status']}", file=sys.stderr, flush=True)
    except (OSError, ValueError, ArithmeticError, RuntimeError, subprocess.SubprocessError, KeyboardInterrupt) as exc:
        error = f"{type(exc).__name__}: {exc}"
    try:
        unchanged = source_snapshot() == sources and file_hash(probe) == binary_hash
    except OSError as exc:
        unchanged = False
        error = error or f"Source/binary recheck failed: {exc}"
    if not unchanged:
        error = error or "Source or binary bytes changed during this run"
    incomplete = completed != len(cases) or error is not None
    code = (3 if incomplete else 1 if failures["comparison_a"] or failures["comparison_b"]
            else 2 if failures["inconclusive"] else 0)
    summary = {
        "schema_version": 1, "study_identity": STUDY_IDENTITY, "scope": SCOPE,
        "suite": suite, "status": "incomplete" if incomplete else "complete",
        "exit_code": code, "plan_sha256": plan["plan_sha256"],
        "legacy_filtered_plan_sha256": plan["legacy_filtered_plan_sha256"],
        "case_matrix_sha256": plan["case_matrix_sha256"],
        "expected_cases": len(cases), "completed_cases": completed,
        "counts": counts, "case_ids_requiring_attention": failures,
        "maximum_observed_absolute_errors_volts": maxima,
        "maximum_observed_rms_errors_volts": maximum_rms,
        "source_and_binary_bytes_unchanged": unchanged, "operational_error": error,
        "cases_jsonl_sha256": file_hash(cases_path) if cases_path.is_file() else None,
        "finished_at_utc": datetime.now(timezone.utc).isoformat(),
    }
    reference.write_exclusive(output / "summary.json", canonical_bytes(summary) + b"\n")
    print(json.dumps(summary, indent=2, allow_nan=False))
    return code


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--plan", action="store_true", help="print the exact plan without a probe or numerical dependencies")
    parser.add_argument("--suite", choices=("full", "pilot"), default="full",
                        help="fixed 2097-case full study or 36-case development pilot")
    parser.add_argument("--probe", type=Path, help="rhodes_quadrature_probe executable")
    parser.add_argument("--output", type=Path, help="new result directory; parent must exist")
    args = parser.parse_args(argv)
    if args.plan:
        if args.probe is not None or args.output is not None:
            parser.error("--plan writes only stdout; omit --probe and --output")
        print(json.dumps(make_plan(args.suite), indent=2, allow_nan=False))
        return 0
    if args.probe is None or args.output is None:
        parser.error("a numerical run requires --probe and --output (or use --plan)")
    try:
        return run_study(args.probe, args.output, args.suite)
    except (OSError, ValueError, ArithmeticError, RuntimeError, subprocess.SubprocessError) as exc:
        print(f"Quadrature audio diagnostic failed: {exc}", file=sys.stderr)
        return 3


if __name__ == "__main__":
    sys.exit(main())
