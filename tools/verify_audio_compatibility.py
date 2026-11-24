#!/usr/bin/env python3
"""Replay the retained full Q6 and legacy WAV contracts without fitting output.

This is exact compatibility evidence, not a new independent reference solution.
The original study retains the independent Q6/Q64 comparisons and their limits.
Every probe stream and rendered WAV is kept in a new, exclusive output directory.
Exit 0 means exact compatibility, 1 a mismatch, and 3 an operational failure.
"""
from __future__ import annotations

import argparse
from datetime import datetime, timezone
import hashlib
import json
from pathlib import Path
import platform
import subprocess
import sys

import validate_quadrature_audio as q6

ROOT = Path(__file__).resolve().parents[1]
Q6_BASELINE = ROOT / "docs/validation/quadrature-audio-full-20261123.json"
WAV_BASELINE = ROOT / "docs/validation/legacy-audio-baseline-20261123.json"


def digest(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def utc() -> str:
    return datetime.now(timezone.utc).isoformat()


def snapshot() -> dict[str, str]:
    paths = set(q6.SOURCE_PATHS) | {
        "apps/audio_probe.cpp", "apps/render.cpp",
        "tools/verify_audio_compatibility.py",
        "tests/cpp/fractional_observation_test.cpp",
        "tests/cpp/quadrature_engine_test.cpp",
    }
    return {name: q6.file_hash(ROOT / name) for name in sorted(paths)}


def run(probe: Path, renderer: Path, output: Path) -> int:
    probe, renderer = probe.resolve(strict=True), renderer.resolve(strict=True)
    if not probe.is_file() or not renderer.is_file():
        raise ValueError("Probe and renderer must be regular executable files")
    baseline_bytes, wav_bytes = Q6_BASELINE.read_bytes(), WAV_BASELINE.read_bytes()
    baseline, legacy = json.loads(baseline_bytes), json.loads(wav_bytes)
    columns = baseline["case_table_schema"]["columns"]
    rows = [dict(zip(columns, row, strict=True)) for row in baseline["cases"]]
    cases = q6.build_cases("full")
    if len(rows) != 2097 or len(cases) != 2097 or len(legacy["cases"]) != 24:
        raise ValueError("The complete historical case contracts are required")
    for case, expected in zip(cases, rows, strict=True):
        if (case.id != expected["case_id"]
                or q6.object_hash(case.document()) != expected["input_sha256"]
                or digest(q6.reference.probe_input(case)) != expected["probe_stdin_sha256"]):
            raise ValueError(f"Historical input changed: {case.id}")
    sources = snapshot()
    binaries = {"probe": q6.file_hash(probe), "renderer": q6.file_hash(renderer)}
    output.mkdir(parents=True, exist_ok=False)
    (output / "probe").mkdir()
    (output / "wav").mkdir()
    report = {
        "schema_version": 1, "kind": "exact-audio-compatibility",
        "scope": "Exact replay of 2097 independently studied Q6 outputs and 24 legacy WAVs; no new continuous-reference computation or physical fidelity claim.",
        "started_at_utc": utc(), "head_commit": subprocess.check_output(
            ["git", "rev-parse", "HEAD"], cwd=ROOT, text=True).strip(),
        "source_sha256": sources, "binary_sha256": binaries,
        "binary_paths": {"probe": str(probe), "renderer": str(renderer)},
        "baselines": {str(Q6_BASELINE.relative_to(ROOT)): digest(baseline_bytes),
                      str(WAV_BASELINE.relative_to(ROOT)): digest(wav_bytes)},
        "environment": {"platform": platform.platform(), "python": platform.python_version()},
        "output_directory": str(output.resolve()),
        "raw_retention": "All streams and WAVs are retained at output_directory; this report retains their hashes. Replaying this tool reconstructs them. They are not embedded in this JSON.",
        "q6_columns": ["case_id", "stdout_sha256", "stderr_sha256", "production_float64_sha256", "exact_match"],
        "q6_cases": [], "wav_cases": [], "errors": [],
    }
    try:
        for index, (case, expected) in enumerate(zip(cases, rows, strict=True)):
            stdin = q6.reference.probe_input(case)
            (output / "probe" / f"{case.id}.stdin.txt").write_bytes(stdin)
            try:
                stdout, stderr = q6.reference.run_probe(probe, case)
            except q6.reference.ProbeFailure as exc:
                (output / "probe" / f"{case.id}.stdout.json").write_bytes(exc.stdout)
                (output / "probe" / f"{case.id}.stderr.txt").write_bytes(exc.stderr)
                raise
            (output / "probe" / f"{case.id}.stdout.json").write_bytes(stdout)
            (output / "probe" / f"{case.id}.stderr.txt").write_bytes(stderr)
            samples, metadata = q6.parse_probe_output(stdout, case)
            actual = (digest(stdout), digest(stderr), q6.array_hash(samples))
            expected_hashes = (expected["probe_stdout_sha256"], expected["probe_stderr_sha256"],
                               expected["raw_array_sha256"][0])
            expected_metadata = baseline["case_table_schema"]["probe_metadata_dictionary"][
                expected["probe_metadata_index"]]
            matches = actual == expected_hashes and metadata == expected_metadata
            report["q6_cases"].append([case.id, *actual, matches])
            if (index + 1) % 100 == 0:
                print(f"Q6 {index + 1}/{len(cases)}", flush=True)
        for case in legacy["cases"]:
            destination = output / "wav" / case["file_name"]
            args = [str(renderer), "--output", str(destination), "--seconds", str(case["seconds"]),
                    "--sample-rate", str(case["sample_rate_hz"]), "--key", str(case["key"]),
                    "--velocity", str(case["velocity"]), "--gain", str(case["monitoring_gain"]),
                    "--audio-path", case["audio_path"]]
            process = subprocess.run(args, capture_output=True, timeout=120, check=False)
            (output / "wav" / f"{case['case_id']}.stdout.txt").write_bytes(process.stdout)
            (output / "wav" / f"{case['case_id']}.stderr.txt").write_bytes(process.stderr)
            actual = q6.file_hash(destination) if destination.is_file() else None
            report["wav_cases"].append({"case_id": case["case_id"], "command_argv": args,
                "returncode": process.returncode, "wav_sha256": actual,
                "stdout_sha256": digest(process.stdout), "stderr_sha256": digest(process.stderr),
                "exact_match": process.returncode == 0 and actual == case["wav_sha256"]})
    except (OSError, ValueError, RuntimeError, subprocess.SubprocessError) as exc:
        report["errors"].append(f"{type(exc).__name__}: {exc}")
    unchanged = (snapshot() == sources and q6.file_hash(probe) == binaries["probe"]
        and q6.file_hash(renderer) == binaries["renderer"]
        and Q6_BASELINE.read_bytes() == baseline_bytes and WAV_BASELINE.read_bytes() == wav_bytes)
    q6_pass = sum(row[-1] for row in report["q6_cases"])
    wav_pass = sum(row["exact_match"] for row in report["wav_cases"])
    complete = len(report["q6_cases"]) == 2097 and len(report["wav_cases"]) == 24
    code = 3 if report["errors"] or not unchanged or not complete else (
        0 if q6_pass == 2097 and wav_pass == 24 else 1)
    report.update({"completed_at_utc": utc(), "unchanged_inputs_and_binaries": unchanged,
        "summary": {"q6_cases": len(report["q6_cases"]), "q6_exact_matches": q6_pass,
                    "q6_frames": sum(case.frames for case in cases) if complete else None,
                    "wav_cases": len(report["wav_cases"]), "wav_exact_matches": wav_pass,
                    "wav_frames": legacy["total_frame_count"] if complete else None},
        "exit_code": code})
    with (output / "result.json").open("x", encoding="utf-8") as stream:
        json.dump(report, stream, indent=2, allow_nan=False)
        stream.write("\n")
    print(json.dumps(report["summary"]), flush=True)
    return code


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--probe", type=Path, required=True)
    parser.add_argument("--renderer", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    try:
        return run(args.probe, args.renderer, args.output)
    except (OSError, ValueError, KeyError, TypeError) as exc:
        print(f"Compatibility replay failed: {exc}", file=sys.stderr)
        return 3


if __name__ == "__main__":
    raise SystemExit(main())
