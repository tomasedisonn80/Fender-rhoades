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
import math
from pathlib import Path
import platform
import re
import subprocess
import sys

import validate_quadrature_audio as q6

ROOT = Path(__file__).resolve().parents[1]
Q6_BASELINE = ROOT / "docs/validation/quadrature-audio-full-20261123.json"
WAV_BASELINE = ROOT / "docs/validation/legacy-audio-baseline-20261123.json"
BASELINE_BYTE_LIMIT = 16 * 1024 * 1024


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


def baseline_bytes(path: Path) -> bytes:
    with path.open("rb") as stream:
        raw = stream.read(BASELINE_BYTE_LIMIT + 1)
    if len(raw) > BASELINE_BYTE_LIMIT:
        raise ValueError("Historical baseline exceeds its 16 MiB limit")
    return raw


def baseline_document(raw: bytes) -> dict:
    def unique(pairs):
        result = {}
        for key, value in pairs:
            if key in result:
                raise ValueError(f"Duplicate historical JSON field: {key}")
            result[key] = value
        return result

    def reject_constant(value):
        raise ValueError(f"Nonfinite historical JSON token: {value}")

    def finite_float(value):
        result = float(value)
        if not math.isfinite(result):
            raise ValueError("Historical JSON number exceeds binary64")
        return result

    try:
        document = json.loads(raw, object_pairs_hook=unique,
                              parse_constant=reject_constant, parse_float=finite_float)
    except RecursionError as exc:
        raise ValueError("Historical JSON nesting exceeds the interpreter limit") from exc
    if not isinstance(document, dict):
        raise ValueError("Historical baseline must be one JSON object")
    return document


def validate_legacy(legacy: dict) -> None:
    """Freeze the 24 historical inputs, including every retained output name.

    Stored output digests are comparison data, not proof of acquisition authenticity.
    Exact IDs/names also keep all children inside the exclusive output directory.
    """
    expected = [
        (audio_path, rate, key)
        for audio_path in ("direct", "filtered")
        for rate in (32000, 48000, 96000, 192000)
        for key in (0, 36, 72)
    ]
    cases = legacy["cases"]
    if not isinstance(cases, list) or len(cases) != len(expected):
        raise ValueError("The complete historical WAV case contract is required")
    for case, (audio_path, rate, key) in zip(cases, expected, strict=True):
        fields = {
            "case_id": f"{audio_path}-r{rate}-k{key:02d}",
            "file_name": f"{audio_path}-r{rate:06d}-k{key:02d}-u07.wav",
            "audio_path": audio_path, "sample_rate_hz": rate, "key": key,
            "velocity": 0.7, "seconds": 0.02, "monitoring_gain": 8.0,
            "frame_count": rate // 50,
        }
        if not isinstance(case, dict):
            raise ValueError("Historical WAV case must be an object")
        for name, value in fields.items():
            actual = case.get(name)
            if (type(actual) is bool or actual != value
                    or (isinstance(value, int) and type(actual) is not int)):
                raise ValueError(f"Historical WAV input changed: {fields['case_id']}/{name}")
        if not isinstance(case.get("wav_sha256"), str) or not re.fullmatch(
                r"[0-9a-f]{64}", case["wav_sha256"]):
            raise ValueError("Historical WAV digest must be lowercase SHA-256")
    if type(legacy.get("total_frame_count")) is not int or legacy["total_frame_count"] != 44160:
        raise ValueError("Historical WAV total frame count changed")


def render_with_retention(args: list[str], directory: Path, case_id: str,
                          *, destination: Path | None = None,
                          timeout: float = 120) -> subprocess.CompletedProcess:
    """Keep raw output before propagating timeout, failed exit or missing WAV."""
    try:
        process = subprocess.run(args, capture_output=True, timeout=timeout, check=False)
    except subprocess.TimeoutExpired as exc:
        (directory / f"{case_id}.stdout.txt").write_bytes(exc.stdout or b"")
        (directory / f"{case_id}.stderr.txt").write_bytes(exc.stderr or b"")
        raise
    (directory / f"{case_id}.stdout.txt").write_bytes(process.stdout)
    (directory / f"{case_id}.stderr.txt").write_bytes(process.stderr)
    if process.returncode:
        raise RuntimeError(f"Renderer failed ({process.returncode}): {case_id}")
    if destination is not None and (destination.is_symlink() or not destination.is_file()):
        raise RuntimeError(f"Renderer did not produce a regular WAV: {case_id}")
    return process


def run(probe: Path, renderer: Path, output: Path) -> int:
    probe, renderer = probe.resolve(strict=True), renderer.resolve(strict=True)
    if not probe.is_file() or not renderer.is_file():
        raise ValueError("Probe and renderer must be regular executable files")
    q6_bytes, wav_bytes = baseline_bytes(Q6_BASELINE), baseline_bytes(WAV_BASELINE)
    baseline, legacy = baseline_document(q6_bytes), baseline_document(wav_bytes)
    columns = baseline["case_table_schema"]["columns"]
    if (not isinstance(columns, list) or not all(isinstance(name, str) for name in columns)
            or len(set(columns)) != len(columns)):
        raise ValueError("Historical Q6 column names must be unique strings")
    rows = [dict(zip(columns, row, strict=True)) for row in baseline["cases"]]
    cases = q6.build_cases("full")
    if len(rows) != 2097 or len(cases) != 2097 or len(legacy["cases"]) != 24:
        raise ValueError("The complete historical case contracts are required")
    validate_legacy(legacy)
    metadata_dictionary = baseline["case_table_schema"]["probe_metadata_dictionary"]
    for case, expected in zip(cases, rows, strict=True):
        if (case.id != expected["case_id"]
                or q6.object_hash(case.document()) != expected["input_sha256"]
                or digest(q6.reference.probe_input(case)) != expected["probe_stdin_sha256"]):
            raise ValueError(f"Historical input changed: {case.id}")
        metadata_index = expected["probe_metadata_index"]
        if (type(metadata_index) is not int or not isinstance(metadata_dictionary, list)
                or not 0 <= metadata_index < len(metadata_dictionary)):
            raise ValueError(f"Historical metadata index is invalid: {case.id}")
    sources = snapshot()
    binaries = {"probe": q6.file_hash(probe), "renderer": q6.file_hash(renderer)}
    head = subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=ROOT, text=True).strip()
    output.mkdir(parents=True, exist_ok=False)
    report = {
        "schema_version": 1, "kind": "exact-audio-compatibility",
        "scope": "Exact replay of 2097 independently studied Q6 outputs and 24 legacy WAVs; no new continuous-reference computation or physical fidelity claim.",
        "started_at_utc": utc(), "head_commit": head,
        "source_sha256": sources, "binary_sha256": binaries,
        "binary_paths": {"probe": str(probe), "renderer": str(renderer)},
        "baselines": {str(Q6_BASELINE.relative_to(ROOT)): digest(q6_bytes),
                      str(WAV_BASELINE.relative_to(ROOT)): digest(wav_bytes)},
        "environment": {"platform": platform.platform(), "python": platform.python_version()},
        "output_directory": str(output.resolve()),
        "raw_retention": "All streams and WAVs are retained at output_directory; this report retains their hashes. Replaying this tool reconstructs them. They are not embedded in this JSON.",
        "q6_columns": ["case_id", "stdout_sha256", "stderr_sha256", "production_float64_sha256", "exact_match"],
        "q6_cases": [], "wav_cases": [], "errors": [],
    }
    try:
        (output / "probe").mkdir()
        (output / "wav").mkdir()
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
            process = render_with_retention(args, output / "wav", case["case_id"],
                                            destination=destination)
            actual = q6.file_hash(destination) if destination.is_file() else None
            report["wav_cases"].append({"case_id": case["case_id"], "command_argv": args,
                "returncode": process.returncode, "wav_sha256": actual,
                "stdout_sha256": digest(process.stdout), "stderr_sha256": digest(process.stderr),
                "exact_match": process.returncode == 0 and actual == case["wav_sha256"]})
    except (OSError, ValueError, RuntimeError, KeyError, TypeError,
            OverflowError, subprocess.SubprocessError) as exc:
        report["errors"].append(f"{type(exc).__name__}: {exc}")
    try:
        unchanged = (snapshot() == sources and q6.file_hash(probe) == binaries["probe"]
            and q6.file_hash(renderer) == binaries["renderer"]
            and baseline_bytes(Q6_BASELINE) == q6_bytes and baseline_bytes(WAV_BASELINE) == wav_bytes)
    except (OSError, ValueError, RuntimeError) as exc:
        unchanged = False
        report["errors"].append(f"Final integrity check: {type(exc).__name__}: {exc}")
    q6_pass = sum(row[-1] for row in report["q6_cases"])
    wav_pass = sum(row["exact_match"] for row in report["wav_cases"])
    complete = len(report["q6_cases"]) == 2097 and len(report["wav_cases"]) == 24
    code = 3 if report["errors"] or not unchanged or not complete else (
        0 if q6_pass == 2097 and wav_pass == 24 else 1)
    report.update({"completed_at_utc": utc(), "unchanged_inputs_and_binaries": unchanged,
        "summary": {"q6_cases": len(report["q6_cases"]), "q6_exact_matches": q6_pass,
                    "q6_frames": sum(case.frames for case in cases) if complete else None,
                    "wav_cases": len(report["wav_cases"]), "wav_exact_matches": wav_pass,
                    "wav_frames": legacy["total_frame_count"] if complete and wav_pass == 24 else None},
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
    except (OSError, ValueError, KeyError, TypeError, RuntimeError,
            OverflowError, subprocess.SubprocessError) as exc:
        print(f"Compatibility replay failed: {exc}", file=sys.stderr)
        return 3


if __name__ == "__main__":
    raise SystemExit(main())
