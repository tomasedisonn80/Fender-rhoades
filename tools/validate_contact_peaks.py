#!/usr/bin/env python3
"""Independently diagnose four synthetic contact fixtures' reported maxima.

Candidate files are data; even --producer-binary is only hashed, never executed.
Passing this bounded diagnostic does not complete PHY-002 or certify arbitrary
continuous trajectories. The existing general contact auditor is unchanged.
"""
from __future__ import annotations

import argparse
from datetime import datetime, timezone
import hashlib
import importlib.metadata
import json
import math
import os
from pathlib import Path
import stat
import sys
import tempfile
from typing import Any

from contact_peak_oracles import reference_peaks
from contact_peak_schema import CandidateFormatError, ParsedCandidate, parse_candidate


ROOT = Path(__file__).resolve().parents[1]
MAX_FILE_BYTES = 32 * 1024 * 1024
MAX_COLLECTION_BYTES = 128 * 1024 * 1024
MAX_FILES = 64
PEAKS = ("hammer_compression", "hammer_force", "damper_compression", "damper_force")
VALUE_READY = {"analytic", "resolved_under_declared_refinement", "verified_enclosure"}
TIME_READY = VALUE_READY | {"disabled"}
EXIT_PRECEDENCE = {0: 0, 2: 1, 1: 2, 3: 3}


def budget(name: str, value: float) -> float:
    """Frozen observable budget, in metres or newtons as named."""
    return (1e-8 + 1e-4 * abs(value) if name.endswith("compression")
            else 1e-4 + 2e-4 * abs(value))


def _finite(value: Any) -> bool:
    return type(value) in (int, float) and math.isfinite(value)


def _interval(value: Any) -> bool:
    return isinstance(value, (list, tuple)) and len(value) == 2 and \
        all(_finite(x) for x in value) and 0 <= value[0] <= value[1]


def _value_check(name: str, observed: float, reference: dict) -> dict:
    bounds = reference.get("value_interval")
    if reference.get("value_status") not in VALUE_READY or not _interval(bounds):
        return {"status": "inconclusive_reference", "reference_interval": bounds,
                "reason": "independent value is unavailable or unresolved"}
    lower, upper = bounds
    sufficient_error = max(abs(observed - lower), abs(observed - upper))
    definite_error = max(lower - observed, observed - upper, 0.0)
    lower_budget, upper_budget = budget(name, lower), budget(name, upper)
    if sufficient_error <= lower_budget:
        status = "pass"
    elif definite_error > upper_budget:
        status = "fail"
    else:
        status = "inconclusive_reference"
    return {"status": status, "reference_interval": bounds,
            "maximum_endpoint_error": sufficient_error, "distance_to_interval": definite_error,
            "sufficient_pass_budget": lower_budget, "definite_failure_budget": upper_budget,
            "reason": "frozen observable budget applied to independent interval endpoints"}


def _time_check(observed: float, reference: dict) -> dict:
    status, intervals = reference.get("time_status"), reference.get("maximizer_intervals_s")
    if status not in TIME_READY or not isinstance(intervals, (list, tuple)) or \
            not intervals or not all(_interval(item) for item in intervals):
        return {"status": status if status in {"not_available", "unresolved_near_equilibrium",
                                               "inconclusive_reference"} else "inconclusive_reference",
                "reference_intervals_s": intervals,
                "reason": "independent maximizing time is separately unavailable or unresolved"}
    if reference.get("kind") in {"plateau", "disabled"}:
        distance = min(max(lo - observed, observed - hi, 0.0) for lo, hi in intervals)
        return {"status": "pass" if distance == 0 else "fail",
                "distance_to_maximizers_s": distance, "reference_intervals_s": intervals,
                "reason": "membership in the complete maximizing interval; zero semantics retained"}
    records = []
    for lower, upper in intervals:
        maximum_error = max(abs(observed - lower), abs(observed - upper))
        distance = max(lower - observed, observed - upper, 0.0)
        low_budget, high_budget = 1e-7 + 1e-4 * lower, 1e-7 + 1e-4 * upper
        records.append({"status": "pass" if maximum_error <= low_budget else
                        "fail" if distance > high_budget else "inconclusive_reference",
                        "maximum_endpoint_error_s": maximum_error,
                        "distance_to_interval_s": distance,
                        "sufficient_pass_budget_s": low_budget,
                        "definite_failure_budget_s": high_budget})
    result = "pass" if any(row["status"] == "pass" for row in records) else \
        "fail" if all(row["status"] == "fail" for row in records) else "inconclusive_reference"
    return {"status": result, "interval_comparisons": records,
            "reference_intervals_s": intervals,
            "reason": "supplemental timing diagnostic; independent time need not lie in the candidate replay bracket"}


def _observations(name: str, value: float, parsed: ParsedCandidate, reference: dict) -> dict:
    contact, quantity = name.split("_")
    disabled = reference.get("kind") == "disabled"
    sample_values = [row[contact + "_force_n"] if quantity == "force" else
                     0.0 if disabled else max(0.0, row[contact + "_gap_m"])
                     for row in parsed.run["samples"]]
    sampled = parsed.run["diagnostics"]["sampled_maxima"]
    diagnostic_name = name + ("_m" if quantity == "compression" else "_n")
    maximum_exported = max(sample_values)
    maximum_diagnostic = sampled[diagnostic_name]
    witness = max(maximum_exported, maximum_diagnostic)
    deficit = max(witness - value, 0.0)
    tolerance = budget(name, witness)
    negative_force = quantity == "force" and any(x < 0 for x in sample_values)
    return {"status": "fail" if deficit > tolerance or negative_force else "pass",
            "maximum_exported_observation": maximum_exported,
            "maximum_native_sampled_diagnostic": maximum_diagnostic,
            "peak_deficit": deficit, "budget": tolerance,
            "negative_exported_force": negative_force,
            "reason": "claimed maximum must cover exported observations and separate sampled diagnostic within the frozen observable budget"}


def compare_candidate(parsed: ParsedCandidate, reference: dict | None) -> dict:
    """Preserve independent validity, value, timing and coverage findings."""
    result = {"fixture": parsed.fixture, "candidate_sha256": parsed.raw_sha256,
              "availability": parsed.availability, "validity": "pass", "peaks": {},
              "settings": dict(parsed.settings), "phy002_acceptance": "incomplete"}
    if parsed.run["success"] is not True or parsed.run["failure_code"] != 0:
        result.update(validity="producer_failure", failure_code=parsed.run["failure_code"],
                      exit_code=1, reason="producer failure retained; partial trajectory is not success")
        return result
    if parsed.availability != "available":
        result.update(exit_code=2, reason="required peak diagnostics missing or fixture outside four-fixture scope")
        return result
    if not isinstance(reference, dict) or reference.get("fixture") != parsed.fixture:
        result.update(validity="inconclusive_reference", exit_code=2,
                      reason="matching independent fixture reference is unavailable")
        return result
    result["definition_sha256"] = reference.get("definition_sha256")
    result["reference_settings"] = reference.get("reference_settings", {})
    requested_damped_time = bool(reference.get("reference_settings", {}).get("damped_time_requested", False))
    codes = [0]
    for name in PEAKS:
        candidate = parsed.peaks[name]
        independent = reference["peaks"][name]
        value = candidate["value_m" if name.endswith("compression") else "value_n"]
        value_check = _value_check(name, value, independent)
        time_check = _time_check(candidate["time_s"], independent)
        observations = _observations(name, value, parsed, independent)
        time_required = independent.get("time_status") in TIME_READY or \
            (parsed.fixture == "damped_impact" and name.startswith("hammer_") and requested_damped_time)
        statuses = [value_check["status"], observations["status"]]
        if time_required:
            statuses.append(time_check["status"])
        code = 1 if "fail" in statuses else 0 if all(x == "pass" for x in statuses) else 2
        codes.append(code)
        result["peaks"][name] = {
            "candidate": dict(candidate), "reference": independent,
            "value_status": value_check["status"], "time_status": time_check["status"],
            "observation_consistency_status": observations["status"],
            "time_comparison_required": time_required,
            "value_comparison": value_check, "time_comparison": time_check,
            "observation_comparison": observations,
            "coverage": {"candidate": parsed.peaks["coverage"],
                         "reference": independent.get("coverage"),
                         "general_continuous_extrema": "incomplete"},
            "exit_code": code,
        }
    result["exit_code"] = aggregate_exit(codes)
    return result


def aggregate_exit(codes) -> int:
    return max(codes, key=EXIT_PRECEDENCE.__getitem__, default=2)


def _utc() -> str:
    return datetime.now(timezone.utc).isoformat()


def _error(error: BaseException) -> str:
    return (type(error).__name__ + ": " + str(error))[:1000]


def _canonical(value) -> bytes:
    # Escaping Unicode also retains POSIX surrogate-escaped input paths without
    # turning an otherwise readable file into a manifest encoding failure.
    return json.dumps(value, sort_keys=True, separators=(",", ":"),
                      ensure_ascii=True, allow_nan=False).encode("utf-8")


def _exclusive_json(path: Path, value) -> None:
    raw = _canonical(value) + b"\n"
    with path.open("xb") as handle:
        handle.write(raw)


def _replace_manifest(path: Path, value) -> None:
    """Replace only this attempt's manifest after completely serializing it."""
    raw = _canonical(value) + b"\n"
    descriptor, name = tempfile.mkstemp(dir=path.parent, prefix=".contact-peaks-")
    temporary = Path(name)
    try:
        with os.fdopen(descriptor, "wb") as handle:
            handle.write(raw)
            handle.flush()
            os.fsync(handle.fileno())
        os.replace(temporary, path)
    finally:
        temporary.unlink(missing_ok=True)


def _regular_reader(path: Path):
    """Reject final symlinks/special files and bind checks to the opened object."""
    prior = path.lstat()
    if not stat.S_ISREG(prior.st_mode):
        raise OSError("input must be a regular file; final symlinks are refused")
    flags = os.O_RDONLY | getattr(os, "O_BINARY", 0) | getattr(os, "O_NOFOLLOW", 0) | getattr(os, "O_NONBLOCK", 0)
    descriptor = os.open(path, flags)
    try:
        actual = os.fstat(descriptor)
        current = path.lstat()
        if not stat.S_ISREG(actual.st_mode) or not stat.S_ISREG(current.st_mode) or \
                (prior.st_dev, prior.st_ino) != (actual.st_dev, actual.st_ino) or \
                (current.st_dev, current.st_ino) != (actual.st_dev, actual.st_ino):
            raise OSError("input changed identity or type during opening")
        return os.fdopen(descriptor, "rb"), actual
    except BaseException:
        os.close(descriptor)
        raise


def _bounded_read(path: Path, limit: int) -> tuple[bytes, bool]:
    handle, _metadata = _regular_reader(path)
    with handle:
        data = handle.read(limit + 1)
        exceeded = len(data) > limit
        # A growing file cannot bypass the bound; a prior stat is not trusted.
        return data[:limit], exceeded


def _binary_identity(path: Path) -> dict:
    handle, metadata = _regular_reader(path)
    digest = hashlib.sha256()
    count = 0
    with handle:
        while block := handle.read(64 * 1024):
            count += len(block)
            if count > metadata.st_size:
                raise OSError("declared producer binary changed size during hashing")
            digest.update(block)
        after = os.fstat(handle.fileno())
    if count != metadata.st_size or (metadata.st_mtime_ns, metadata.st_size) != (after.st_mtime_ns, after.st_size):
        raise OSError("declared producer binary changed during hashing")
    return {"declared_path": str(path), "bytes": count, "sha256": digest.hexdigest(),
            "executed": False, "candidate_production_attested": False}


def _source_identity() -> dict:
    names = ["tools/validate_contact_peaks.py", "tools/contact_peak_schema.py",
             "tools/contact_peak_oracles.py", "tools/validate_contact_reference.py",
             "docs/specs/CONTACT_ANALYTIC_PEAK_SLICE_20261123.md",
             "docs/specs/CONTACT_ACCEPTANCE_20261123.md"]
    return {name: hashlib.sha256((ROOT / name).read_bytes()).hexdigest() for name in names}


def _versions() -> dict:
    result = {"python": sys.version, "numpy": "not_available", "scipy": "not_available"}
    for name in ("numpy", "scipy"):
        try:
            result[name] = importlib.metadata.version(name)
        except importlib.metadata.PackageNotFoundError:
            pass
    return result


def main(argv=None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--candidate", type=Path, action="append", required=True,
                        help="native one-run JSON; repeat up to 64 times")
    parser.add_argument("--output", type=Path, required=True, help="new exclusive evidence directory")
    parser.add_argument("--with-damped-time", action="store_true", help="request optional bounded SciPy damped timing")
    parser.add_argument("--producer-binary", type=Path, help="optional declared producer file to hash only")
    args = parser.parse_args(argv)
    if len(args.candidate) > MAX_FILES:
        parser.error("at most 64 candidate files are accepted")
    output = args.output.absolute()
    try:
        output.mkdir(exist_ok=False)
    except OSError as error:
        print("Cannot create exclusive output: " + _error(error), file=sys.stderr)
        return 3
    plan = {"schema_version": 1, "candidates": [str(path) for path in args.candidate],
            "with_damped_time": args.with_damped_time,
            "producer_binary": str(args.producer_binary) if args.producer_binary else None,
            "limits": {"file_bytes": MAX_FILE_BYTES, "collection_bytes": MAX_COLLECTION_BYTES,
                       "files": MAX_FILES},
            "input_policy": "regular files only; final symlinks refused",
            "exit_precedence": [3, 1, 2, 0], "phy002_acceptance": "incomplete"}
    manifest = {"schema_version": 1, "started_utc": _utc(), "status": "running",
                "plan_sha256": hashlib.sha256(_canonical(plan) + b"\n").hexdigest(),
                "phy002_acceptance": "incomplete", "cases": [], "operational_errors": [],
                "limitations": ["Four synthetic fixtures only; no hardware fidelity established.",
                    "Native envelope lacks physical configuration; identity consistency does not attest arbitrary bytes.",
                    "Candidate sign-change coverage is uncertified; full continuous-extrema acceptance stays incomplete.",
                    "Supplemental timing checks do not amend frozen contact acceptance."]}
    codes, total = [], 0
    references = {}
    try:
        _exclusive_json(output / "plan.json", plan)
        manifest["versions"] = _versions()
        manifest["source_sha256_before"] = _source_identity()
        _exclusive_json(output / "manifest.json", manifest)
        if args.producer_binary:
            try:
                manifest["declared_producer"] = _binary_identity(args.producer_binary)
            except OSError as error:
                codes.append(3)
                manifest["operational_errors"].append("producer identity: " + _error(error))
        for index, path in enumerate(args.candidate, 1):
            name = f"candidate-{index:04d}"
            record = {"index": index, "input_path": str(path), "started_utc": _utc(),
                      "status": "operational_failure", "exit_code": 3}
            manifest["cases"].append(record)
            try:
                limit = min(MAX_FILE_BYTES, MAX_COLLECTION_BYTES - total)
                raw, exceeded = _bounded_read(path, limit)
                total += len(raw)
                with (output / (name + ".raw")).open("xb") as handle:
                    handle.write(raw)
                record.update(raw_file=name + ".raw", retained_bytes=len(raw),
                              retained_sha256=hashlib.sha256(raw).hexdigest(),
                              raw_complete=not exceeded)
                if exceeded:
                    record.update(status="input_limit_exceeded", exit_code=1,
                                  error="retained prefix only; file or collection byte limit exceeded")
                else:
                    parsed = parse_candidate(raw)
                    record.update(fixture=parsed.fixture, raw_sha256=parsed.raw_sha256,
                                  settings=dict(parsed.settings), availability=parsed.availability)
                    reference = None
                    if parsed.availability == "available" and parsed.run["success"] is True:
                        if parsed.fixture not in references:
                            references[parsed.fixture] = reference_peaks(parsed.fixture, damped_time=args.with_damped_time)
                            _exclusive_json(output / ("reference-" + parsed.fixture + ".json"), references[parsed.fixture])
                        reference = references[parsed.fixture]
                    comparison = compare_candidate(parsed, reference)
                    _exclusive_json(output / (name + ".comparison.json"), comparison)
                    record.update(status={0: "pass", 1: "fail", 2: "incomplete"}[comparison["exit_code"]],
                                  exit_code=comparison["exit_code"], comparison_file=name + ".comparison.json")
            except CandidateFormatError as error:
                record.update(status="malformed_candidate", exit_code=1, error=_error(error))
            except (OSError, ArithmeticError, ValueError, TypeError, KeyError, RuntimeError) as error:
                record.update(status="operational_failure", exit_code=3, error=_error(error))
            record["ended_utc"] = _utc()
            codes.append(record["exit_code"])
            _exclusive_json(output / (name + ".record.json"), record)
            _replace_manifest(output / "manifest.json", manifest)
        manifest["source_sha256_after"] = _source_identity()
        manifest["source_unchanged"] = manifest["source_sha256_before"] == manifest["source_sha256_after"]
        if not manifest["source_unchanged"]:
            codes.append(3)
            manifest["operational_errors"].append("diagnostic source changed during this attempt")
    except (OSError, TypeError, ValueError, KeyError, RuntimeError) as error:
        codes.append(3)
        manifest["operational_errors"].append(_error(error))
    code = aggregate_exit(codes)
    manifest.update(status={0: "pass", 1: "fail", 2: "incomplete", 3: "operational_failure"}[code],
                    exit_code=code, ended_utc=_utc(), retained_candidate_bytes=total,
                    unattempted_inputs=[str(path) for path in args.candidate[len(manifest["cases"]):]])
    try:
        _replace_manifest(output / "manifest.json", manifest)
    except (OSError, TypeError, ValueError) as error:
        print("Could not retain final manifest: " + _error(error), file=sys.stderr)
        return 3
    print(json.dumps({"status": manifest["status"], "exit_code": code,
                      "cases": len(manifest["cases"]), "output": str(output),
                      "phy002_acceptance": "incomplete"}))
    return code


if __name__ == "__main__":
    raise SystemExit(main())
