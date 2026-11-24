#!/usr/bin/env python3
"""Retain and compare the frozen 26-run supported-contact extrema experiment.

``--plan`` needs only the standard library and does not read candidates or run
a solver. An experiment takes repeated ``--candidate`` native JSON files and a
new ``--output-dir``. Candidate executables are never run: the optional
``--producer-executable`` records a declared identity only. Exactly six fresh
independent R0/R1/R2 workers are scheduled, each with a parent-enforced 300 s
deadline; a wholly unusable input collection records six unexecuted workers.

Value, supplemental timing, independent refinement, and coverage remain
separate. Even a successful scoped diagnostic cannot certify continuous-time
coverage or complete PHY-002. The original four-fixture tools and the general
auditor's convergence/energy gates are unchanged and unevaluated here.
"""
from __future__ import annotations

import argparse
from dataclasses import asdict
from datetime import datetime, timezone
import hashlib
import importlib.metadata
import json
import math
import os
from pathlib import Path
import platform
import signal
import stat
import subprocess
import sys
import time
from typing import Any, Callable, Mapping, Sequence

from contact_supported_schema import (
    CandidateFormatError, EXPECTED_COUNT, EXPECTED_SETTINGS, MATRIX_ID,
    MATRIX_SETTINGS_SHA256, MAX_COLLECTION_BYTES, MAX_FILE_BYTES, MAX_FILES,
    PEAK_NAMES, ParsedCandidate, RUN_SETTING_FIELDS, SUPPORTED_FIXTURES,
    parse_candidate, validate_collection,
)
from validate_contact_reference import SCALE_BUDGETS, fixtures, refinement_slot_id


ROOT = Path(__file__).resolve().parents[1]
COVERAGE = "uncertified_independent_search"
LEVEL_NAMES = ("R0", "R1", "R2")
REFERENCE_WALL_SECONDS = 300.0
MAX_REFERENCE_JSON_BYTES = 128 * 1024 * 1024
MAX_IDENTITY_BYTES = 1024 * 1024 * 1024
BASE_ATOL = (1e-16, 1e-16, 1e-13, 1e-13, 1e-16, 1e-13,
             1e-16, 1e-16, 1e-16, 1e-16, 1e-16, 1e-16, 1e-16)
SOURCE_PATHS = (
    "tools/validate_contact_extrema.py", "tools/contact_extrema_reference.py",
    "tools/contact_supported_schema.py", "tools/contact_peak_schema.py",
    "tools/validate_contact_reference.py", "tools/run_contact_sweep.py",
    "src/contact_reference.cpp", "include/rhodes/contact_reference.hpp",
    "apps/contact_reference.cpp", "CMakeLists.txt",
    "docs/specs/CONTACT_EXTREMA_AUDIT_DESIGN_20261123.md",
    "docs/specs/CONTACT_ACCEPTANCE_20261123.md",
    "docs/specs/CONTACT_REFINEMENT_EXTENSION_20261123.md",
    "docs/validation/research-gates-20261124.md",
    "docs/validation/contact-split-depth1-20261123.json",
)
READY = {"resolved_under_declared_refinement", "analytic_disabled_identity"}


def _utc() -> str:
    return datetime.now(timezone.utc).isoformat()


def _canonical(value: Any) -> bytes:
    return json.dumps(value, sort_keys=True, separators=(",", ":"),
                      ensure_ascii=True, allow_nan=False).encode("utf-8")


def _sha(raw: bytes) -> str:
    return hashlib.sha256(raw).hexdigest()


def _error(exc: BaseException) -> str:
    return type(exc).__name__ + ": " + str(exc)


def _absolute(path: str | Path) -> Path:
    # resolve() would silently follow a symlink before the safety checks.
    return Path(os.path.abspath(os.fspath(path)))


def _check_parents(path: Path) -> None:
    for parent in reversed(path.parents):
        metadata = parent.lstat()
        if not stat.S_ISDIR(metadata.st_mode) or stat.S_ISLNK(metadata.st_mode):
            raise OSError("path ancestors must be real directories: " + str(parent))


def _signature(metadata: os.stat_result) -> tuple[int, ...]:
    return (metadata.st_dev, metadata.st_ino, metadata.st_mode, metadata.st_size,
            metadata.st_mtime_ns, metadata.st_ctime_ns)


def _regular_reader(path: Path):
    """Never block opening a FIFO; bind the regular-file check to its handle."""
    path = _absolute(path)
    _check_parents(path)
    prior = path.lstat()
    if not stat.S_ISREG(prior.st_mode):
        raise OSError("input must be a regular file; symlinks and special files are refused")
    flags = (os.O_RDONLY | getattr(os, "O_BINARY", 0) | getattr(os, "O_NOFOLLOW", 0)
             | getattr(os, "O_NONBLOCK", 0) | getattr(os, "O_CLOEXEC", 0))
    descriptor = os.open(path, flags)
    try:
        actual, current = os.fstat(descriptor), path.lstat()
        if not stat.S_ISREG(actual.st_mode) or not stat.S_ISREG(current.st_mode) or \
                _signature(prior) != _signature(actual) or _signature(current) != _signature(actual):
            raise OSError("input changed identity, type, or metadata while opening")
        return os.fdopen(descriptor, "rb"), actual
    except BaseException:
        os.close(descriptor)
        raise


def bounded_read(path: Path, limit: int) -> tuple[bytes, dict[str, Any]]:
    """Read at most limit bytes, preserving prefixes and every detected error.

    There is no extra sentinel byte beyond either input budget. A regular-file
    size and before/after handle identity establish whether the retained bytes
    cover the entire stable file. Special files are rejected before any read.
    """
    if type(limit) is not int or limit < 0:
        raise ValueError("read limit must be a nonnegative integer")
    record: dict[str, Any] = {"complete": False, "errors": [], "read_limit_bytes": limit}
    blocks: list[bytes] = []
    try:
        handle, before = _regular_reader(path)
        record["source_bytes_before"] = before.st_size
        record["source_signature_before"] = list(_signature(before))
        remaining = limit
        with handle:
            try:
                while remaining:
                    block = handle.read(min(65536, remaining))
                    if not block:
                        break
                    blocks.append(block)
                    remaining -= len(block)
            except OSError as exc:
                record["errors"].append("read failed: " + _error(exc))
            after = os.fstat(handle.fileno())
        record["source_signature_after"] = list(_signature(after))
        record["source_bytes_after"] = after.st_size
        if before.st_size > limit or after.st_size > limit:
            record["errors"].append("file exceeds the available per-file or collection byte allowance")
        if _signature(before) != _signature(after):
            record["errors"].append("source changed during bounded read")
        try:
            if _signature(_absolute(path).lstat()) != _signature(after):
                record["errors"].append("source path changed identity during bounded read")
        except OSError as exc:
            record["errors"].append("source path inspection failed: " + _error(exc))
        if sum(map(len, blocks)) != after.st_size:
            record["errors"].append("retained bytes are only a prefix or an incomplete read")
        record["complete"] = not record["errors"]
    except OSError as exc:
        record["errors"].append(_error(exc))
    raw = b"".join(blocks)
    record.update(read_bytes=len(raw), retained_sha256=_sha(raw))
    return raw, record


def _write_new(path: Path, raw: bytes) -> None:
    _check_parents(path)
    flags = (os.O_WRONLY | os.O_CREAT | os.O_EXCL | getattr(os, "O_BINARY", 0)
             | getattr(os, "O_NOFOLLOW", 0) | getattr(os, "O_CLOEXEC", 0))
    descriptor = os.open(path, flags, 0o600)
    with os.fdopen(descriptor, "wb") as stream:
        if stream.write(raw) != len(raw):
            raise OSError("short write while preserving evidence")
        stream.flush()
        os.fsync(stream.fileno())


def _write_json(path: Path, value: Any) -> None:
    _write_new(path, _canonical(value) + b"\n")


def _identity(path: Path) -> dict[str, Any]:
    result: dict[str, Any] = {"path": str(_absolute(path))}
    try:
        handle, before = _regular_reader(path)
        digest, count = hashlib.sha256(), 0
        with handle:
            if before.st_size > MAX_IDENTITY_BYTES:
                raise OSError("identity file exceeds the declared 1 GiB hashing limit")
            while count < before.st_size:
                block = handle.read(min(65536, before.st_size - count))
                if not block:
                    break
                digest.update(block)
                count += len(block)
            after = os.fstat(handle.fileno())
        if count != before.st_size or _signature(before) != _signature(after) or \
                _signature(_absolute(path).lstat()) != _signature(after):
            raise OSError("file changed during identity hashing")
        result.update(status="recorded", bytes=count, sha256=digest.hexdigest())
    except OSError as exc:
        result.update(status="unavailable", error=_error(exc))
    return result


def _source_identity() -> dict[str, Any]:
    return {name: _identity(ROOT / name) for name in SOURCE_PATHS}


def _git_identity() -> dict[str, Any]:
    result: dict[str, Any] = {}
    for key, arguments in (("head", ["rev-parse", "HEAD"]),
                           ("working_tree", ["status", "--porcelain", "--untracked-files=no"])):
        try:
            run = subprocess.run(["git", "-C", str(ROOT), *arguments], stdin=subprocess.DEVNULL,
                                 capture_output=True, timeout=5, check=False)
            result[key] = {"exit_code": run.returncode,
                           "stdout": run.stdout.decode("utf-8", "backslashreplace").rstrip(),
                           "stderr": run.stderr.decode("utf-8", "backslashreplace").rstrip()}
        except (OSError, subprocess.SubprocessError) as exc:
            result[key] = {"error": _error(exc)}
    return result


def _versions() -> dict[str, Any]:
    result = {"python": sys.version, "python_executable": sys.executable,
              "platform": platform.platform(), "numpy": "not_available", "scipy": "not_available"}
    for name in ("numpy", "scipy"):
        try:
            result[name] = importlib.metadata.version(name)
        except importlib.metadata.PackageNotFoundError:
            pass
    return result


def _fixture_definitions() -> dict[str, Any]:
    definitions = {}
    for name in sorted(SUPPORTED_FIXTURES):
        fixture = fixtures()[name]
        value = {**asdict(fixture), "evaluated_stiffness_n_per_m": list(fixture.stiffness()),
                 "evaluated_damping_ns_per_m": list(fixture.damping())}
        definitions[name] = {"definition": value, "sha256": _sha(_canonical(value))}
    return definitions


def _reference_settings(level: str) -> dict[str, Any]:
    step, relative, multiplier = {
        "R0": (5e-6, 1e-10, 10.0), "R1": (2.5e-6, 1e-12, 1.0),
        "R2": (1.25e-6, 1e-13, 0.1),
    }[level]
    return {"maximum_step_s": step, "relative_tolerance": relative,
            "absolute_vector_multiplier": multiplier,
            "absolute_tolerance_vector": [value * multiplier for value in BASE_ATOL]}


def plan() -> dict[str, Any]:
    """The complete fixed experiment declaration, without numerical imports."""
    slots = [{"slot": refinement_slot_id(name, settings), "fixture": name,
              "settings": dict(zip(RUN_SETTING_FIELDS, settings))}
             for name in sorted(SUPPORTED_FIXTURES) for settings in EXPECTED_SETTINGS]
    definitions = _fixture_definitions()
    model = {"name": "synthetic-contact-p1", "native_schema_version": 1,
             "fixture_sha256": {name: row["sha256"] for name, row in definitions.items()},
             "synthetic_parameters": True, "physical_measurements": False}
    return {
        "schema_version": 1, "experiment": "supported-contact-extrema-depth-one",
        "matrix_id": MATRIX_ID, "settings_sha256": MATRIX_SETTINGS_SHA256,
        "expected_candidate_count": EXPECTED_COUNT, "slots": slots,
        "model": model, "model_sha256": _sha(_canonical(model)), "fixtures": definitions,
        "reference_count": 6,
        "references": [{"fixture": name, "level": level,
                        "settings": _reference_settings(level),
                        "settings_sha256": _sha(_canonical(_reference_settings(level)))}
                       for name in sorted(SUPPORTED_FIXTURES) for level in LEVEL_NAMES],
        "reference_limits": {"accepted_steps": 200000, "rhs_evaluations": 5000000,
                             "search_evaluations": 1000000, "candidates": 100000,
                             "wall_seconds": REFERENCE_WALL_SECONDS,
                             "parent_wall_seconds": REFERENCE_WALL_SECONDS},
        "input_limits": {"file_bytes": MAX_FILE_BYTES, "collection_bytes": MAX_COLLECTION_BYTES,
                         "files": MAX_FILES},
        "input_policy": "regular files only; refuse symlinks; preserve complete raw bytes or an explicitly identified bounded prefix",
        "output_policy": "new exclusive directory and exclusive artifacts; preserve worker checkpoints and partial failures",
        "comparison_budgets": {"compression_m": {"absolute": 1e-8, "relative": 1e-4},
                               "force_n": {"absolute": 1e-4, "relative": 2e-4},
                               "supplemental_time_s": {"absolute": 1e-7, "relative": 1e-4},
                               "both_reference_refinement_fractions": 0.1,
                               "coordinate_scaling": dict(SCALE_BUDGETS)},
        "coordinate_candidate_pairs": 13, "coordinate_reference_pairs": 3,
        "root_width_s": 1e-12, "root_iteration_limit": 80,
        "coverage": COVERAGE, "phy002_acceptance": "incomplete", "PHY002_acceptance": "incomplete",
        "unevaluated_gates": {
            "general_auditor_convergence_and_energy": "not_evaluated",
            "original_0_8_decrease_rule": "unchanged; not evaluated or waived by this tool",
            "current_scope": "docs/validation/research-gates-20261124.md",
            "historical_split_report": "docs/validation/contact-split-depth1-20261123.json"},
        "exit_codes": {"0": "scoped numerical diagnostics passed; coverage and PHY-002 remain incomplete",
                       "1": "input, numerical, or provenance failure", "2": "incomplete scoped diagnostics",
                       "3": "operational artifact failure"},
    }


def _kill_worker(process: subprocess.Popen) -> None:
    if process.poll() is not None:
        return
    try:
        if os.name == "posix":
            os.killpg(process.pid, signal.SIGKILL)
        else:
            process.kill()
    except ProcessLookupError:
        pass
    process.wait(timeout=5)


def _reference_source_identity() -> dict[str, dict[str, Any]]:
    return {name: _identity(ROOT / "tools" / name) for name in
            ("contact_extrema_reference.py", "validate_contact_reference.py")}


def _finite_number(value: Any, *, lower: float = 0.0, upper: float = math.inf) -> bool:
    return type(value) in (int, float) and math.isfinite(value) and lower <= value <= upper


def _validate_worker_reference(reference: Any, fixture: str, level: str,
                               expected_sources: Mapping[str, str]) -> dict[str, Any]:
    """Validate the private generated-worker protocol without importing NumPy.

    This is structural/provenance validation, not numerical acceptance. Search
    pathologies remain in the unresolved ledger; valid capped records remain
    inspectable even though they cannot complete an execution.
    """
    from contact_extrema_reference import (
        CANDIDATE_ORIGINS, DEFAULT_LIMITS, SCHEMA_VERSION, STATE_COORDINATES,
        _limits, _raw_layout, inspect_retained_raw,
    )
    if type(reference) is not dict or type(reference.get("schema_version")) is not int or \
            reference["schema_version"] != SCHEMA_VERSION or \
            reference.get("reference_kind") != "independent_supported_contact_extrema":
        raise ValueError("worker reference schema/version/kind is invalid")
    if reference.get("fixture") != fixture or reference.get("level") != level or \
            reference.get("coverage") != COVERAGE or \
            _canonical(reference.get("settings")) != _canonical(_reference_settings(level)):
        raise ValueError("worker reference identity, settings, or coverage differs from the declared plan")
    frozen = _fixture_definitions()[fixture]
    if reference.get("fixture_sha256") != frozen["sha256"] or \
            _sha(_canonical(reference.get("fixture_definition"))) != frozen["sha256"]:
        raise ValueError("worker fixture definition differs from the frozen independent fixture")
    if set(expected_sources) != {"contact_extrema_reference.py", "validate_contact_reference.py"} or \
            reference.get("source_sha256") != dict(expected_sources):
        raise ValueError("worker source hash map differs from the source identities recorded before execution")
    limits_value, counts = reference.get("limits"), reference.get("counts")
    if type(limits_value) is not dict or set(limits_value) != set(asdict(DEFAULT_LIMITS)):
        raise ValueError("worker reference omits its complete declared limits")
    limits = _limits(limits_value)
    count_names = ("accepted_steps", "rhs_evaluations", "search_evaluations", "candidates")
    if type(counts) is not dict or any(type(counts.get(name)) is not int or
            not 0 <= counts[name] <= getattr(limits, name) for name in count_names) or \
            not _finite_number(counts.get("elapsed_s")):
        raise ValueError("worker reference counters are invalid or exceed their declared limits")
    duration = fixtures()[fixture].duration
    complete = reference.get("complete_duration")
    if type(complete) is not bool or reference.get("status") != (
            "completed" if complete else "inconclusive_reference") or \
            reference.get("value_status") != ("requires_refinement" if complete else "inconclusive_reference") or \
            not _finite_number(reference.get("duration_s")) or reference["duration_s"] != duration or \
            "final_time_s" not in reference or \
            (reference.get("final_time_s") is not None and
             not _finite_number(reference["final_time_s"], upper=duration)) or \
            (complete and (reference.get("final_time_s") != duration or reference.get("failure") is not None)):
        raise ValueError("worker reference completion/duration/failure structure is inconsistent")
    if "failure" not in reference or (reference["failure"] is not None and type(reference["failure"]) is not dict):
        raise ValueError("worker reference failure ledger is missing or invalid")
    raw = reference.get("raw")
    if type(raw) is not dict or "raw_arrays" in reference or \
            raw.get("basis") != "ascending normalized u=(t-t0)/(t1-t0)" or \
            raw.get("state_coordinates") != list(STATE_COORDINATES) or \
            raw.get("candidate_origin_bits") != dict(CANDIDATE_ORIGINS):
        raise ValueError("worker retained-array representation is invalid")
    layout, _ = _raw_layout(reference)
    if (layout["mesh_t_s"][0][0] == 0) != (reference["final_time_s"] is None):
        raise ValueError("worker final time is inconsistent with its empty/nonempty mesh")
    dense_count, candidate_count = layout["dense_t0_s"][0][0], layout["candidate_time_s"][0][0]
    peaks = reference.get("peaks")
    if type(peaks) is not dict or set(peaks) != set(PEAK_NAMES):
        raise ValueError("worker reference must contain all four required peak records")
    for name in PEAK_NAMES:
        peak = peaks[name]
        disabled = name.startswith("damper_")
        field, unit = ("value_m", "m") if name.endswith("compression") else ("value_n", "N")
        if type(peak) is not dict or not _finite_number(peak.get(field)) or \
                peak.get("unit") != unit or peak.get("coverage") != COVERAGE or \
                peak.get("kind") not in ({"disabled"} if disabled else {"isolated_candidate", "tied_candidates"}) or \
                peak.get("classification") not in ({"disabled"} if disabled else
                                                       {"endpoint", "branch_boundary", "stationary"}) or \
                peak.get("value_status") != ("analytic_disabled_identity" if disabled else
                                              "requires_refinement" if complete else "inconclusive_reference") or \
                peak.get("time_status") != ("disabled" if disabled else
                                             "numerical_candidates" if complete else "inconclusive_reference"):
            raise ValueError("worker peak value/classification structure is invalid: " + name)
        intervals = peak.get("maximizer_intervals_s")
        maxima = peak.get("maximizing_candidates")
        if type(intervals) is not list or (not intervals and (complete or disabled)) or \
                (intervals and (not _intervals(intervals) or any(item[1] > duration for item in intervals))) or \
                type(maxima) is not list or (complete and not disabled and not maxima):
            raise ValueError("worker peak intervals/candidate registry is invalid: " + name)
        if disabled and (peak[field] != 0.0 or intervals != [[0.0, duration]] or maxima):
            raise ValueError("worker disabled-damper identity is inconsistent: " + name)
        for candidate in maxima:
            if type(candidate) is not dict or type(candidate.get("candidate_id")) is not int or \
                    not 0 <= candidate["candidate_id"] < candidate_count or \
                    not _finite_number(candidate.get("time_s"), upper=duration) or \
                    type(candidate.get("segment_id")) is not int or \
                    not -1 <= candidate["segment_id"] < dense_count or \
                    type(candidate.get("origin_bits")) is not int or \
                    not 1 <= candidate["origin_bits"] <= sum(CANDIDATE_ORIGINS.values()) or \
                    not _finite_number(candidate.get("gap_m"), lower=-math.inf) or \
                    type(candidate.get("details")) is not list or \
                    any(type(detail) is not dict for detail in candidate["details"]):
                raise ValueError("worker maximizing candidate structure is invalid: " + name)
    for name in ("root_candidates", "resolved_peak_clusters", "branches", "unresolved"):
        if type(reference.get(name)) is not list or any(type(row) is not dict for row in reference[name]):
            raise ValueError("worker reference diagnostic list is missing or invalid: " + name)
    for cluster in reference["resolved_peak_clusters"]:
        if not _finite_number(cluster.get("time_s"), upper=duration) or \
                cluster.get("crossing") != "positive_to_negative":
            raise ValueError("worker resolved peak cluster structure is invalid")
    for branch in reference["branches"]:
        if branch.get("branch") not in ("separated", "positive_compression"):
            raise ValueError("worker branch classification is invalid")
    statistics = reference.get("search_statistics")
    if type(statistics) is not dict or not _finite_number(
            statistics.get("maximum_physical_dense_derivative_discrepancy_m_per_s")):
        raise ValueError("worker derivative diagnostic is missing or nonfinite")
    return inspect_retained_raw(reference)


def run_reference_worker(fixture: str, level: str, directory: Path, *,
                         timeout_seconds: float = REFERENCE_WALL_SECONDS,
                         worker_path: Path | None = None) -> tuple[dict[str, Any], dict | None]:
    """Own one subprocess deadline, including import/solver stalls.

    The optional private testing controls can only tighten the wall deadline;
    the CLI always uses the declared reference script and 300 s limit. No
    producer path is accepted by this function or by the numerical backend.
    """
    if fixture not in SUPPORTED_FIXTURES or level not in LEVEL_NAMES:
        raise ValueError("worker must name one declared fixture and reference level")
    if type(timeout_seconds) not in (int, float) or not math.isfinite(timeout_seconds) or \
            not 0 < timeout_seconds <= REFERENCE_WALL_SECONDS:
        raise ValueError("parent deadline may only tighten the declared 300 s limit")
    directory = _absolute(directory)
    _check_parents(directory)
    directory.mkdir(mode=0o700)
    output, raw = directory / "reference.json", directory / "reference.npz"
    worker = _absolute(worker_path or ROOT / "tools/contact_extrema_reference.py")
    command = [sys.executable, str(worker), "--fixture", fixture, "--level", level,
               "--output", str(output), "--raw", str(raw)]
    record: dict[str, Any] = {
        "fixture": fixture, "level": level, "command": command, "started_utc": _utc(),
        "parent_wall_limit_s": timeout_seconds, "worker_executed": False,
        "timed_out": False, "exit_code": None, "status": "inconclusive_reference", "errors": [],
        "expected_output_path": str(output), "expected_raw_path": str(raw),
        "worker_identity": _identity(worker),
        "reference_sources_before": _reference_source_identity(),
        "coverage": COVERAGE, "phy002_acceptance": "incomplete"}
    started, process = time.monotonic(), None
    try:
        if record["worker_identity"]["status"] != "recorded":
            raise OSError("independent worker must be a stable regular source file: " +
                          record["worker_identity"]["error"])
        with (directory / "stdout.raw").open("xb") as stdout, \
                (directory / "stderr.raw").open("xb") as stderr:
            environment = os.environ.copy()
            environment["PYTHONDONTWRITEBYTECODE"] = "1"
            process = subprocess.Popen(command, stdin=subprocess.DEVNULL, stdout=stdout, stderr=stderr,
                                       cwd=ROOT, env=environment, start_new_session=os.name == "posix")
            record["worker_executed"] = True
            while process.poll() is None:
                remaining = timeout_seconds - (time.monotonic() - started)
                if remaining <= 0:
                    record["timed_out"] = True
                    _kill_worker(process)
                    record["errors"].append("parent wall deadline exhausted; worker terminated; partial artifacts retained")
                    break
                try:
                    process.wait(timeout=min(remaining, 1.0))
                except subprocess.TimeoutExpired:
                    continue
            record["exit_code"] = process.returncode
    except (OSError, subprocess.SubprocessError) as exc:
        record["errors"].append("worker execution: " + _error(exc))
        if process is not None:
            _kill_worker(process)
            record["exit_code"] = process.returncode
    except BaseException:
        if process is not None:
            _kill_worker(process)
        raise
    record.update(elapsed_s=time.monotonic() - started, finished_utc=_utc())
    record["reference_sources_after"] = _reference_source_identity()
    reference = None
    if output.exists() or output.is_symlink():
        payload, reading = bounded_read(output, MAX_REFERENCE_JSON_BYTES)
        record["result_read"] = reading
        if reading["complete"]:
            try:
                reference = json.loads(payload)
                sources = record["reference_sources_before"]
                if any(row.get("status") != "recorded" or
                       record["reference_sources_after"][name].get("status") != "recorded" or
                       row.get("sha256") != record["reference_sources_after"][name].get("sha256")
                       for name, row in sources.items()):
                    raise ValueError("worker reference sources changed or are unavailable")
                if type(reference) is not dict or type(reference.get("raw")) is not dict or \
                        reference["raw"].get("path") != str(raw):
                    raise ValueError("worker raw reference path differs from its assigned artifact")
                record["raw_validation"] = _validate_worker_reference(
                    reference, fixture, level, {name: row["sha256"] for name, row in sources.items()})
                if record["exit_code"] == 0 and not record["timed_out"] and not record["errors"] and \
                        reference.get("status") == "completed" and reference.get("complete_duration") is True and \
                        reference.get("final_time_s") == fixtures()[fixture].duration:
                    record["status"] = "completed"
                else:
                    record["errors"].append("worker did not produce a successful full-duration reference")
            except (OSError, ValueError, TypeError, KeyError, OverflowError, RuntimeError) as exc:
                record["errors"].append("worker result: " + _error(exc))
                reference = None
        else:
            record["errors"].extend("worker result: " + message for message in reading["errors"])
    else:
        record["errors"].append("worker result JSON is absent")
    record["artifacts"] = [_identity(path) for path in sorted(directory.iterdir()) if not path.is_dir()]
    record["artifact_identity_changes"] = _retained_digest_changes(
        _expected_retained_digests([], [record]), record["artifacts"])
    if record["artifact_identity_changes"]:
        record["status"] = "inconclusive_reference"
        record["errors"].append("worker retained artifacts changed after result validation")
    _write_json(directory / "execution.json", record)
    return record, reference


def value_budget(name: str, value: float) -> float:
    return 1e-8 + 1e-4 * abs(value) if name.endswith("compression") else 1e-4 + 2e-4 * abs(value)


def _status(statuses: Sequence[str]) -> str:
    if "fail" in statuses:
        return "fail"
    return "pass" if statuses and all(value == "pass" for value in statuses) else "inconclusive"


def _global_value(name: str, observed: float, refined: Mapping[str, Any] | None) -> dict[str, Any]:
    target = refined.get("values", {}).get("R2") if refined else None
    result: dict[str, Any] = {"reference_value": target, "reference_level": "R2",
                              "reference_status": refined.get("value_status") if refined else None,
                              "status": "inconclusive_reference"}
    if type(target) in (int, float) and math.isfinite(target):
        error, budget = abs(observed - target), value_budget(name, target)
        result.update(absolute_error=error, budget=budget, budget_fraction=error / budget)
        if refined["value_status"] in READY:
            result["status"] = "pass" if error <= budget else "fail"
    return result


def _intervals(value: Any) -> bool:
    return isinstance(value, (tuple, list)) and bool(value) and all(
        isinstance(item, (tuple, list)) and len(item) == 2 and
        all(type(number) in (int, float) and math.isfinite(number) for number in item) and
        0 <= item[0] <= item[1] for item in value)


def _time_check(observed: float, peak: Mapping[str, Any] | None,
                refined: Mapping[str, Any] | None) -> dict[str, Any]:
    intervals = refined.get("maximizer_intervals_s") if refined else None
    result: dict[str, Any] = {"status": "inconclusive_reference", "observed_time_s": observed,
                              "reference_intervals_s": intervals,
                              "reference_time_status": refined.get("time_status") if refined else None,
                              "candidate_bracket_used_as_oracle": False}
    if not refined or refined.get("time_status") not in READY | {"disabled"} or not _intervals(intervals):
        return result
    distances = [max(lo - observed, observed - hi, 0.0) for lo, hi in intervals]
    result["distance_to_maximizers_s"] = min(distances)
    if peak and peak.get("kind") in {"disabled", "plateau"}:
        result.update(status="pass" if min(distances) == 0 else "fail",
                      method="membership in the union of all maximizing intervals")
        return result
    checks = []
    for interval, distance in zip(intervals, distances):
        lo, hi = interval
        budget = 1e-7 + 1e-4 * max(abs(lo), abs(hi))
        checks.append({"interval_s": interval, "distance_s": distance,
                       "budget_s": budget, "within_budget": distance <= budget})
    matches = [index for index, row in enumerate(checks) if row["within_budget"]]
    result.update(interval_comparisons=checks, matching_intervals=matches,
                  method="distance to every independently retained maximizing interval")
    if not matches:
        result["status"] = "fail"
    elif len(matches) == 1:
        result["status"] = "pass"
    else:
        result.update(status="inconclusive_pairing",
                      reason="multiple retained maximizing intervals match; no earliest tie is selected")
    return result


def _sample_lower_bound(name: str, observed: float, parsed: ParsedCandidate) -> dict[str, Any]:
    contact, quantity = name.split("_")
    field = name + ("_m" if quantity == "compression" else "_n")
    values = [sample[contact + "_force_n"] if quantity == "force" else
              0.0 if contact == "damper" else max(0.0, sample[contact + "_gap_m"])
              for sample in parsed.run["samples"]]
    exported = max(values)
    diagnostic = parsed.run["diagnostics"]["sampled_maxima"][field]
    witness = max(exported, diagnostic)
    deficit, budget = max(0.0, witness - observed), value_budget(name, witness)
    negative_force = quantity == "force" and any(value < 0 for value in values)
    return {"status": "fail" if deficit > budget or negative_force else "pass",
            "maximum_exported_sample": exported, "maximum_native_sampled_diagnostic": diagnostic,
            "witness_value": witness, "peak_deficit": deficit, "budget": budget,
            "negative_exported_force": negative_force,
            "coverage_implication": "lower-bound consistency only; no continuous-domain upper bound"}


def _reference_evaluation_token(reference: Mapping[str, Any]) -> tuple[Any, ...]:
    """Bind cached values to the declared reference and current file identity.

    Cache hits need only a regular-file metadata check. Misses still run the
    evaluator's complete snapshot/hash/schema validation, and final experiment
    provenance rehashes every originally retained artifact. This avoids
    repeatedly decompressing a large NPZ for identical point evaluations.
    """
    relevant = ("fixture", "level", "fixture_sha256", "source_sha256", "settings", "raw",
                "raw_arrays", "limits", "counts", "status", "complete_duration", "final_time_s")
    fingerprint = _sha(_canonical({key: reference[key] for key in relevant if key in reference}))
    raw, signature = reference.get("raw"), None
    if isinstance(raw, Mapping) and "path" in raw:
        handle, metadata = _regular_reader(Path(raw["path"]))
        with handle:
            signature = _signature(metadata)
    return fingerprint, signature


def _at_candidate_time(name: str, value: float, time_s: float, references: Mapping[str, dict],
                       evaluator: Callable[..., dict], cache: dict) -> dict[str, Any]:
    evaluations: dict[str, Any] = {}
    errors = []
    field = name + ("_m" if name.endswith("compression") else "_n")
    for level in LEVEL_NAMES:
        reference = references.get(level)
        if reference is None:
            errors.append(level + ": independent trajectory is absent")
            continue
        evaluations[level] = {}
        try:
            identity = _reference_evaluation_token(reference)
        except Exception as exc:
            error = _error(exc)
            evaluations[level] = {side: {"error": error} for side in ("left", "right")}
            errors.append(level + ": " + error)
            continue
        for side in ("left", "right"):
            key = (identity, level, time_s, side)
            if key not in cache:
                try:
                    evaluation = evaluator(reference, time_s, side=side)
                    required = ("hammer_compression_m", "hammer_force_n", "damper_compression_m", "damper_force_n")
                    if not isinstance(evaluation, dict) or any(
                            type(evaluation.get(item)) not in (int, float) or
                            not math.isfinite(evaluation[item]) for item in required):
                        raise ValueError("independent evaluation omits a finite physical peak quantity")
                    if _reference_evaluation_token(reference) != identity:
                        raise ValueError("independent reference identity changed during point evaluation")
                    cache[key] = {"evaluation": evaluation}
                except Exception as exc:
                    cache[key] = {"error": _error(exc)}
            evaluated = cache[key]
            evaluations[level][side] = evaluated
            if "error" in evaluated:
                errors.append(level + "/" + side + ": " + evaluated["error"])
    sides = {}
    for side in ("left", "right"):
        complete = all(level in evaluations and "evaluation" in evaluations[level][side]
                       and references[level].get("status") == "completed"
                       and references[level].get("complete_duration") is True for level in LEVEL_NAMES)
        if not complete:
            sides[side] = {"status": "inconclusive_reference"}
            continue
        values = [evaluations[level][side]["evaluation"][field] for level in LEVEL_NAMES]
        budget = value_budget(name, values[2])
        differences = [abs(b - a) for a, b in zip(values, values[1:])]
        resolved = all(difference <= 0.1 * budget for difference in differences)
        error = abs(value - values[2])
        sides[side] = {"status": ("pass" if error <= budget else "fail") if resolved else "inconclusive_reference",
                       "values": dict(zip(LEVEL_NAMES, values)),
                       "successive_absolute_differences": differences,
                       "refinement_limit": 0.1 * budget, "pointwise_refinement_resolved": resolved,
                       "absolute_error": error, "budget": budget, "budget_fraction": error / budget}
    return {"status": _status([row["status"] for row in sides.values()]),
            "time_s": time_s, "sides": sides, "evaluations": evaluations, "errors": errors,
            "interpretation": "Independent trajectory evaluated at the candidate time; retained branch and derivative diagnostics do not certify global coverage."}


def compare_candidate(parsed: ParsedCandidate, references: Mapping[str, dict],
                      refinement: Mapping[str, Any] | None, *,
                      evaluator: Callable[..., dict] | None = None, cache: dict | None = None) -> dict[str, Any]:
    """Keep a known value failure even when reference coverage is uncertified."""
    result: dict[str, Any] = {"fixture": parsed.fixture, "raw_sha256": parsed.raw_sha256,
                              "settings": dict(parsed.settings), "availability": parsed.availability,
                              "slot": refinement_slot_id(parsed.fixture, tuple(value for _, value in parsed.settings)),
                              "validity": "pass", "status": "incomplete", "peaks": {},
                              "value_status": "not_evaluated", "time_status": "not_evaluated",
                              "refinement_status": refinement.get("status") if refinement else "inconclusive_reference",
                              "coverage": COVERAGE, "phy002_acceptance": "incomplete", "PHY002_acceptance": "incomplete"}
    if parsed.run["success"] is not True or parsed.run["failure_code"] != 0:
        result.update(validity="producer_failure", status="fail", failure_code=parsed.run["failure_code"])
        return result
    if parsed.peaks is None or parsed.availability != "available":
        result.update(validity="missing_peak_diagnostics")
        return result
    if evaluator is None:
        from contact_extrema_reference import evaluate_reference
        evaluator = evaluate_reference
    cache = {} if cache is None else cache
    for name in PEAK_NAMES:
        candidate = parsed.peaks[name]
        value = candidate["value_m" if name.endswith("compression") else "value_n"]
        refined = refinement.get("peaks", {}).get(name) if refinement else None
        reference_peak = references.get("R2", {}).get("peaks", {}).get(name)
        global_value = _global_value(name, value, refined)
        timing = _time_check(candidate["time_s"], reference_peak, refined)
        sampled = _sample_lower_bound(name, value, parsed)
        local = _at_candidate_time(name, value, candidate["time_s"], references, evaluator, cache)
        status = _status([global_value["status"], sampled["status"], local["status"]])
        result["peaks"][name] = {
            "candidate": dict(candidate), "value_status": status, "time_status": timing["status"],
            "reference_refinement_status": refined.get("value_status") if refined else "inconclusive_reference",
            "global_value_comparison": global_value, "time_comparison": timing,
            "sample_lower_bound": sampled, "reference_at_candidate_time": local,
            "coverage": {"candidate": parsed.peaks["coverage"], "reference": COVERAGE,
                         "continuous_extrema": "incomplete"}}
    result["value_status"] = _status([row["value_status"] for row in result["peaks"].values()])
    result["time_status"] = _status([row["time_status"] for row in result["peaks"].values()])
    result["diagnostic_status"] = _status([result["value_status"], result["time_status"]])
    result["status"] = "fail" if result["diagnostic_status"] == "fail" else "incomplete"
    return result


def _scaling_value(name: str, base: float, scaled: float) -> dict[str, Any]:
    absolute, relative = SCALE_BUDGETS["position" if name.endswith("compression") else "force"]
    budget, error = absolute + relative * abs(base), abs(scaled - base)
    return {"status": "pass" if error <= budget else "fail", "unscaled_value": base,
            "scaled_value": scaled, "absolute_error": error, "budget": budget,
            "budget_fraction": error / budget}


def coordinate_pairs(candidates: Sequence[ParsedCandidate], comparisons: Sequence[dict],
                     refinements: Mapping[str, dict]) -> dict[str, Any]:
    groups: dict[tuple[str, tuple], list[tuple[ParsedCandidate, dict]]] = {}
    for parsed, comparison in zip(candidates, comparisons):
        groups.setdefault((parsed.fixture, tuple(value for _, value in parsed.settings)), []).append((parsed, comparison))
    rows = []
    for settings in EXPECTED_SETTINGS:
        row: dict[str, Any] = {"settings": dict(zip(RUN_SETTING_FIELDS, settings)), "peaks": {},
                               "value_status": "inconclusive", "time_status": "inconclusive",
                               "status": "incomplete", "errors": []}
        pairs = [groups.get((name, settings), []) for name in ("supported_impact", "supported_impact_scaled")]
        if any(len(pair) != 1 for pair in pairs):
            row["errors"].append("coordinate pair requires exactly one candidate in each fixture slot")
        elif any(pair[0][0].run["success"] is not True or pair[0][0].peaks is None for pair in pairs):
            row["errors"].append("coordinate pair contains a failed producer or unavailable peak diagnostics")
        else:
            (base, base_comparison), (scaled, scaled_comparison) = (pair[0] for pair in pairs)
            row["raw_sha256"] = [base.raw_sha256, scaled.raw_sha256]
            for name in PEAK_NAMES:
                field = "value_m" if name.endswith("compression") else "value_n"
                a, b = base.peaks[name], scaled.peaks[name]
                values = _scaling_value(name, a[field], b[field])
                absolute, relative = SCALE_BUDGETS["time"]
                budget, error = absolute + relative * abs(a["time_s"]), abs(b["time_s"] - a["time_s"])
                timing = {"status": "inconclusive_pairing", "absolute_error_s": error,
                          "budget_s": budget, "unscaled_time_s": a["time_s"], "scaled_time_s": b["time_s"]}
                peak_rows = [comparison.get("peaks", {}).get(name, {})
                             for comparison in (base_comparison, scaled_comparison)]
                isolated = all(len(refinements.get(fixture, {}).get("peaks", {}).get(name, {}).get(
                    "maximizer_intervals_s", [])) == 1 for fixture in (base.fixture, scaled.fixture))
                if name.startswith("damper_"):
                    timing.update(status="pass", method="both native disabled records are exact zero identities")
                elif isolated and all(peak.get("time_status") == "pass" for peak in peak_rows):
                    timing.update(status="pass" if error <= budget else "fail",
                                  method="unambiguous physical maximizing-time comparison")
                else:
                    timing["reason"] = "no unique resolved maximizing-time pairing; raw difference retained separately"
                row["peaks"][name] = {"value_comparison": values, "time_comparison": timing}
            row["value_status"] = _status([peak["value_comparison"]["status"] for peak in row["peaks"].values()])
            row["time_status"] = _status([peak["time_comparison"]["status"] for peak in row["peaks"].values()])
            row["status"] = _status([row["value_status"], row["time_status"]])
        rows.append(row)
    return {"status": _status([row["status"] for row in rows]), "pairs": rows,
            "value_status": _status([row["value_status"] for row in rows]),
            "time_status": _status([row["time_status"] for row in rows]),
            "budgets": dict(SCALE_BUDGETS), "coverage": COVERAGE}


def _reference_scaling(references: Mapping[str, Mapping[str, dict]], refinements: Mapping[str, dict]) -> dict:
    rows = []
    for level in LEVEL_NAMES:
        row: dict[str, Any] = {"level": level, "status": "inconclusive", "peaks": {}}
        a, b = (references.get(name, {}).get(level) for name in ("supported_impact", "supported_impact_scaled"))
        if a and b:
            for name in PEAK_NAMES:
                field = "value_m" if name.endswith("compression") else "value_n"
                value = _scaling_value(name, a["peaks"][name][field], b["peaks"][name][field])
                ready = all(refinements.get(fixture, {}).get("peaks", {}).get(name, {}).get("value_status") in READY
                            for fixture in (a["fixture"], b["fixture"])) and all(
                                reference.get("status") == "completed" for reference in (a, b))
                if not ready:
                    value["status"] = "inconclusive_reference"
                left, right = a["peaks"][name]["maximizer_intervals_s"], b["peaks"][name]["maximizer_intervals_s"]
                timing: dict[str, Any] = {"status": "inconclusive_pairing", "unscaled_intervals_s": left,
                                           "scaled_intervals_s": right}
                if name.startswith("damper_"):
                    timing["status"] = "pass" if left == right else "fail"
                elif ready and len(left) == len(right) == 1:
                    ta, tb = sum(left[0]) / 2, sum(right[0]) / 2
                    absolute, relative = SCALE_BUDGETS["time"]
                    budget, error = absolute + relative * abs(ta), abs(tb - ta)
                    timing.update(status="pass" if error <= budget else "fail", budget_s=budget,
                                  absolute_error_s=error, method="centres of the unique retained root brackets")
                row["peaks"][name] = {"value_comparison": value, "time_comparison": timing}
            row["status"] = _status([check["status"] for peak in row["peaks"].values() for check in peak.values()])
        rows.append(row)
    return {"status": _status([row["status"] for row in rows]), "pairs": rows, "coverage": COVERAGE}


def _retained_artifacts(output: Path) -> list[dict[str, Any]]:
    rows = []
    for directory, folders, files in os.walk(output, followlinks=False):
        for name in sorted(files):
            path = Path(directory) / name
            row = _identity(path)
            row["relative_path"] = str(path.relative_to(output))
            rows.append(row)
        for name in list(folders):
            path = Path(directory) / name
            if path.is_symlink():
                folders.remove(name)
                rows.append({"relative_path": str(path.relative_to(output)), "status": "unavailable",
                             "error": "unexpected symlink directory in evidence output"})
    return sorted(rows, key=lambda row: row["relative_path"])


def _expected_retained_digests(input_records: Sequence[Mapping[str, Any]],
                               executions: Sequence[Mapping[str, Any]]) -> dict[str, str]:
    """Keep the original identities, including invalid/partial worker evidence."""
    expected = {row["raw_path"]: row["retained_sha256"] for row in input_records
                if row.get("raw_path") and row.get("retained_sha256")}
    for execution in executions:
        for artifact in execution.get("artifacts", []):
            if artifact.get("status") == "recorded":
                expected[artifact["path"]] = artifact["sha256"]
        # These identities precede the worker's artifact inventory. If a file
        # changed between validation and inventory, preserve the validated
        # digest so the later observation cannot silently replace it.
        reading = execution.get("result_read", {})
        if reading.get("complete") and execution.get("expected_output_path"):
            expected[execution["expected_output_path"]] = reading["retained_sha256"]
        raw = execution.get("raw_validation", {})
        if raw.get("sha256") and execution.get("expected_raw_path"):
            expected[execution["expected_raw_path"]] = raw["sha256"]
    return expected


def _retained_digest_changes(expected: Mapping[str, str],
                              artifacts: Sequence[Mapping[str, Any]]) -> list[dict[str, Any]]:
    observed = {row.get("path"): row for row in artifacts}
    changes = []
    for path, digest in sorted(expected.items()):
        current = observed.get(path, {})
        if current.get("status") != "recorded" or current.get("sha256") != digest:
            changes.append({"path": path, "expected_sha256": digest,
                            "observed_sha256": current.get("sha256"),
                            "observed_status": current.get("status", "missing"),
                            "error": current.get("error", "retained bytes changed or disappeared")})
    return changes


def run_experiment(paths: Sequence[Path], output: Path, *, producer_executable: Path | None = None) -> dict[str, Any]:
    """Retain inputs first, then execute references and all possible comparisons."""
    output = _absolute(output)
    _check_parents(output)
    output.mkdir(mode=0o700)
    (output / "raw").mkdir(mode=0o700)
    (output / "references").mkdir(mode=0o700)
    declared = plan()
    declared["candidate_paths"] = [str(_absolute(path)) for path in paths]
    declared["producer_executable"] = str(_absolute(producer_executable)) if producer_executable else None
    _write_json(output / "plan.json", declared)
    before = {"utc": _utc(), "sources": _source_identity(), "git": _git_identity(), "versions": _versions()}
    if producer_executable:
        before["producer"] = {**_identity(producer_executable), "executed": False,
                              "candidate_production_attested": False}
    _write_json(output / "provenance-before.json", before)
    input_records, candidates, candidate_indices = [], [], []
    consumed = 0
    operational_errors = []
    for index, path in enumerate(paths, 1):
        record: dict[str, Any] = {"index": index, "input_path": str(_absolute(path)), "status": "invalid_input",
                                  "errors": [], "raw_path": None, "raw_complete": False}
        input_records.append(record)
        if index > MAX_FILES:
            record["errors"].append("candidate count exceeds the 64-file limit; file was not read")
            continue
        allowance = min(MAX_FILE_BYTES, MAX_COLLECTION_BYTES - consumed)
        raw, reading = bounded_read(path, allowance)
        consumed += len(raw)
        record.update(reading)
        record["raw_complete"] = reading["complete"]
        raw_path = output / "raw" / f"candidate-{index:04d}.raw"
        try:
            _write_new(raw_path, raw)
            record["raw_path"] = str(raw_path)
            record["raw_sha256"] = _sha(raw) if reading["complete"] else None
            record["retained_sha256"] = _sha(raw)
        except OSError as exc:
            message = "raw retention failed: " + _error(exc)
            record["errors"].append(message)
            record["raw_complete"] = False
            operational_errors.append(f"candidate {index}: {message}")
            record["retention_artifact"] = _identity(raw_path)
        if not record["errors"]:
            try:
                parsed = parse_candidate(raw)
                candidates.append(parsed)
                candidate_indices.append(index)
                record.update(status="parsed", fixture=parsed.fixture, settings=dict(parsed.settings),
                              slot=refinement_slot_id(parsed.fixture, tuple(value for _, value in parsed.settings)),
                              availability=parsed.availability, producer_success=parsed.run["success"],
                              producer_failure_code=parsed.run["failure_code"])
            except (CandidateFormatError, ValueError, RuntimeError) as exc:
                record["errors"].append(_error(exc))
        del raw
    matrix = validate_collection(candidates)
    matrix["supplied_file_count"] = len(paths)
    matrix["bounded_input_bytes"] = consumed
    _write_json(output / "inputs.json", {"records": input_records, "matrix": matrix})
    references: dict[str, dict] = {name: {} for name in sorted(SUPPORTED_FIXTURES)}
    executions = []
    comparable = any(candidate.run["success"] is True and candidate.peaks is not None for candidate in candidates)
    for item in declared["references"]:
        fixture, level = item["fixture"], item["level"]
        if not comparable:
            executions.append({"fixture": fixture, "level": level, "worker_executed": False,
                               "status": "not_run_no_comparable_candidates", "coverage": COVERAGE,
                               "errors": ["no successfully parsed, successful producer with peak diagnostics"]})
            continue
        try:
            execution, reference = run_reference_worker(fixture, level, output / "references" / (fixture + "-" + level))
            executions.append(execution)
            if reference is not None:
                if execution["status"] != "completed":
                    # Preserve the result, but never promote a killed/nonzero worker.
                    reference = {**reference, "status": "inconclusive_reference", "complete_duration": False}
                references[fixture][level] = reference
        except Exception as exc:
            executions.append({"fixture": fixture, "level": level, "worker_executed": False,
                               "status": "inconclusive_reference", "coverage": COVERAGE,
                               "errors": [_error(exc)]})
            operational_errors.append(f"reference {fixture}/{level}: " + _error(exc))
    _write_json(output / "reference-executions.json", executions)
    refinements = {}
    reference_module_error = None
    try:
        from contact_extrema_reference import compare_refinement, evaluate_reference
    except Exception as exc:
        compare_refinement = evaluate_reference = None
        reference_module_error = _error(exc)
    for fixture in sorted(SUPPORTED_FIXTURES):
        try:
            if compare_refinement is None:
                raise RuntimeError(reference_module_error)
            if set(references[fixture]) != set(LEVEL_NAMES):
                raise ValueError("exactly one retained reference at each R0/R1/R2 level is unavailable")
            refinements[fixture] = compare_refinement([references[fixture][level] for level in LEVEL_NAMES])
        except Exception as exc:
            refinements[fixture] = {"fixture": fixture, "status": "inconclusive_reference", "coverage": COVERAGE,
                                   "peaks": {}, "issues": [_error(exc)]}
    _write_json(output / "reference-refinement.json", refinements)
    comparisons, cache = [], {}
    for index, candidate in zip(candidate_indices, candidates):
        try:
            if evaluate_reference is None:
                def missing_evaluator(*args, **kwargs):
                    raise RuntimeError(reference_module_error)
                evaluator = missing_evaluator
            else:
                evaluator = evaluate_reference
            comparison = compare_candidate(candidate, references[candidate.fixture], refinements[candidate.fixture],
                                           evaluator=evaluator, cache=cache)
        except Exception as exc:
            comparison = {"fixture": candidate.fixture, "raw_sha256": candidate.raw_sha256,
                          "status": "incomplete", "validity": "comparison_error", "peaks": {},
                          "value_status": "inconclusive", "time_status": "inconclusive", "errors": [_error(exc)],
                          "coverage": COVERAGE, "phy002_acceptance": "incomplete", "PHY002_acceptance": "incomplete"}
        comparison["input_index"] = index
        comparison["raw_path"] = input_records[index - 1]["raw_path"]
        comparisons.append(comparison)
    scaling = coordinate_pairs(candidates, comparisons, refinements)
    try:
        reference_scaling = _reference_scaling(references, refinements)
    except Exception as exc:
        reference_scaling = {"status": "inconclusive", "errors": [_error(exc)], "coverage": COVERAGE}
    after = {"utc": _utc(), "sources": _source_identity(), "git": _git_identity()}
    if producer_executable:
        after["producer"] = {**_identity(producer_executable), "executed": False,
                             "candidate_production_attested": False}
    changes = [name for name in SOURCE_PATHS if before["sources"][name].get("sha256") !=
               after["sources"][name].get("sha256") or before["sources"][name].get("status") != "recorded" or
               after["sources"][name].get("status") != "recorded"]
    producer_ok = not producer_executable or (before["producer"].get("status") == "recorded" and
                                             after["producer"].get("status") == "recorded" and
                                             before["producer"].get("sha256") == after["producer"].get("sha256"))
    # One final streaming hash inventory serves both the manifest and the
    # original-identity comparison. Cached point evaluations cannot establish
    # that the files retained at the end still match the inputs they used.
    artifacts = _retained_artifacts(output)
    expected_retained = _expected_retained_digests(input_records, executions)
    retained_changes = _retained_digest_changes(expected_retained, artifacts)
    provenance_status = "pass" if not changes and producer_ok and not retained_changes else "fail"
    after["retained_artifacts"] = {"expected_sha256": expected_retained,
                                   "changed_or_unavailable": retained_changes}
    _write_json(output / "provenance-after.json", after)
    artifacts.append({**_identity(output / "provenance-after.json"), "relative_path": "provenance-after.json"})
    input_failed = any(record["errors"] for record in input_records) or bool(matrix["duplicate_slots"] or matrix["failed_runs"])
    input_complete = matrix["matrix_complete"] and not matrix["unavailable_peaks"] and len(paths) == EXPECTED_COUNT
    input_status = "fail" if input_failed else "pass" if input_complete else "incomplete"
    values = _status([row["value_status"] for row in comparisons])
    times = _status([row["time_status"] for row in comparisons])
    if input_status != "pass":
        values = values if values == "fail" else "inconclusive"
        times = times if times == "fail" else "inconclusive"
    refined = all(row["status"] == "resolved_under_declared_refinement" for row in refinements.values())
    executed = len(executions) == 6 and all(row["status"] == "completed" for row in executions)
    diagnostic = _status([input_status, values, times, scaling["status"], reference_scaling["status"],
                          provenance_status, "pass" if refined and executed else "inconclusive"])
    for artifact in artifacts:
        if artifact["status"] != "recorded":
            operational_errors.append("artifact identity: " + artifact["relative_path"] + ": " + artifact["error"])
    if operational_errors and diagnostic == "pass":
        diagnostic = "inconclusive"
    exit_code = 3 if operational_errors else 1 if diagnostic == "fail" else 0 if diagnostic == "pass" else 2
    slot_rows = []
    for slot in declared["slots"]:
        indices = [record["index"] for record in input_records if record.get("slot") == slot["slot"]]
        slot_rows.append({**slot, "input_indices": indices,
                          "status": "missing" if not indices else "duplicate" if len(indices) > 1 else "present"})
    report = {
        "schema_version": 1, "experiment": declared["experiment"], "started_utc": before["utc"],
        "finished_utc": after["utc"], "status": "fail" if diagnostic == "fail" else "incomplete",
        "diagnostic_status": diagnostic, "exit_code": exit_code,
        "input_status": input_status, "value_status": values, "time_status": times,
        "reference_execution_status": "pass" if executed else "incomplete",
        "reference_refinement_status": "resolved_under_declared_refinement" if refined else "inconclusive_reference",
        "coverage": COVERAGE, "phy002_acceptance": "incomplete", "PHY002_acceptance": "incomplete",
        "matrix": matrix, "slots": slot_rows, "inputs": input_records, "comparisons": comparisons,
        "references": executions, "reference_refinement": refinements,
        "coordinate_candidate_comparison": scaling, "coordinate_reference_comparison": reference_scaling,
        "provenance": {"status": provenance_status, "source_sha256_before": {
            name: row.get("sha256") for name, row in before["sources"].items()},
            "source_sha256_after": {name: row.get("sha256") for name, row in after["sources"].items()},
            "changed_or_unavailable_sources": changes, "producer_identity_stable": producer_ok,
            "expected_retained_sha256": expected_retained,
            "changed_or_unavailable_artifacts": retained_changes,
            "retained_artifact_identity_stable": not retained_changes,
            "plan_sha256": _sha(_canonical(declared) + b"\n"), "model_sha256": declared["model_sha256"],
            "settings_sha256": MATRIX_SETTINGS_SHA256, "fixture_sha256": declared["model"]["fixture_sha256"],
            "before_path": str(output / "provenance-before.json"), "after_path": str(output / "provenance-after.json")},
        "operational_errors": operational_errors, "unevaluated_gates": declared["unevaluated_gates"],
        "interpretation": "A scoped numerical diagnostic only. Independent refinement does not enclose the ODE or certify continuous extrema. General convergence/energy gates and physical fidelity remain unevaluated."}
    _write_json(output / "report.json", report)
    artifacts.append({**_identity(output / "report.json"), "relative_path": "report.json"})
    artifacts.sort(key=lambda row: row["relative_path"])
    manifest = {"schema_version": 1, "output_dir": str(output), "report_path": str(output / "report.json"),
                "status": report["status"], "diagnostic_status": diagnostic, "exit_code": exit_code,
                "coverage": COVERAGE, "phy002_acceptance": "incomplete", "PHY002_acceptance": "incomplete",
                "artifacts": artifacts, "manifest_self_hash": "excluded to avoid a recursive hash",
                "incomplete_artifacts": [row for row in artifacts if row["status"] != "recorded"]}
    _write_json(output / "manifest.json", manifest)
    return report


def main(argv: Sequence[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--plan", action="store_true", help="print the fixed stdlib-only experiment plan")
    parser.add_argument("--candidate", type=Path, action="append", default=[], help="native one-run raw JSON; repeat")
    parser.add_argument("--output-dir", type=Path, help="new exclusive evidence directory")
    parser.add_argument("--producer-executable", type=Path, help="optional declared producer identity; hash only, never execute")
    args = parser.parse_args(argv)
    if args.plan:
        if args.candidate or args.output_dir or args.producer_executable:
            parser.error("--plan is a read-only declaration and cannot be combined with experiment inputs")
        print(_canonical(plan()).decode("utf-8"))
        return 0
    if not args.candidate or args.output_dir is None:
        parser.error("an experiment requires repeated --candidate inputs and a new --output-dir")
    try:
        report = run_experiment(args.candidate, args.output_dir, producer_executable=args.producer_executable)
    except (OSError, ValueError, RuntimeError) as exc:
        print(json.dumps({"status": "operational_failure", "error": _error(exc),
                          "output_dir": str(_absolute(args.output_dir)), "coverage": COVERAGE,
                          "phy002_acceptance": "incomplete"}, ensure_ascii=True), file=sys.stderr)
        return 3
    print(_canonical({"status": report["status"], "diagnostic_status": report["diagnostic_status"],
                      "value_status": report["value_status"], "time_status": report["time_status"],
                      "reference_refinement_status": report["reference_refinement_status"],
                      "exit_code": report["exit_code"], "report_path": str(_absolute(args.output_dir) / "report.json"),
                      "coverage": COVERAGE, "phy002_acceptance": "incomplete"}).decode("utf-8"))
    return report["exit_code"]


if __name__ == "__main__":
    sys.exit(main())
