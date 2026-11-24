#!/usr/bin/env python3
"""Compare native baseline/current VST3 modules in isolated SDK-host processes.

Adapted from this repository's own retained comparison driver in
docs/validation/host-midi-loaded-compatibility-20261124.json,
reproduction.driver_source SHA-256
6ece0b767d31b86b305486069b263f8a7a38acc47b10d5bd330979097d447e88.
The existing matrix and exact-byte comparison are retained. Paths, fresh record
identity, complete-input checks, native bundle discovery, explicit unity and
failure evidence are now portable and reusable. Python standard library only.

Each invocation creates a new run directory. report.json at --output-dir is
replaced with an incomplete record before input validation; only a complete
216-capture / 144-comparison matrix can replace it with a passed record.
Baseline manifest.json must identify the fixed revision and hash all three
native module binaries. Additional supplied provenance is retained verbatim.
"""

from __future__ import annotations

import argparse
from concurrent.futures import ThreadPoolExecutor
from datetime import datetime, timezone
from hashlib import sha256
from itertools import product
import json
import math
import os
from pathlib import Path
import platform
import re
import struct
import subprocess
import sys
import time
import uuid


ROOT = Path(__file__).resolve().parents[1]
BASELINE_REVISION = "6c91dab00855b67a559e92edb79ae49e567be469"
VARIANTS = ("RhodesResearch", "RhodesResearchFiltered", "RhodesResearchQuadrature")
IDENTITIES = {
    "RhodesResearch": (1, "BDDA70F46F9244C1A5639EE371A0B804", "EA801E8B97864ED9B3A0E6572D81CE61"),
    "RhodesResearchFiltered": (2, "4C0E773A24FC4BE9B804BC71B8B98B33", "0569EDDBD21D4568A9ED52C3D8545B6D"),
    "RhodesResearchQuadrature": (3, "D73B886A72E04FC892435F1DC5F0B6A9", "A45F23E1B68A4D79AF31472C8E909D52"),
}
FORMATS = (32, 64)
RATES = (48000, 192000)
PARTITIONS = {"whole": (2048,), "fixed64": (64,), "ragged": (1, 7, 31, 128, 3, 257, 64, 509, 2, 100)}
WORKLOADS = ("dense", "sparse")
MODES = ("baseline", "default", "explicit1")
EPOCH_FRAMES = 2048
TAIL_FRAMES = 128
TOTAL_FRAMES = 2 * EPOCH_FRAMES + TAIL_FRAMES
EXPECTED_FIXTURES = len(VARIANTS) * len(FORMATS) * len(RATES) * len(PARTITIONS) * len(WORKLOADS)
EXPECTED_CAPTURES = EXPECTED_FIXTURES * len(MODES)
EXPECTED_PAIRS = EXPECTED_FIXTURES * 2
EXPECTED_PARTITION_GROUPS = len(MODES) * len(VARIANTS) * len(FORMATS) * len(RATES)
MAX_METADATA_BYTES = 128 * 1024
MAX_STDERR_BYTES = 64 * 1024
MAX_INPUT_BYTES = 512 * 1024 * 1024
PEDALS = ((0, 0.0), (191, 1.0), (512, 0.0), (768, 1.0), (1280, 0.0), (1600, 1.0), (1664, 0.0))
METADATA_KEYS = {
    "capture_schema", "fixture_version", "complete", "capture_id", "mode", "format_bits",
    "sample_rate_hz", "partition", "gain_workload", "byte_order", "processor_id", "controller_id",
    "model", "latency", "tail_samples", "parameter_count", "legacy_parameters", "initial_state",
    "checkpoints", "callbacks", "nonzero_left_samples", "output_frames", "note_events",
    "pressure_events", "command_points", "expression_points", "epoch_nonzero_left_samples", "epoch_peak_abs",
}
MODE_DIFFERENCES = {"capture_id", "mode", "parameter_count", "expression_points"}


class ComparisonError(ValueError):
    """An incomplete, inconsistent, out-of-bounds or mismatching capture."""


def require(condition: bool, message: str) -> None:
    if not condition:
        raise ComparisonError(message)


def utc_now() -> str:
    return datetime.now(timezone.utc).isoformat()


def file_hash(path: Path) -> str:
    require(path.is_file(), f"missing regular file: {path}")
    require(path.stat().st_size <= MAX_INPUT_BYTES, f"input exceeds hash bound: {path}")
    result = sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            result.update(chunk)
    return result.hexdigest()


def json_hash(value: object) -> str:
    return sha256(json.dumps(value, sort_keys=True, separators=(",", ":"), allow_nan=False).encode()).hexdigest()


def read_bounded(path: Path, limit: int) -> bytes:
    require(path.is_file(), f"missing capture file: {path}")
    require(path.stat().st_size <= limit, f"capture exceeds {limit}-byte bound: {path.name}")
    with path.open("rb") as stream:
        data = stream.read(limit + 1)
    require(len(data) <= limit, f"capture grew beyond bound: {path.name}")
    return data


def strict_json(data: bytes) -> object:
    def unique(pairs: list[tuple[str, object]]) -> dict:
        result = {}
        for key, value in pairs:
            require(key not in result, f"duplicate JSON member: {key}")
            result[key] = value
        return result

    def reject_constant(value: str) -> None:
        raise ComparisonError(f"nonfinite JSON number: {value}")

    return json.loads(data.decode("utf-8"), object_pairs_hook=unique, parse_constant=reject_constant)


def write_json(path: Path, value: object) -> None:
    temporary = path.with_name(path.name + ".tmp-" + uuid.uuid4().hex)
    try:
        temporary.write_text(json.dumps(value, indent=2, sort_keys=True, allow_nan=False) + "\n", encoding="utf-8")
        os.replace(temporary, path)
    finally:
        temporary.unlink(missing_ok=True)


def dense_gain(frame: int) -> float:
    return 0.0 if 1750 <= frame < 1780 else (32 + (frame * 13) % 193) / 256.0


def state_hex(model: int, gain: float = 0.5, pedal: float = 0.0) -> str:
    # Independent literal legacy packet; no production codec is imported.
    return struct.pack("<4sHHff", b"RHST", 1, model, gain, pedal).hex()


def blocks(partition: str) -> list[tuple[int, int]]:
    result = []
    start = 0
    while start < EPOCH_FRAMES:
        count = min(PARTITIONS[partition][len(result) % len(PARTITIONS[partition])], EPOCH_FRAMES - start)
        result.append((start, count))
        start += count
    return result


def expected_checkpoints(model: int, partition: str) -> list[dict]:
    def checkpoint(name: str, frame: int, frames: int, state: str) -> dict:
        return {"name": name, "frame": frame, "frames": frames, "state": state}

    initial = state_hex(model)
    custom = state_hex(model, 0.25, 1.0)
    result = [checkpoint("flush", 0, 0, custom), checkpoint("pending_default_recall", 0, 0, initial)]
    for epoch in range(2):
        if epoch:
            result += [checkpoint("pending_custom_recall", EPOCH_FRAMES, 0, custom),
                       checkpoint("flush", EPOCH_FRAMES, 0, state_hex(model, 0.75, 0.0))]
        for start, count in blocks(partition):
            end = start + count - 1
            pedal = next(value for at, value in reversed(PEDALS) if at <= end)
            result.append(checkpoint("render", epoch * EPOCH_FRAMES + start, count,
                                     state_hex(model, dense_gain(end), pedal)))
    result += [checkpoint("saved_live_round_trip", 2 * EPOCH_FRAMES, 0,
                          state_hex(model, dense_gain(EPOCH_FRAMES - 1))),
               checkpoint("final_pending_default_recall", 2 * EPOCH_FRAMES, 0, initial),
               checkpoint("reset_silence", 2 * EPOCH_FRAMES, TAIL_FRAMES, initial)]
    return result


def validate_capture(metadata: object, audio: bytes, *, capture_id: str, mode: str,
                     variant: str, format_bits: int, rate: int, partition: str, workload: str) -> dict:
    """Validate complete fresh records independently before any pair can pass."""
    require(isinstance(metadata, dict) and set(metadata) == METADATA_KEYS, "incomplete or unknown capture schema")
    model, processor_id, controller_id = IDENTITIES[variant]
    expected = {
        "capture_schema": 1, "fixture_version": 1, "capture_id": capture_id, "mode": mode,
        "format_bits": format_bits, "sample_rate_hz": rate, "partition": partition, "gain_workload": workload,
        "byte_order": sys.byteorder, "processor_id": processor_id, "controller_id": controller_id,
        "model": model, "latency": 0 if model == 1 else 48, "tail_samples": 0xFFFFFFFF,
        "parameter_count": 18 if mode == "baseline" else 19, "initial_state": state_hex(model),
        "callbacks": 2 * len(blocks(partition)) + 3, "output_frames": TOTAL_FRAMES,
        "note_events": 44, "pressure_events": 16, "command_points": 48,
        "expression_points": (4 + 2 * sum(1 if count == 1 else 2 for _, count in blocks(partition)))
        if mode == "explicit1" else 0,
    }
    require(metadata["complete"] is True, "capture did not complete")
    for key, value in expected.items():
        require(type(metadata[key]) is type(value) and metadata[key] == value, f"invalid capture field: {key}")
    legacy_parameters = metadata["legacy_parameters"]
    ids = [0, 1] + list(range(0x1000, 0x1010))
    require(isinstance(legacy_parameters, list) and len(legacy_parameters) == len(ids), "legacy parameter inventory extent")
    for item, parameter_id in zip(legacy_parameters, ids):
        require(isinstance(item, dict) and set(item) == {"id", "title", "units", "flags", "step_count", "default_normalized"},
                "legacy parameter inventory schema")
        require(type(item["id"]) is int and item["id"] == parameter_id, "legacy parameter identity")
        require(isinstance(item["title"], str) and isinstance(item["units"], str), "parameter labels")
        require(type(item["flags"]) is int and type(item["step_count"]) is int, "parameter metadata integers")
        require(type(item["default_normalized"]) in (int, float) and math.isfinite(item["default_normalized"])
                and 0 <= item["default_normalized"] <= 1, "finite normalized parameter default")
    sample_bytes = format_bits // 8
    require(len(audio) == TOTAL_FRAMES * 2 * sample_bytes, "missing, partial or oversized audio trace")
    unpack = struct.Struct(("<" if sys.byteorder == "little" else ">") + ("ff" if format_bits == 32 else "dd"))
    left = []
    for offset in range(0, len(audio), 2 * sample_bytes):
        a, b = unpack.unpack_from(audio, offset)
        require(math.isfinite(a) and math.isfinite(b), "nonfinite audio")
        require(audio[offset:offset + sample_bytes] == audio[offset + sample_bytes:offset + 2 * sample_bytes],
                "stereo sample bits differ")
        left.append(a)
    counts = [sum(value != 0 for value in left[start:start + EPOCH_FRAMES]) for start in (0, EPOCH_FRAMES)]
    peaks = [max(abs(value) for value in left[start:start + EPOCH_FRAMES]) for start in (0, EPOCH_FRAMES)]
    require(all(count >= 128 for count in counts) and all(peak > 1e-6 for peak in peaks),
            "each musical epoch needs nonvacuous nonzero audio")
    require(all(value == 0 for value in left[2 * EPOCH_FRAMES:]), "recall did not clear the final silent tail")
    require(metadata["epoch_nonzero_left_samples"] == counts, "nonzero epoch evidence differs from raw samples")
    require(metadata["epoch_peak_abs"] == peaks, "peak evidence differs from raw samples")
    require(type(metadata["nonzero_left_samples"]) is int and metadata["nonzero_left_samples"] == sum(counts),
            "nonzero evidence differs from raw samples")
    checkpoints = metadata["checkpoints"]
    expected_points = expected_checkpoints(model, partition)
    require(isinstance(checkpoints, list) and len(checkpoints) == len(expected_points), "missing state checkpoints")
    for actual, reference in zip(checkpoints, expected_points):
        require(isinstance(actual, dict) and set(actual) == set(reference) | {"silence_flags"}, "checkpoint schema")
        for key, value in reference.items():
            require(type(actual[key]) is type(value) and actual[key] == value, f"checkpoint {reference['name']}: {key}")
        start, count = reference["frame"], reference["frames"]
        silence = (0 if any(value != 0 for value in left[start:start + count]) else 3) if count else 0
        require(type(actual["silence_flags"]) is int and actual["silence_flags"] == silence, "silence flag evidence")
    return {
        "output_frames": TOTAL_FRAMES, "audio_bytes": len(audio), "audio_sha256": sha256(audio).hexdigest(),
        "state_checkpoints": len(checkpoints), "state_checkpoints_sha256": json_hash(checkpoints),
        "callbacks": metadata["callbacks"], "nonzero_left_samples": sum(counts),
        "epoch_nonzero_left_samples": counts, "epoch_peak_abs": peaks,
        "legacy_metadata_sha256": json_hash({key: value for key, value in metadata.items() if key not in MODE_DIFFERENCES}),
    }


def bundle_inventory(root: Path) -> dict:
    """Fingerprint bundle contents using native layouts, never Linux-only paths."""
    result = {}
    for variant in VARIANTS:
        bundle = root / (variant + ".vst3")
        require(bundle.is_dir(), f"missing bundle: {bundle}")
        files = {}
        for path in sorted(bundle.rglob("*")):
            if path.is_file():
                require(len(files) < 512, f"bundle file-count bound: {bundle}")
                relative = path.relative_to(root).as_posix()
                files[relative] = {"bytes": path.stat().st_size, "sha256": file_hash(path)}
        require(files, f"empty bundle: {bundle}")
        native = []
        for relative in files:
            parts = Path(relative).parts
            if sys.platform == "win32":
                selected = len(parts) == 4 and parts[2].endswith("-win") and parts[3] == variant + ".vst3"
            elif sys.platform == "darwin":
                selected = parts == (variant + ".vst3", "Contents", "MacOS", variant)
            else:
                selected = len(parts) == 4 and parts[2].endswith("-linux") and parts[3] == variant + ".so"
            if selected:
                native.append(relative)
        require(len(native) == 1, f"expected one native module binary in {bundle}, got {native}")
        result[variant] = {"bundle_path": str(bundle), "native_binary": native[0], "files": files}
    return result


def validate_baseline_manifest(root: Path, inventory: dict) -> dict:
    manifest = strict_json(read_bounded(root / "manifest.json", 1024 * 1024))
    require(isinstance(manifest, dict) and manifest.get("source_commit") == BASELINE_REVISION,
            "baseline manifest must name the frozen source commit")
    entries = manifest.get("modules")
    require(isinstance(entries, list) and len(entries) == len(VARIANTS), "baseline manifest needs three module hashes")
    expected = {}
    for bundle in inventory.values():
        relative = bundle["native_binary"]
        expected[(root / relative).resolve()] = bundle["files"][relative]["sha256"]
    observed = {}
    for entry in entries:
        require(isinstance(entry, dict) and isinstance(entry.get("path"), str)
                and isinstance(entry.get("sha256"), str), "invalid baseline module manifest entry")
        path = Path(entry["path"])
        path = (path if path.is_absolute() else root / path).resolve()
        require(path not in observed, "duplicate baseline module manifest entry")
        observed[path] = entry["sha256"]
    require(observed == expected, "baseline manifest paths/hashes differ from actual native modules")
    return manifest


def source_fingerprints(root: Path) -> dict:
    files = []
    for relative in ("CMakeLists.txt", "cmake", "include", "src", "plugins/vst3",
                     "tests/cpp/vst3_legacy_capture.cpp", "tools/compare_vst3_legacy.py",
                     "docs/specs/HOST_EXPRESSION_MONO_20261124.md"):
        path = root / relative
        require(path.exists(), f"missing source binding: {path}")
        files.extend(item for item in path.rglob("*") if item.is_file()) if path.is_dir() else files.append(path)
    require(len(files) <= 1024, "source inventory bound")
    return {path.relative_to(root).as_posix(): file_hash(path) for path in sorted(files)}


def read_git(root: Path, *arguments: str) -> str | None:
    try:
        process = subprocess.run(["git", "-C", str(root), *arguments], capture_output=True, timeout=15)
    except (OSError, subprocess.TimeoutExpired):
        return None
    if process.returncode or len(process.stdout) > 1024 * 1024:
        return None
    return process.stdout.decode("utf-8", errors="replace").strip()


def build_provenance(capture: Path) -> dict:
    for directory in list(capture.parents)[:4]:
        cache = directory / "CMakeCache.txt"
        if not cache.is_file():
            continue
        data = read_bounded(cache, 1024 * 1024).decode("utf-8", errors="replace")
        selected = {}
        for line in data.splitlines():
            match = re.match(r"(CMAKE_(?:BUILD_TYPE|CXX_COMPILER|CXX_FLAGS[^:]*|GENERATOR[^:]*|OSX_[^:]*|SYSTEM_[^:]*)|RHODES_VST3_SDK_ROOT):[^=]*=(.*)", line)
            if match:
                selected[match[1]] = match[2]
        return {"cache_path": str(cache), "cache_sha256": file_hash(cache), "settings": selected}
    return {"cache_path": None, "note": "No nearby CMake cache; capture binary hash is retained."}


def run_bounded(command: list[str], stdout: Path, stderr: Path, trace: Path,
                audio_limit: int, timeout: float) -> tuple[int, str | None]:
    """No shell; fresh files, wall-clock limit, bounded parsed output and monitored files."""
    require(not any(path.exists() for path in (stdout, stderr, trace)), "refuse pre-existing capture files")
    error = None
    with stdout.open("xb") as output, stderr.open("xb") as diagnostics:
        process = subprocess.Popen(command, stdout=output, stderr=diagnostics)
        deadline = time.monotonic() + timeout
        while True:
            try:
                process.wait(timeout=min(0.05, max(0.001, deadline - time.monotonic())))
                break
            except subprocess.TimeoutExpired:
                if time.monotonic() >= deadline:
                    error = f"capture exceeded {timeout:g}-second timeout"
                if any(path.exists() and path.stat().st_size > limit for path, limit in
                       ((stdout, MAX_METADATA_BYTES), (stderr, MAX_STDERR_BYTES), (trace, audio_limit))):
                    error = "capture output exceeded a bounded file extent"
                if error:
                    process.kill()
                    process.wait(timeout=10)
                    break
        code = process.returncode
    for path, limit in ((stdout, MAX_METADATA_BYTES), (stderr, MAX_STDERR_BYTES), (trace, audio_limit)):
        if path.exists() and path.stat().st_size > limit:
            original_size = path.stat().st_size
            with path.open("r+b") as stream:
                stream.truncate(limit)
            error = f"oversized {path.name} ({original_size} bytes); retained bounded prefix only"
    return code, error


def one_capture(capture: Path, bundle_root: Path, run_directory: Path, key: tuple,
                mode: str, timeout: float) -> tuple[dict, dict | None, bytes | None]:
    variant, format_bits, rate, partition, workload = key
    stem = f"{mode}-{variant}-{format_bits}-{rate}-{partition}-{workload}"
    stdout, stderr, trace = (run_directory / (stem + suffix) for suffix in (".json", ".stderr", ".bin"))
    capture_id = uuid.uuid4().hex
    command = [str(capture), str(bundle_root / (variant + ".vst3")), str(format_bits), str(rate),
               partition, workload, mode, str(trace), capture_id]
    record = {"capture_id": capture_id, "mode": mode, "variant": variant, "format_bits": format_bits,
              "sample_rate_hz": rate, "partition": partition, "gain_workload": workload,
              "command": command, "stdout_path": stdout.name, "stderr_path": stderr.name,
              "trace_path": trace.name, "passed": False}
    metadata = audio = None
    try:
        code, error = run_bounded(command, stdout, stderr, trace, TOTAL_FRAMES * 2 * (format_bits // 8), timeout)
        record["exit_code"] = code
        require(error is None, error or "capture error")
        require(code == 0, f"capture exited {code}; see {stderr.name}")
        metadata_bytes = read_bounded(stdout, MAX_METADATA_BYTES)
        # Bind retained evidence to the byte snapshots actually parsed and
        # compared. Rehashing files after validation could bless a replacement.
        record.update(stdout_sha256=sha256(metadata_bytes).hexdigest(), stdout_bytes=len(metadata_bytes))
        metadata = strict_json(metadata_bytes)
        audio = read_bounded(trace, TOTAL_FRAMES * 2 * (format_bits // 8))
        record.update(trace_sha256=sha256(audio).hexdigest(), trace_bytes=len(audio))
        record.update(validate_capture(metadata, audio, capture_id=capture_id, mode=mode, variant=variant,
                                       format_bits=format_bits, rate=rate, partition=partition, workload=workload))
        record["passed"] = True
    except (OSError, ValueError, subprocess.SubprocessError) as error:
        record["error"] = str(error)
    for kind, path in (("stdout", stdout), ("stderr", stderr), ("trace", trace)):
        if path.is_file() and kind + "_sha256" not in record:
            record[kind + "_sha256"] = file_hash(path)
            record[kind + "_bytes"] = path.stat().st_size
    return record, metadata, audio


def compare_pair(baseline: tuple, current: tuple) -> dict:
    old_record, old_metadata, old_audio = baseline
    new_record, new_metadata, new_audio = current
    valid = old_record["passed"] and new_record["passed"]
    metadata_equal = valid and ({key: value for key, value in old_metadata.items() if key not in MODE_DIFFERENCES}
                               == {key: value for key, value in new_metadata.items() if key not in MODE_DIFFERENCES})
    audio_equal = valid and old_audio == new_audio
    return {
        "mode": new_record["mode"], "baseline_capture_id": old_record["capture_id"],
        "current_capture_id": new_record["capture_id"], "valid_complete_captures": bool(valid),
        "metadata_and_state_exact": bool(metadata_equal), "sample_bytes_exact": bool(audio_equal),
        "baseline_audio_sha256": old_record.get("audio_sha256"), "current_audio_sha256": new_record.get("audio_sha256"),
        "passed": bool(valid and metadata_equal and audio_equal),
    }


def raw_records_match_manifest(captures: list[dict], manifest: dict) -> bool:
    """Bind the compared bytes to the retained files, including post-read changes."""
    expected_files = {"commands.json"}
    for record in captures:
        for kind in ("stdout", "stderr", "trace"):
            name = record.get(kind + "_path")
            expected_files.add(name)
            expected = {"bytes": record.get(kind + "_bytes"), "sha256": record.get(kind + "_sha256")}
            if manifest["files"].get(name) != expected:
                return False
    return set(manifest["files"]) == expected_files


def parser() -> argparse.ArgumentParser:
    result = argparse.ArgumentParser(description=__doc__)
    result.add_argument("--capture", type=Path, required=True, help="SDK-only rhodes_vst3_legacy_capture executable")
    result.add_argument("--baseline-root", type=Path, required=True, help="three frozen baseline bundles plus manifest.json")
    result.add_argument("--current-root", type=Path, required=True, help="three current native bundles")
    result.add_argument("--output-dir", type=Path, required=True, help="report.json and retained unique raw-run directories")
    result.add_argument("--sdk-root", type=Path, help="pinned SDK checkout for revision/submodule provenance")
    result.add_argument("--source-root", type=Path, default=ROOT, help="current source root (defaults to this script's repository)")
    result.add_argument("--timeout", type=float, default=30.0, help="per-capture seconds, 1..60 (default: 30)")
    return result


def main(argv: list[str] | None = None) -> int:
    args = parser().parse_args(argv)
    for name in ("capture", "baseline_root", "current_root", "output_dir", "source_root", "sdk_root"):
        path = getattr(args, name)
        if path is not None:
            setattr(args, name, path.resolve())
    args.output_dir.mkdir(parents=True, exist_ok=True)
    run_id = uuid.uuid4().hex
    run_directory = args.output_dir / ("run-" + datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%S") + "-" + run_id)
    run_directory.mkdir()
    report = {
        "schema_version": 1, "status": "incomplete", "run_id": run_id, "started_utc": utc_now(),
        "scope": "Native SDK-hosted exact legacy stereo compatibility; separate same-FUID module processes",
        "baseline_revision": BASELINE_REVISION, "raw_directory": run_directory.name,
        "environment": {"platform": None, "machine": platform.machine(), "python": platform.python_version(),
                        "byte_order": sys.byteorder, "host_linkage": "SDK hosting/loader only; no instrument implementation headers or linkage"},
        "inputs": {"variants": VARIANTS, "formats_bits": FORMATS, "sample_rates_hz": RATES,
                   "partitions": PARTITIONS, "gain_workloads": WORKLOADS, "modes": MODES,
                   "epoch_frames": EPOCH_FRAMES, "epochs": 2, "post_recall_silence_frames": TAIL_FRAMES,
                   "nonzero_minimum_per_epoch": 128, "peak_abs_minimum_exclusive": 1e-6,
                   "notes_per_capture": 44, "pressure_events_per_capture": 16, "cc123_points_per_capture": 48,
                   "raw_sample_encoding": "interleaved stereo native-endian IEEE binary32/binary64",
                   "sparse_partition_scope": "Sparse gain endpoints depend on callback size; compare only matching partitions.",
                   "full_timeline": "tests/cpp/vst3_legacy_capture.cpp (source hash retained)",
                   "per_capture_timeout_seconds": args.timeout if math.isfinite(args.timeout) else None},
        "summary": {"passed": False, "complete": False}, "captures": [], "cases": [], "dense_partition_comparisons": [],
        "limitations": [
            "Baseline source provenance is supplied by its manifest; the driver verifies actual native module hashes.",
            "Current source hashes describe the observed worktree, not an attestation that it produced a binary; module and recorder hashes bind the executed comparison.",
            "Exact comparisons are within one native platform/compiler build pair, not between different platforms.",
            "Only the recorded valid stereo timelines are replayed; malformed input, mono, allocation, concurrency and CPU deadlines are separate tests.",
            "Nonzero digital sample evidence does not establish playback-device audibility, a named DAW pass or measured physical fidelity.",
        ],
    }
    if not math.isfinite(args.timeout):
        report["inputs"]["invalid_timeout_argument"] = str(args.timeout)
    write_json(args.output_dir / "report.json", report)
    write_json(run_directory / "report.json", report)
    runs = {}
    try:
        require(math.isfinite(args.timeout) and 1 <= args.timeout <= 60, "timeout must be finite and in 1..60")
        # platform.platform() may spawn a host-information subprocess. Keep
        # invalid attempts free of child execution, including that probe.
        report["environment"]["platform"] = platform.platform()
        require(args.capture.is_file(), f"missing capture executable: {args.capture}")
        require(args.baseline_root != args.current_root, "baseline and current roots must be distinct")
        modules_before = {"baseline": bundle_inventory(args.baseline_root), "current": bundle_inventory(args.current_root)}
        manifest = validate_baseline_manifest(args.baseline_root, modules_before["baseline"])
        manifest_hash = file_hash(args.baseline_root / "manifest.json")
        sources_before = source_fingerprints(args.source_root)
        capture_hash = file_hash(args.capture)
        report.update({"baseline_manifest": manifest, "baseline_manifest_sha256": manifest_hash,
                       "modules": modules_before, "source_fingerprints": sources_before,
                       "capture_executable": {"path": str(args.capture), "sha256": capture_hash},
                       "build": build_provenance(args.capture), "observed_current_head": read_git(args.source_root, "rev-parse", "HEAD")})
        if args.sdk_root is not None:
            report["sdk"] = {"path": str(args.sdk_root), "revision": read_git(args.sdk_root, "rev-parse", "HEAD"),
                             "submodules": read_git(args.sdk_root, "submodule", "status")}
        with ThreadPoolExecutor(max_workers=3) as pool:
            for key in product(VARIANTS, FORMATS, RATES, PARTITIONS, WORKLOADS):
                futures = [pool.submit(one_capture, args.capture,
                                       args.baseline_root if mode == "baseline" else args.current_root,
                                       run_directory, key, mode, args.timeout) for mode in MODES]
                captures = [future.result() for future in futures]
                for mode, result in zip(MODES, captures):
                    runs[(mode,) + key] = result
                    report["captures"].append(result[0])
                pairs = [compare_pair(captures[0], current) for current in captures[1:]]
                variant, format_bits, rate, partition, workload = key
                case = {"variant": variant, "format_bits": format_bits, "sample_rate_hz": rate,
                        "partition": partition, "gain_workload": workload, "comparisons": pairs,
                        "passed": all(pair["passed"] for pair in pairs)}
                report["cases"].append(case)
                print(f"{'PASS' if case['passed'] else 'FAIL'} {variant} f{format_bits} {rate} {partition} {workload}", flush=True)
        for mode, variant, format_bits, rate in product(MODES, VARIANTS, FORMATS, RATES):
            selected = [runs[(mode, variant, format_bits, rate, partition, "dense")] for partition in PARTITIONS]
            passed = all(record["passed"] for record, _, _ in selected) and len({record["audio_sha256"] for record, _, _ in selected}) == 1
            report["dense_partition_comparisons"].append({"mode": mode, "variant": variant, "format_bits": format_bits,
                                                         "sample_rate_hz": rate, "three_partitions_exact": passed})
        modules_after = {"baseline": bundle_inventory(args.baseline_root), "current": bundle_inventory(args.current_root)}
        unchanged = (modules_before == modules_after and sources_before == source_fingerprints(args.source_root)
                     and capture_hash == file_hash(args.capture) and manifest_hash == file_hash(args.baseline_root / "manifest.json"))
        complete = (len(report["captures"]) == EXPECTED_CAPTURES and len(report["cases"]) == EXPECTED_FIXTURES
                    and sum(len(case["comparisons"]) for case in report["cases"]) == EXPECTED_PAIRS
                    and len(report["dense_partition_comparisons"]) == EXPECTED_PARTITION_GROUPS)
        passed = (complete and unchanged and all(record["passed"] for record in report["captures"])
                  and all(case["passed"] for case in report["cases"])
                  and all(group["three_partitions_exact"] for group in report["dense_partition_comparisons"]))
        report["summary"] = {
            "passed": passed, "complete": complete, "fixtures": len(report["cases"]), "expected_fixtures": EXPECTED_FIXTURES,
            "loaded_processes": len(report["captures"]), "expected_loaded_processes": EXPECTED_CAPTURES,
            "valid_captures": sum(record["passed"] for record in report["captures"]),
            "baseline_current_pairs": EXPECTED_PAIRS,
            "passed_pairs": sum(pair["passed"] for case in report["cases"] for pair in case["comparisons"]),
            "paired_stereo_frames": EXPECTED_PAIRS * TOTAL_FRAMES,
            "loaded_callbacks_total": sum(record.get("callbacks", 0) for record in report["captures"]),
            "dense_partition_groups": len(report["dense_partition_comparisons"]),
            "dense_partition_groups_passed": sum(group["three_partitions_exact"] for group in report["dense_partition_comparisons"]),
            "modules_recorder_manifest_and_relevant_sources_unchanged": unchanged,
        }
        report["status"] = "passed" if passed else "failed"
    except (OSError, ValueError, subprocess.SubprocessError) as error:
        report["status"] = "failed"
        report["error"] = str(error)
        print(f"FAIL: {error}", file=sys.stderr, flush=True)
    finally:
        report["completed_utc"] = utc_now()
        write_json(run_directory / "commands.json", report["captures"])
        raw_manifest = {"schema_version": 1, "run_id": run_id, "files": {
            path.name: {"bytes": path.stat().st_size, "sha256": file_hash(path)}
            for path in sorted(run_directory.iterdir()) if path.is_file() and path.name not in ("report.json", "manifest.json")}}
        write_json(run_directory / "manifest.json", raw_manifest)
        report["raw_manifest_sha256"] = file_hash(run_directory / "manifest.json")
        report["raw_manifest_file_count"] = len(raw_manifest["files"])
        report["raw_records_match_manifest"] = raw_records_match_manifest(report["captures"], raw_manifest)
        if report["status"] == "passed" and not report["raw_records_match_manifest"]:
            report["status"] = "failed"
            report["summary"]["passed"] = False
            report["error"] = "retained raw files differ from the bytes compared or contain unexpected records"
        write_json(run_directory / "report.json", report)
        write_json(args.output_dir / "report.json", report)
    print(json.dumps(report["summary"], indent=2, sort_keys=True), flush=True)
    print(f"Report: {args.output_dir / 'report.json'}", flush=True)
    return 0 if report["status"] == "passed" else 1


if __name__ == "__main__":
    raise SystemExit(main())
