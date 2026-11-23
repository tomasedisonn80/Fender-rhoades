#!/usr/bin/env python3
"""Generate frozen contact candidates; this does not evaluate acceptance.

Eight deduplicated runs per fixture cover four maximum steps, three event
tolerances and three local-tolerance scales. The independent audit is a separate
command: validate_contact_reference.py --candidate OUTPUT/candidates.json.
Only fixture/axis metadata are imported from that tool; no oracle is executed.
Raw stdout and stderr are retained even for failed or timed-out candidates.
"""
from __future__ import annotations

import argparse
from dataclasses import asdict
from datetime import datetime, timezone
import hashlib
import json
import math
import os
from pathlib import Path
import subprocess
import sys
import tempfile
from typing import Any

from validate_contact_reference import EVENT_LEVELS, STEP_LEVELS, fixtures


ROOT = Path(__file__).resolve().parents[1]
LOCAL_SCALES = (1.0, 0.1, 0.01)
OUTPUT_STEP = 20e-6
MAX_JSON_BYTES = 64 * 1024 * 1024
SAMPLE_FIELDS = ("time_s", "hammer_position_m", "hammer_velocity_m_per_s",
                 "hammer_force_n", "damper_force_n", "hammer_impulse_ns",
                 "damper_impulse_ns", "modal_loss_j", "hammer_loss_j",
                 "damper_loss_j", "actuator_work_j", "launch_work_j", "energy_j")


def sha256(path: Path) -> str:
    with path.open("rb") as handle:
        return hashlib.file_digest(handle, "sha256").hexdigest()


def canonical(value: Any) -> bytes:
    return json.dumps(value, sort_keys=True, separators=(",", ":"),
                      ensure_ascii=False, allow_nan=False).encode("utf-8")


def write_json(path: Path, value: Any) -> None:
    """Replace only a named output; no directory is removed or traversed."""
    descriptor, name = tempfile.mkstemp(dir=path.parent, prefix=".sweep-")
    temporary = Path(name)
    try:
        with os.fdopen(descriptor, "w", encoding="utf-8") as handle:
            json.dump(value, handle, indent=2, ensure_ascii=False, allow_nan=False)
            handle.write("\n")
        os.replace(temporary, path)
    finally:
        temporary.unlink(missing_ok=True)


def plan(names: list[str]) -> list[dict[str, Any]]:
    settings: dict[tuple[float, float, float], list[str]] = {}
    for axis, values in (
        ("maximum_step", ((h, EVENT_LEVELS[-1], 1.0) for h in STEP_LEVELS)),
        ("event_tolerance", ((STEP_LEVELS[-1], eta, 1.0) for eta in EVENT_LEVELS)),
        ("local_tolerance", ((STEP_LEVELS[-1], EVENT_LEVELS[-1], s) for s in LOCAL_SCALES)),
    ):
        for key in values:
            settings.setdefault(key, []).append(axis)
    runs = []
    for name in sorted(set(names)):
        for (step, eta, scale), axes in settings.items():
            runs.append({"id": f"{len(runs) + 1:04d}_{name}", "fixture": name,
                         "maximum_step_s": step, "event_tolerance_s": eta,
                         "tolerance_scale": scale, "output_step_s": OUTPUT_STEP,
                         "axes": axes})
    return runs


def number(value: Any) -> bool:
    return type(value) in (int, float) and math.isfinite(value)


def candidate(path: Path, item: dict[str, Any], duration: float) -> dict[str, Any]:
    if path.stat().st_size > MAX_JSON_BYTES:
        raise ValueError("candidate JSON exceeds the 64 MiB reader limit")
    def nonfinite(token: str) -> None:
        raise ValueError("nonfinite JSON token: " + token)
    with path.open(encoding="utf-8") as handle:
        payload = json.load(handle, parse_constant=nonfinite)
    def finite_numbers(value: Any) -> None:
        if isinstance(value, float) and not math.isfinite(value):
            raise ValueError("nonfinite number in candidate JSON")
        if isinstance(value, dict):
            for child in value.values():
                finite_numbers(child)
        elif isinstance(value, list):
            for child in value:
                finite_numbers(child)
    finite_numbers(payload)
    if not isinstance(payload, dict) or type(payload.get("schema_version")) is not int or \
            payload["schema_version"] != 1 or payload.get("model") != "synthetic-contact-p1":
        raise ValueError("unexpected candidate envelope/schema/model")
    runs = payload.get("runs")
    if not isinstance(runs, list) or len(runs) != 1 or not isinstance(runs[0], dict):
        raise ValueError("expected exactly one candidate run")
    run = runs[0]
    if run.get("fixture") != item["fixture"]:
        raise ValueError("candidate fixture differs from requested input")
    expected = {key: item[key] for key in ("maximum_step_s", "event_tolerance_s", "output_step_s")}
    expected.update(relative_tolerance=1e-8 * item["tolerance_scale"],
                    absolute_tolerance=1e-12 * item["tolerance_scale"])
    for key, value in expected.items():
        actual = run.get(key)
        if not number(actual) or not math.isclose(actual, value, rel_tol=1e-12, abs_tol=0):
            raise ValueError("candidate setting differs from requested input: " + key)
    if run.get("success") is not True or type(run.get("failure_code")) is not int or run["failure_code"] != 0:
        raise ValueError("candidate reported failure: " + str(run.get("failure_code")))
    samples = run.get("samples")
    if not isinstance(samples, list) or not 1 <= len(samples) <= 30000:
        raise ValueError("missing or excessive sample rows")
    previous = -1.0
    for sample in samples:
        if not isinstance(sample, dict) or any(not number(sample.get(key)) for key in SAMPLE_FIELDS):
            raise ValueError("sample has missing/nonfinite scalar fields")
        for key in ("q_m", "v_m_per_s"):
            if not isinstance(sample.get(key), list) or len(sample[key]) != 2 or \
                    not all(number(value) for value in sample[key]):
                raise ValueError("sample has invalid two-mode field: " + key)
        time = sample["time_s"]
        if not previous < time <= duration + 1e-12:
            raise ValueError("sample times are not ordered within the requested duration")
        previous = time
    if samples[0]["time_s"] != 0 or not math.isclose(previous, duration, rel_tol=0, abs_tol=1e-12):
        raise ValueError("candidate did not cover the complete frozen duration")
    if not isinstance(run.get("events"), list) or not isinstance(run.get("diagnostics"), dict):
        raise ValueError("missing event or diagnostic containers")
    return run


def execute(executable: Path, output: Path, item: dict[str, Any], timeout: float,
            duration: float) -> dict[str, Any]:
    argv = [str(executable), "--fixture", item["fixture"],
            "--max-step", repr(item["maximum_step_s"]),
            "--event-tolerance", repr(item["event_tolerance_s"]),
            "--tolerance-scale", repr(item["tolerance_scale"]),
            "--output-step", repr(item["output_step_s"])]
    stdout = output / (item["id"] + ".json")
    stderr = output / (item["id"] + ".stderr.txt")
    record = dict(item, argv=argv, stdout=stdout.name, stderr=stderr.name,
                  status="failed", exit_code=None, errors=[])
    # Streams go directly to files: a noisy candidate cannot grow a PIPE buffer
    # in memory. Serial execution bounds active child processes to one.
    with stdout.open("wb") as out, stderr.open("wb") as err:
        try:
            result = subprocess.run(argv, stdout=out, stderr=err, timeout=timeout, check=False)
            record["exit_code"] = result.returncode
            if result.returncode != 0:
                record["errors"].append(f"candidate exited {result.returncode}")
        except subprocess.TimeoutExpired:
            record["errors"].append(f"candidate exceeded {timeout:g} second timeout")
        except OSError as error:
            record["errors"].append(str(error))
    record["stdout_sha256"] = sha256(stdout)
    record["stderr_sha256"] = sha256(stderr)
    record["stdout_bytes"] = stdout.stat().st_size
    try:
        run = candidate(stdout, item, duration)
        record["sample_count"] = len(run["samples"])
        record["event_count"] = len(run["events"])
    except (OSError, UnicodeError, ValueError, TypeError, KeyError, OverflowError) as error:
        record["errors"].append(str(error))
    if not record["errors"]:
        record["status"] = "candidate_generated"
    return record


def aggregate(output: Path, items: list[dict[str, Any]], definitions: dict[str, Any]) -> None:
    # Retain per-run byte-exact output; the combined file is a convenient input
    # for the existing independent auditor, created only after every run passed
    # the generation/schema checks. It is streamed one run at a time.
    descriptor, name = tempfile.mkstemp(dir=output, prefix=".sweep-")
    temporary = Path(name)
    try:
        with os.fdopen(descriptor, "w", encoding="utf-8") as handle:
            handle.write('{"schema_version":1,"generation_only":true,"runs":[')
            for index, item in enumerate(items):
                if index:
                    handle.write(",")
                run = candidate(output / (item["id"] + ".json"), item,
                                definitions[item["fixture"]].duration)
                json.dump(run, handle, separators=(",", ":"), allow_nan=False)
            handle.write("]}\n")
        os.replace(temporary, output / "candidates.json")
    finally:
        temporary.unlink(missing_ok=True)


def main() -> int:
    definitions = fixtures()
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--executable", type=Path, default=ROOT / "build/release/rhodes_contact_reference")
    parser.add_argument("--output-dir", type=Path, default=ROOT / "build/contact-sweep")
    parser.add_argument("--fixture", choices=sorted(definitions), action="append",
                        help="repeat for a subset; default is all 26 frozen fixtures")
    parser.add_argument("--timeout", type=float, default=30.0, help="seconds per candidate, 0 < timeout <= 600")
    parser.add_argument("--force", action="store_true", help="replace only this run's named outputs in an existing directory")
    parser.add_argument("--plan", action="store_true", help="print deterministic input plan without running or writing files")
    args = parser.parse_args()
    if not math.isfinite(args.timeout) or not 0 < args.timeout <= 600:
        parser.error("timeout must be finite and in (0, 600]")
    names = sorted(set(args.fixture or definitions))
    items = plan(names)
    metadata = {"fixtures": {name: asdict(definitions[name]) for name in names},
                "maximum_step_levels_s": STEP_LEVELS, "event_levels_s": EVENT_LEVELS,
                "local_tolerance_scales": LOCAL_SCALES, "output_step_s": OUTPUT_STEP,
                "runs": items, "nonlinear_solve_axis": "inapplicable: explicit RK4"}
    if args.plan:
        print(json.dumps(metadata, indent=2, allow_nan=False))
        return 0
    output = args.output_dir.absolute()
    try:
        executable = args.executable.resolve(strict=True)
        if not executable.is_file():
            raise ValueError("executable is not a regular file")
        if output.is_symlink():
            raise ValueError("output directory must not be a symlink")
        if output.exists() and not args.force:
            raise ValueError("output directory already exists; use a new path or explicit --force")
        output.mkdir(parents=True, exist_ok=args.force)
        # Do not follow or overwrite symlinks/nonregular managed outputs. Other
        # files, including results of a different subset, are left untouched.
        managed = [output / "manifest.json", output / "candidates.json"]
        managed += [output / (item["id"] + suffix) for item in items
                    for suffix in (".json", ".stderr.txt")]
        for path in managed:
            if path.is_symlink() or (path.exists() and not path.is_file()):
                raise ValueError("managed output is a symlink or nonregular file: " + str(path))
        # A previous successful aggregate must not survive a new failed run.
        (output / "candidates.json").unlink(missing_ok=True)
        source_paths = [Path(__file__).resolve(), ROOT / "tools/validate_contact_reference.py",
                        ROOT / "docs/specs/CONTACT_ACCEPTANCE_20261123.md",
                        ROOT / "docs/agents/development-20261123/W1-A005.md",
                        ROOT / "docs/agents/development-20261123/W1-A007.md"]
        manifest = {"schema_version": 1, "kind": "rhodes-contact-candidate-sweep",
                    "created_utc": datetime.now(timezone.utc).isoformat(),
                    "status": "running", "acceptance": "not evaluated",
                    "executable": {"path": str(executable), "sha256": sha256(executable)},
                    "metadata": metadata, "input_metadata_sha256": hashlib.sha256(canonical(metadata)).hexdigest(),
                    "source_sha256": {str(path.relative_to(ROOT)): sha256(path) for path in source_paths},
                    "execution": {"parallel_jobs": 1, "timeout_seconds_per_run": args.timeout},
                    "limits": ["Candidate generation is not an independent-method or physical-fidelity audit.",
                               "Source hashes are inspection context; executable build provenance is not inferred.",
                               "A subset cannot establish the complete frozen acceptance matrix."],
                    "results": []}
        write_json(output / "manifest.json", manifest)
        for item in items:
            record = execute(executable, output, item, args.timeout, definitions[item["fixture"]].duration)
            manifest["results"].append(record)
            write_json(output / "manifest.json", manifest)
            print(f"{len(manifest['results'])}/{len(items)} {item['id']}: {record['status']}", file=sys.stderr)
        manifest["executable"]["sha256_after"] = sha256(executable)
        unchanged = manifest["executable"]["sha256_after"] == manifest["executable"]["sha256"]
        success = unchanged and all(record["status"] == "candidate_generated" for record in manifest["results"])
        if success:
            aggregate(output, items, definitions)
            manifest["aggregate"] = {"path": "candidates.json", "sha256": sha256(output / "candidates.json")}
            manifest["audit_argv"] = [sys.executable, str(ROOT / "tools/validate_contact_reference.py"),
                                     "--candidate", str(output / "candidates.json")]
        manifest["status"] = "candidate_generation_complete" if success else "failed"
        manifest["executable_unchanged"] = unchanged
        write_json(output / "manifest.json", manifest)
        print(json.dumps({"status": manifest["status"], "acceptance": "not evaluated",
                          "runs": len(items), "manifest": str(output / "manifest.json")}))
        return 0 if success else 1
    except (OSError, ValueError, TypeError) as error:
        print("contact sweep: " + str(error), file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
