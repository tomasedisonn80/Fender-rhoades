#!/usr/bin/env python3
"""Freeze and execute bounded, matched quadrature callback observations.

First use --plan (which needs neither benchmark binaries nor third-party Python
packages), retain its stdout, then use --run-plan FILE --output-dir NEW_DIR.
Every profile/cycle runs baseline, candidate, candidate, baseline. No callback,
outlier, unsuccessful attempt, or nominal-period miss is discarded or retried.
The measurements and candidate/baseline ratios are observational, not WCRT.
"""
from __future__ import annotations

import argparse
from datetime import datetime, timezone
import hashlib
import json
import math
import os
from pathlib import Path
import platform
import signal
import stat
import statistics
import struct
import subprocess
import sys
import threading
import time


ROOT = Path(__file__).resolve().parents[1]
SCRIPT = Path(__file__).resolve()
PROTOCOL = "matched-quadrature-callbacks-abba-v1"
MACHINE_LABEL = "matched-q6-callbacks-abba-v1"
MAX_BLOCKS = 1024
MAX_WARMUP = 64
MAX_CYCLES = 4
MAX_TIMEOUT_SECONDS = 120
MAX_PROFILES = 12
MAX_RUNS = MAX_PROFILES * MAX_CYCLES * 4
MAX_PLAN_BYTES = 2 * 1024 * 1024
MAX_STDOUT_BYTES = 256 * 1024
MAX_STDERR_BYTES = 64 * 1024
MAX_FILE_BYTES = 16 * 1024 * 1024
MAX_CAPTURE_BYTES = 128 * 1024 * 1024
FINAL_RESERVE_BYTES = 8 * 1024 * 1024
MAX_SOURCE_FILES = 4096
MAX_SOURCE_BYTES = 128 * 1024 * 1024
LIMITS = [
    "Observational matched candidate/baseline ratios; no causal speedup or statistical independence is established.",
    "Every callback and outlier is retained. Observed maxima are not worst-case execution-time (WCRT) bounds.",
    "Nominal block-period misses are not measured DAW/audio-device underruns or realtime guarantees.",
    "Only concrete RhodesProcessor::process calls are timed; launch, setup, warmup, checks and reporting are excluded.",
    "Synthetic Q6 model, six nodes and 48-frame latency; no physical or perceptual fidelity is established.",
    "No allocation, locking, native-platform coverage, timer-resolution or long-tail guarantee follows from this experiment.",
    "Current source hashes do not reconstruct a historical baseline. Retained provenance and embedded compiler fields are claims with their original scope.",
    "No build, priority change, power-policy change, pacing, overhead subtraction, adaptive repetition or automatic retry is performed.",
    "Default affinity is inherited and observed where supported; no CPU is silently selected or assumed.",
    "A zero denominator produces a null ratio with an explicit reason, never an invented zero or infinity.",
]

# Fixed representative subset, not a full factorial matrix. Held profiles share
# sample format within each 0/1/12/73-key comparison. Stress pairs also cover f32.
PROFILE_ROWS = (
    ("p01", 48000, 64, 0, "held", 64),
    ("p02", 48000, 64, 1, "held", 64),
    ("p03", 48000, 64, 12, "held", 64),
    ("p04", 48000, 64, 73, "held", 64),
    ("p05", 192000, 256, 0, "held", 64),
    ("p06", 192000, 256, 1, "held", 64),
    ("p07", 192000, 256, 12, "held", 64),
    ("p08", 192000, 256, 73, "held", 64),
    ("p09", 48000, 256, 12, "pedal", 64),
    ("p10", 192000, 64, 73, "pedal", 32),
    ("p11", 192000, 64, 12, "gain", 32),
    ("p12", 48000, 256, 73, "gain", 64),
)
PROFILE_IDS = [row[0] for row in PROFILE_ROWS]
METRICS = ("p50_us", "p99_us", "max_us", "mean_us")
BUILD_METADATA_NAMES = (
    "CMakeCache.txt", "compile_commands.json", "build.ninja", "CMakeFiles/rules.ninja",
    "CMakeFiles/rhodes_vst3_quadrature_benchmark.dir/flags.make",
    "CMakeFiles/rhodes_vst3_quadrature_benchmark.dir/link.txt",
    "vst3-benchmark-manifest.json", "manifest.json",
)


def utc():
    return datetime.now(timezone.utc).isoformat()


def canonical(value):
    return json.dumps(value, ensure_ascii=True, sort_keys=True,
                      separators=(",", ":"), allow_nan=False).encode("utf-8")


def digest(value):
    return hashlib.sha256(value).hexdigest()


def require(condition, message):
    if not condition:
        raise ValueError(message)


def stable_stat(info):
    # Reading may legitimately advance atime. Content/identity metadata must not
    # change, but an access-time update is not an input mutation.
    return (info.st_mode, info.st_dev, info.st_ino, info.st_size,
            info.st_mtime_ns, info.st_ctime_ns)


def strict_json(raw):
    """Reject duplicate keys, non-finite numbers, trailing text and bad UTF-8."""
    def pairs(items):
        result = {}
        for key, value in items:
            require(key not in result, "duplicate JSON key: " + key)
            result[key] = value
        return result

    def reject_constant(value):
        raise ValueError("non-finite JSON constant: " + value)

    def finite_float(value):
        number = float(value)
        require(math.isfinite(number), "non-finite JSON number")
        return number

    try:
        return json.loads(raw.decode("utf-8"), object_pairs_hook=pairs,
                          parse_constant=reject_constant, parse_float=finite_float)
    except (UnicodeError, json.JSONDecodeError, RecursionError) as error:
        raise ValueError("malformed JSON: " + str(error)) from error


def bounded_read(path, limit=MAX_FILE_BYTES):
    path = Path(path)
    info = path.lstat()
    require(stat.S_ISREG(info.st_mode), "not a regular file: " + str(path))
    require(info.st_size <= limit, "file exceeds byte cap: " + str(path))
    with path.open("rb") as stream:
        raw = stream.read(limit + 1)
    require(len(raw) <= limit, "file grew beyond byte cap: " + str(path))
    require(stable_stat(path.lstat()) == stable_stat(info), "file changed while reading: " + str(path))
    return raw


def file_identity(path):
    path = Path(path)
    info = path.lstat()
    require(stat.S_ISREG(info.st_mode), "not a regular file: " + str(path))
    require(info.st_size <= MAX_SOURCE_BYTES, "input exceeds byte cap: " + str(path))
    h = hashlib.sha256()
    consumed = 0
    with path.open("rb") as stream:
        while block := stream.read(1024 * 1024):
            consumed += len(block)
            require(consumed <= MAX_SOURCE_BYTES, "input grew beyond byte cap")
            h.update(block)
    require(stable_stat(path.lstat()) == stable_stat(info), "input changed while hashing: " + str(path))
    return {"sha256": h.hexdigest(), "bytes": consumed, "mode": info.st_mode,
            "device": info.st_dev, "inode": info.st_ino,
            "mtime_ns": info.st_mtime_ns, "ctime_ns": info.st_ctime_ns}


def checked_int(value, minimum, maximum, name):
    require(type(value) is int and minimum <= value <= maximum,
            f"{name} must be an integer in {minimum}..{maximum}")
    return value


def make_plan(*, baseline, candidate, profile_ids=None, blocks=256,
              warmup_blocks=16, cycles=2, timeout_seconds=60, cpu=None):
    """Return a deterministic frozen plan; never inspect/execute binaries."""
    checked_int(blocks, 1, MAX_BLOCKS, "blocks")
    checked_int(warmup_blocks, 0, MAX_WARMUP, "warmup_blocks")
    checked_int(cycles, 1, MAX_CYCLES, "cycles")
    checked_int(timeout_seconds, 1, MAX_TIMEOUT_SECONDS, "timeout_seconds")
    if cpu is not None:
        checked_int(cpu, 0, 1048575, "cpu")
    if profile_ids is None:
        profile_ids = list(PROFILE_IDS)
    require(type(profile_ids) is list and 1 <= len(profile_ids) <= MAX_PROFILES,
            "profile_ids must be a nonempty bounded list")
    require(all(type(p) is str and p in PROFILE_IDS for p in profile_ids),
            "unknown profile ID")
    require(len(set(profile_ids)) == len(profile_ids), "duplicate profile ID")
    profile_ids = [p for p in PROFILE_IDS if p in profile_ids]
    binaries = {}
    for label, value in (("baseline", baseline), ("candidate", candidate)):
        require(isinstance(value, (str, Path)) and bool(str(value)), "empty binary path")
        require("\x00" not in str(value), "NUL in binary path")
        binaries[label] = str(Path(value).absolute())
    profiles = []
    for pid, rate, frames, keys, workload, bits in PROFILE_ROWS:
        if pid in profile_ids:
            profiles.append({"id": pid, "sample_rate_hz": rate, "block_frames": frames,
                             "active_keys": keys, "workload": workload,
                             "sample_format_bits": bits, "measured_blocks": blocks,
                             "warmup_blocks": warmup_blocks})
    runs = []
    for cycle in range(1, cycles + 1):
        ordered_profiles = profiles if cycle % 2 else list(reversed(profiles))
        for profile in ordered_profiles:
            for position, label in enumerate(("baseline", "candidate", "candidate", "baseline"), 1):
                command = [binaries[label], "--blocks", str(blocks), "--warmup-blocks",
                           str(warmup_blocks), "--block-size", str(profile["block_frames"]),
                           "--sample-rate", str(profile["sample_rate_hz"]), "--active-keys",
                           str(profile["active_keys"]), "--workload", profile["workload"],
                           "--sample-format", str(profile["sample_format_bits"]),
                           "--machine", MACHINE_LABEL]
                runs.append({"id": f"c{cycle:02d}-{profile['id']}-{position}-{label}",
                             "cycle": cycle, "profile_id": profile["id"],
                             "position": position, "variant": label, "argv": command})
    require(len(runs) <= MAX_RUNS, "run cap exceeded")
    settings = {**binaries, "profile_ids": profile_ids, "blocks": blocks,
                "warmup_blocks": warmup_blocks, "cycles": cycles,
                "timeout_seconds": timeout_seconds, "cpu": cpu}
    return {"schema_version": 1, "protocol": PROTOCOL, "status": "planned_not_executed",
            "harness_sha256": digest(bounded_read(SCRIPT)), "settings": settings,
            "profiles": profiles, "runs": runs,
            "ordering": "ABBA within each profile/cycle; profile order reverses on even cycles",
            "analysis": "Adjacent pairs (positions 1,2) and (4,3); candidate/baseline per-run statistics; all raw durations retained",
            "affinity": {"requested_cpu": cpu, "policy": "inherited" if cpu is None else "explicit Linux single CPU; verify before exec"},
            "caps": {"profiles": MAX_PROFILES, "runs": MAX_RUNS, "blocks_per_run": MAX_BLOCKS,
                     "warmup_blocks_per_run": MAX_WARMUP, "cycles": MAX_CYCLES,
                     "timeout_seconds_per_run": MAX_TIMEOUT_SECONDS,
                     "stdout_bytes_per_run": MAX_STDOUT_BYTES,
                     "stderr_bytes_per_run": MAX_STDERR_BYTES,
                     "capture_bytes": MAX_CAPTURE_BYTES},
            "totals": {"profiles": len(profiles), "process_runs": len(runs),
                       "matched_pairs": len(runs) // 2,
                       "measured_callbacks": len(runs) * blocks,
                       "warmup_callbacks": len(runs) * warmup_blocks,
                       "measured_host_frames": sum(p["block_frames"] * blocks * cycles * 4 for p in profiles),
                       "sum_process_timeout_budgets_seconds": len(runs) * timeout_seconds},
            "limits": LIMITS}


def load_plan(path):
    raw = bounded_read(path, MAX_PLAN_BYTES)
    plan = strict_json(raw)
    require(type(plan) is dict and type(plan.get("settings")) is dict, "invalid plan object")
    try:
        expected = make_plan(**plan["settings"])
    except TypeError as error:
        raise ValueError("invalid plan settings: " + str(error)) from error
    require(canonical(plan) == canonical(expected),
            "plan differs from this harness's frozen schema, commands, caps or hash; regenerate and review it")
    return plan, raw


def timing_statistics(values, deadline_us):
    require(type(values) is list and 1 <= len(values) <= MAX_BLOCKS,
            "raw_callback_us must be a nonempty bounded array")
    for value in values:
        require(type(value) in (int, float) and math.isfinite(value) and value >= 0,
                "callback duration must be finite and nonnegative, not bool/string/null")
    ordered = sorted(values)
    total = 0.0
    for value in ordered:  # Match the native sorted binary64 accumulation exactly.
        total += value
    require(math.isfinite(total), "callback duration sum overflowed")
    result = {"observations": len(values), "mean_us": total / len(values),
              "max_us": ordered[-1], "deadline_us": deadline_us,
              "deadline_misses": sum(value > deadline_us for value in values)}
    for percentile in (50, 95, 99):
        result[f"p{percentile}_us"] = ordered[(len(values) * percentile + 99) // 100 - 1]
    return result


def validate_benchmark(raw, profile):
    """Strict independently derived schema/workload/statistics verification."""
    require(type(raw) is dict, "benchmark JSON must be an object")

    def obj(value, fields, label):
        require(type(value) is dict, label + " must be an object")
        require(set(value) == set(fields), label + " has missing or unexpected fields")
        return value

    def equal(actual, expected, label):
        if type(expected) is float:
            require(type(actual) in (int, float) and math.isfinite(actual), label + " must be finite numeric")
        else:
            require(type(actual) is type(expected), label + " has the wrong JSON type")
        require(actual == expected, label + " differs from frozen expectation")

    def string(value, label):
        require(type(value) is str and 0 < len(value) <= 8192, label + " must be a bounded nonempty string")

    obj(raw, ("schema_version", "benchmark", "complete", "identity", "environment", "build",
              "workload", "timing", "integrity", "limits"), "root")
    equal(raw["schema_version"], 1, "schema_version")
    equal(raw["benchmark"], "synthetic_vst3_process", "benchmark")
    equal(raw["complete"], True, "complete")
    identity = obj(raw["identity"], ("source_revision", "source_diff_hash", "plugin_name",
                   "state_model", "latency_frames", "quadrature_nodes", "observation_scheme",
                   "timing_scope"), "identity")
    for key in ("source_revision", "source_diff_hash"):
        string(identity[key], "identity." + key)
    for key, value in {"plugin_name": "Rhodes Research Quadrature", "state_model": 3,
                       "latency_frames": 48, "quadrature_nodes": 6,
                       "observation_scheme": "gauss-legendre-host-cell",
                       "timing_scope": "RhodesProcessor::process including host getters, validation, ownership, automation, output clearing, rendering and mailbox publication"}.items():
        equal(identity[key], value, "identity." + key)
    environment = obj(raw["environment"], ("machine_label", "platform", "architecture",
                      "cpu_model", "kernel", "power_policy", "affinity", "priority",
                      "competing_load", "floating_point_rounding_code", "flush_to_zero"), "environment")
    equal(environment["machine_label"], MACHINE_LABEL, "environment.machine_label")
    for key in ("platform", "architecture", "affinity", "priority", "competing_load", "flush_to_zero"):
        string(environment[key], "environment." + key)
    for key in ("cpu_model", "kernel", "power_policy"):
        equal(environment[key], None, "environment." + key)
    require(type(environment["floating_point_rounding_code"]) is int,
            "environment.floating_point_rounding_code must be integer")
    build = raw["build"]
    require(type(build) is dict, "build must be an object")
    fields = {"configuration", "compiler", "complete_flags", "ndebug", "fast_math_macro", "sanitizer", "lto"}
    if "msvc_full_version" in build:
        fields.add("msvc_full_version")
        checked_int(build["msvc_full_version"], 1, 2**63 - 1, "build.msvc_full_version")
    obj(build, fields, "build")
    for key in ("configuration", "compiler", "complete_flags", "sanitizer", "lto"):
        string(build[key], "build." + key)
    for key in ("ndebug", "fast_math_macro"):
        require(type(build[key]) is bool, "build." + key + " must be boolean")
    blocks, warmup = profile["measured_blocks"], profile["warmup_blocks"]
    frames, rate, keys = profile["block_frames"], profile["sample_rate_hz"], profile["active_keys"]
    workload = profile["workload"]
    interval = (rate + 2 * frames - 1) // (2 * frames)
    notes = ((blocks - 1) // interval + 1) * keys * (2 if workload == "pedal" else 1)
    points = 0 if workload == "held" else blocks * frames
    owners = 0 if workload == "pedal" else keys
    nonzero_expected = keys > 0 and blocks * frames > 1
    expected_workload = {
        "id": workload, "sample_format_bits": profile["sample_format_bits"],
        "sample_rate_hz": rate, "block_frames": frames, "requested_blocks": blocks,
        "measured_blocks": blocks, "requested_warmup_blocks": warmup,
        "completed_warmup_blocks": warmup, "selected_key_count": keys,
        "retrigger_interval_blocks": interval, "offered_note_events": notes,
        "offered_parameter_points": points, "initial_normalized_gain": 0.5,
        "initial_sustain": False,
        "note_intensity_binary32": struct.unpack("f", struct.pack("f", 0.7))[0]}
    obj(raw["workload"], expected_workload, "workload")
    for key, value in expected_workload.items():
        equal(raw["workload"][key], value, "workload." + key)
    expected_integrity = {"warmup_success": True, "successful_callbacks": blocks,
        "failed_callbacks": 0, "last_result_code": 0, "frame_cursor": blocks * frames,
        "active_note_owners": owners, "expected_note_owners": owners,
        "note_getter_reads": notes, "parameter_point_getter_reads": points,
        "finite_output_and_checksum": True, "stereo_equal": True,
        "silence_flags_correct": True, "nonzero_output_expected": nonzero_expected,
        "output_presence_correct": True,
        "allocation_evidence": "not measured by this executable"}
    integrity = obj(raw["integrity"], set(expected_integrity) | {"nonzero_channel_samples",
                    "checksum_sum_squares", "peak_abs_digital_output", "output_fnv1a64"}, "integrity")
    for key, value in expected_integrity.items():
        equal(integrity[key], value, "integrity." + key)
    count = checked_int(integrity["nonzero_channel_samples"], 0, 2 * blocks * frames,
                        "integrity.nonzero_channel_samples")
    require((count > 0) == nonzero_expected and count % 2 == 0,
            "nonzero sample count contradicts workload or stereo identity")
    for key in ("checksum_sum_squares", "peak_abs_digital_output"):
        value = integrity[key]
        require(type(value) in (int, float) and math.isfinite(value) and value >= 0,
                "integrity." + key + " must be finite and nonnegative")
        require((value > 0) == nonzero_expected, "integrity." + key + " contradicts output presence")
    fingerprint = integrity["output_fnv1a64"]
    require(type(fingerprint) is str and len(fingerprint) == 16 and
            all(c in "0123456789abcdef" for c in fingerprint), "invalid output_fnv1a64")
    timing = obj(raw["timing"], ("clock", "percentile_rule", "summary", "raw_callback_us"), "timing")
    equal(timing["clock"], "std::chrono::steady_clock", "timing.clock")
    equal(timing["percentile_rule"], "nearest rank, one-based ceil(N*p/100)", "timing.percentile_rule")
    summary = timing_statistics(timing["raw_callback_us"], 1e6 * frames / rate)
    equal(len(timing["raw_callback_us"]), blocks, "timing.raw_callback_us length")
    obj(timing["summary"], summary, "timing.summary")
    for key, value in summary.items():
        if key not in ("observations", "deadline_misses"):
            value = float(value)
        equal(timing["summary"][key], value, "timing.summary." + key)
    require(type(raw["limits"]) is list and 1 <= len(raw["limits"]) <= 64,
            "limits must be a nonempty bounded array")
    for item in raw["limits"]:
        string(item, "limits item")
    return {"valid": True, "summary": summary,
            "mean_fsum_us": math.fsum(timing["raw_callback_us"]) / blocks,
            "output_identity": {k: integrity[k] for k in ("nonzero_channel_samples",
                "checksum_sum_squares", "peak_abs_digital_output", "output_fnv1a64",
                "finite_output_and_checksum", "stereo_equal", "silence_flags_correct",
                "nonzero_output_expected", "output_presence_correct")},
            "workload": raw["workload"], "identity": identity,
            "build": build, "environment": environment}


class EvidenceStore:
    """Exclusive writes, bounded total bytes and final retained-byte checks."""
    def __init__(self, directory):
        self.directory = directory
        self.used = 0
        self.lock = threading.Lock()
        self.artifacts = {}

    def reserve(self, size, final=False):
        with self.lock:
            cap = MAX_CAPTURE_BYTES if final else MAX_CAPTURE_BYTES - FINAL_RESERVE_BYTES
            require(self.used + size <= cap, "capture byte cap reached")
            self.used += size

    def register(self, name):
        identity = file_identity(self.directory / name)
        with self.lock:
            self.artifacts[name] = {"bytes": identity["bytes"], "sha256": identity["sha256"]}

    def write(self, name, raw, final=False):
        require(len(raw) <= MAX_FILE_BYTES, "artifact exceeds file byte cap: " + name)
        self.reserve(len(raw), final)
        path = self.directory / name
        path.parent.mkdir(parents=True, exist_ok=True)
        with path.open("xb") as stream:
            stream.write(raw)
        self.register(name)

    def json(self, name, value, final=False):
        self.write(name, json.dumps(value, indent=2, ensure_ascii=True,
                                   allow_nan=False).encode("utf-8") + b"\n", final)

    def verify(self):
        for name, expected in self.artifacts.items():
            actual = file_identity(self.directory / name)
            require({"bytes": actual["bytes"], "sha256": actual["sha256"]} == expected,
                    "retained artifact mutated: " + name)
        actual_names = {str(p.relative_to(self.directory)) for p in self.directory.rglob("*") if not p.is_dir()}
        require(actual_names == set(self.artifacts), "unexpected or missing retained artifact")


def affinity_snapshot():
    if sys.platform != "linux" or not hasattr(os, "sched_getaffinity"):
        return {"status": "unsupported", "platform": sys.platform, "cpus": None}
    try:
        cpus = sorted(os.sched_getaffinity(0))
        require(bool(cpus), "empty permitted CPU mask")
        return {"status": "observed", "platform": sys.platform, "cpus": cpus}
    except (OSError, ValueError) as error:
        return {"status": "unavailable", "platform": sys.platform, "cpus": None, "error": str(error)}


def optional_text(path, limit=1024 * 1024):
    try:
        with Path(path).open("rb") as stream:
            raw = stream.read(limit + 1)
        if len(raw) > limit:
            return {"status": "unavailable", "error": "metadata byte cap exceeded"}
        return {"status": "observed", "text": raw.decode("utf-8", errors="replace")}
    except OSError as error:
        return {"status": "unavailable", "error": str(error)}


def machine_snapshot():
    paths = ["/proc/cpuinfo", "/proc/meminfo", "/proc/loadavg", "/proc/stat",
             "/proc/self/cgroup", "/proc/self/status", "/etc/os-release",
             "/sys/fs/cgroup/cpu.max", "/sys/fs/cgroup/cpu.stat",
             "/sys/fs/cgroup/cpuset.cpus.effective",
             "/sys/devices/system/cpu/intel_pstate/no_turbo",
             "/sys/devices/system/cpu/cpufreq/boost"]
    affinity = affinity_snapshot()
    for cpu in (affinity.get("cpus") or [])[:256]:
        paths.append(f"/sys/devices/system/cpu/cpu{cpu}/cpufreq/scaling_governor")
    return {"utc": utc(), "monotonic_ns": time.monotonic_ns(),
            "platform": platform.platform(), "uname": list(platform.uname()),
            "python": sys.version, "python_executable": sys.executable,
            "logical_cpu_count": os.cpu_count(), "processor_label": platform.processor(),
            "affinity": affinity, "files": {name: optional_text(name) for name in paths},
            "priority_policy": "inherited; no priority or power policy change",
            "steady_clock_resolution": "not independently probed; do not infer from microsecond output units"}


def source_snapshot():
    paths = {ROOT / "CMakeLists.txt", ROOT / "apps/vst3_benchmark.cpp", SCRIPT}
    for directory in ("include", "src", "plugins/vst3", "cmake"):
        base = ROOT / directory
        require(base.is_dir(), "missing source directory: " + directory)
        paths.update(path for path in base.rglob("*") if not path.is_dir())
    require(len(paths) <= MAX_SOURCE_FILES, "source file count cap exceeded")
    entries = {}
    total = 0
    for path in sorted(paths):
        entry = file_identity(path)
        total += entry["bytes"]
        require(total <= MAX_SOURCE_BYTES, "aggregate source byte cap exceeded")
        entries[str(path.relative_to(ROOT))] = entry
    return {"files": entries, "sha256": digest(canonical(entries)), "bytes": total,
            "scope": "working-tree CMakeLists, benchmark, include, src, plugins/vst3, cmake and this harness; excludes historical baseline reconstruction and SDK source"}


def input_snapshot(plan, plan_path):
    binaries = {label: file_identity(plan["settings"][label]) for label in ("baseline", "candidate")}
    for label in binaries:
        require(os.access(plan["settings"][label], os.X_OK), "binary is not executable: " + label)
    build = {}
    for label in binaries:
        directory = Path(plan["settings"][label]).parent
        build[label] = {name: file_identity(directory / name) if os.path.lexists(directory / name) else None
                        for name in BUILD_METADATA_NAMES}
    return {"source": source_snapshot(), "binaries": binaries, "build_metadata": build,
            "plan_file": file_identity(plan_path)}


def retain_build_metadata(store, plan):
    result = {}
    for label in ("baseline", "candidate"):
        directory = Path(plan["settings"][label]).parent
        entries = {}
        for name in BUILD_METADATA_NAMES:
            path = directory / name
            if not os.path.lexists(path):
                entries[name] = {"status": "unavailable", "reason": "file absent; not inferred"}
                continue
            raw = bounded_read(path)
            retained = f"build/{label}/{name}"
            store.write(retained, raw)
            entries[name] = {"status": "retained", "path": str(path), "artifact": retained,
                             "sha256": digest(raw), "bytes": len(raw)}
        result[label] = entries
    store.json("build-metadata.json", result)
    return result


def child_main(argv):
    """Private exec wrapper: report the same Linux process's actual affinity."""
    require(len(argv) >= 3, "invalid private child invocation")
    record, cpu_text, *command = argv
    before = affinity_snapshot()
    error = None
    try:
        if cpu_text != "inherit":
            cpu = int(cpu_text)
            require(before["status"] == "observed" and hasattr(os, "sched_setaffinity"),
                    "explicit CPU affinity requires observed Linux support")
            require(cpu in before["cpus"], "requested CPU is not permitted")
            os.sched_setaffinity(0, {cpu})
            require(affinity_snapshot().get("cpus") == [cpu], "CPU affinity was not applied")
    except (OSError, ValueError) as caught:
        error = str(caught)
    value = {"utc": utc(), "pid": os.getpid(), "exec_argv": command,
             "requested_cpu": None if cpu_text == "inherit" else int(cpu_text),
             "before": before, "after": affinity_snapshot(), "error": error,
             "observation_scope": "wrapper process immediately before exec; Linux exec preserves process affinity"}
    if hasattr(os, "getpriority"):
        value["inherited_nice"] = os.getpriority(os.PRIO_PROCESS, 0)
    if hasattr(os, "sched_getscheduler"):
        value["inherited_scheduler"] = os.sched_getscheduler(0)
    with Path(record).open("x", encoding="utf-8") as stream:
        json.dump(value, stream, indent=2, allow_nan=False)
        stream.write("\n")
    if error:
        raise ValueError(error)
    os.execv(command[0], command)
    return 3  # execv either replaces this process or raises.


def run_process(store, run, settings):
    prefix = "runs/" + run["id"]
    record_name = prefix + ".affinity.json"
    record_path = store.directory / record_name
    command = [sys.executable, str(SCRIPT), "_child", str(record_path),
               "inherit" if settings["cpu"] is None else str(settings["cpu"]), *run["argv"]]
    execution = {**run, "wrapper_argv": command, "cwd": str(ROOT), "started_utc": utc(),
                 "timeout_seconds": settings["timeout_seconds"], "returncode": None,
                 "timed_out": False, "errors": [], "streams": {}}
    exceeded = threading.Event()
    threads = []
    process = None
    started = time.monotonic()

    def drain(pipe, name, limit, stream_label):
        info = {"artifact": name, "byte_cap": limit, "observed_bytes": 0,
                "retained_bytes": 0, "truncated": False, "errors": []}
        execution["streams"][stream_label] = info
        try:
            with (store.directory / name).open("xb") as destination:
                while chunk := pipe.read(65536):
                    info["observed_bytes"] += len(chunk)
                    keep = chunk[:max(0, limit - info["retained_bytes"])]
                    if keep:
                        store.reserve(len(keep))
                        destination.write(keep)
                        info["retained_bytes"] += len(keep)
                    if len(keep) != len(chunk):
                        info["truncated"] = True
                        exceeded.set()
        except (OSError, ValueError) as error:
            info["errors"].append(str(error))
            exceeded.set()
        finally:
            pipe.close()
            try:
                if (store.directory / name).exists():
                    store.register(name)
            except (OSError, ValueError) as error:
                info["errors"].append("cannot hash retained stream: " + str(error))
                exceeded.set()

    def kill():
        if process is not None:
            try:
                if os.name == "posix":
                    os.killpg(process.pid, signal.SIGKILL)
                else:
                    process.kill()
            except ProcessLookupError:
                pass

    (store.directory / "runs").mkdir(exist_ok=True)
    try:
        process = subprocess.Popen(command, cwd=ROOT, stdin=subprocess.DEVNULL,
                                   stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                                   start_new_session=os.name == "posix")
        for pipe, suffix, limit, label in ((process.stdout, ".stdout.json", MAX_STDOUT_BYTES, "stdout"),
                                         (process.stderr, ".stderr.txt", MAX_STDERR_BYTES, "stderr")):
            thread = threading.Thread(target=drain, args=(pipe, prefix + suffix, limit, label), daemon=True)
            thread.start()
            threads.append(thread)
        while process.poll() is None:
            if exceeded.is_set():
                execution["errors"].append("stream/capture cap or write failure; retained bytes are incomplete")
                kill()
                break
            remaining = settings["timeout_seconds"] - (time.monotonic() - started)
            if remaining <= 0:
                execution["timed_out"] = True
                execution["errors"].append("subprocess timeout; no retry")
                kill()
                break
            try:
                process.wait(timeout=min(0.05, remaining))
            except subprocess.TimeoutExpired:
                pass
    except (OSError, KeyboardInterrupt) as error:
        execution["errors"].append(type(error).__name__ + ": " + str(error))
        kill()
    finally:
        if process is not None:
            try:
                execution["returncode"] = process.wait(timeout=2)
            except subprocess.TimeoutExpired:
                execution["errors"].append("subprocess did not exit after kill")
            for thread in threads:
                thread.join(timeout=2)
                if thread.is_alive():
                    kill()
                    execution["errors"].append("stream writer did not finish; capture incomplete")
        execution["finished_utc"] = utc()
        execution["whole_process_elapsed_seconds"] = time.monotonic() - started
        if execution["whole_process_elapsed_seconds"] > settings["timeout_seconds"] and not execution["timed_out"]:
            execution["timed_out"] = True
            execution["errors"].append("completion was observed after subprocess timeout; cannot certify timely completion")
    for label, suffix in (("stdout", ".stdout.json"), ("stderr", ".stderr.txt")):
        name = prefix + suffix
        if not (store.directory / name).exists():
            store.write(name, b"")
            execution["streams"][label] = {"artifact": name, "retained_bytes": 0,
                                            "errors": ["process stream was never opened"], "truncated": False}
    try:
        if record_path.exists():
            raw_affinity = bounded_read(record_path, MAX_STDERR_BYTES)
            store.reserve(len(raw_affinity))
            store.register(record_name)
            execution["child_affinity"] = strict_json(raw_affinity)
        else:
            execution["errors"].append("missing child affinity metadata")
    except (OSError, ValueError) as error:
        execution["errors"].append("invalid child affinity metadata: " + str(error))
    return execution


def ratio(numerator, denominator):
    if denominator == 0:
        return {"value": None, "reason": "zero baseline denominator"}
    value = numerator / denominator
    require(math.isfinite(value), "matched ratio overflowed")
    return {"value": value, "reason": None}


def matched_comparison(plan, records):
    require(len(records) == len(plan["runs"]) and all(r.get("valid") is True for r in records),
            "comparison requires every planned run to verify")
    by_id = {record["id"]: record for record in records}
    require(len(by_id) == len(records), "duplicate execution ID")
    pairs = []
    for cycle in range(1, plan["settings"]["cycles"] + 1):
        for profile in plan["profiles"]:
            pid = profile["id"]
            for a, b in ((1, 2), (4, 3)):
                baseline = by_id[f"c{cycle:02d}-{pid}-{a}-baseline"]
                candidate = by_id[f"c{cycle:02d}-{pid}-{b}-candidate"]
                require(canonical(baseline["validation"]["workload"]) == canonical(candidate["validation"]["workload"]),
                        "matched workload identity changed")
                require(canonical(baseline["validation"]["output_identity"]) == canonical(candidate["validation"]["output_identity"]),
                        "matched output identity changed")
                bs, cs = baseline["validation"]["summary"], candidate["validation"]["summary"]
                pairs.append({"profile_id": pid, "cycle": cycle, "baseline_run": baseline["id"],
                              "candidate_run": candidate["id"],
                              "candidate_over_baseline": {key: ratio(cs[key], bs[key]) for key in METRICS},
                              "deadline_misses": {"baseline": bs["deadline_misses"], "candidate": cs["deadline_misses"],
                                                  "candidate_over_baseline": ratio(cs["deadline_misses"], bs["deadline_misses"])}})
    profiles = []
    for profile in plan["profiles"]:
        pid = profile["id"]
        selected = [pair for pair in pairs if pair["profile_id"] == pid]
        profile_summary = {"profile_id": pid, "matched_pairs": len(selected), "ratios": {}, "observations": {}}
        for key in METRICS:
            values = [pair["candidate_over_baseline"][key]["value"] for pair in selected]
            profile_summary["ratios"][key] = {"all_pair_ratios": values,
                "median_pair_ratio": statistics.median(values) if all(v is not None for v in values) else None,
                "undefined_pairs": sum(v is None for v in values)}
        for label in ("baseline", "candidate"):
            summaries = [record["validation"]["summary"] for record in records
                         if record["profile_id"] == pid and record["variant"] == label]
            profile_summary["observations"][label] = {
                "runs": len(summaries), "callbacks": sum(s["observations"] for s in summaries),
                "median_of_run_p50_us": statistics.median(s["p50_us"] for s in summaries),
                "median_of_run_p99_us": statistics.median(s["p99_us"] for s in summaries),
                "maximum_observed_us": max(s["max_us"] for s in summaries),
                "deadline_misses": sum(s["deadline_misses"] for s in summaries)}
        profiles.append(profile_summary)
    return {"scope": "observational, not WCRT or causal speedup; candidate/baseline; lower ratios mean shorter observed durations",
            "median_rule": "native p50 uses nearest rank; cross-run/pair medians use arithmetic middle of two central values when even",
            "pooling": "no pooled callback percentile and no cross-profile speedup aggregate",
            "pairs": pairs, "profiles": profiles, "limits": LIMITS}


def execute_plan(plan_path, output, competing_activity):
    """Retain failure evidence and stop at the first invalid or mutated input."""
    require(type(competing_activity) is str and 1 <= len(competing_activity.strip()) <= 4096,
            "a nonempty competing-activity description of at most 4096 characters is required")
    output = Path(os.path.abspath(output))
    require(not os.path.lexists(output), "output directory must be NEW; refusing existing path: " + str(output))
    require(output.parent.is_dir(), "output parent must already exist")
    output.mkdir(exist_ok=False)
    store = EvidenceStore(output)
    records = []
    manifest = {"schema_version": 1, "protocol": PROTOCOL, "complete": False,
                "started_utc": utc(), "launcher_argv": sys.argv, "output_directory": str(output),
                "competing_activity": competing_activity, "errors": [], "attempted_runs": records,
                "unattempted_run_ids": [], "limits": LIMITS}
    plan = None
    before = None
    comparison = None
    code = 3
    try:
        store.json("attempt.json", {k: v for k, v in manifest.items() if k != "attempted_runs"})
        plan, raw_plan = load_plan(plan_path)
        store.write("plan.json", raw_plan)
        manifest["plan_raw_sha256"] = digest(raw_plan)
        manifest["plan_canonical_sha256"] = digest(canonical(plan))
        manifest["harness_sha256"] = plan["harness_sha256"]
        manifest["unattempted_run_ids"] = [run["id"] for run in plan["runs"]]
        before = input_snapshot(plan, plan_path)
        store.json("input-hashes-before.json", before)
        store.json("machine-before.json", machine_snapshot())
        retain_build_metadata(store, plan)
        parent_affinity = affinity_snapshot()
        cpu = plan["settings"]["cpu"]
        if cpu is not None:
            require(parent_affinity["status"] == "observed" and hasattr(os, "sched_setaffinity"),
                    "explicit CPU affinity is unsupported/unavailable on this host")
            require(cpu in parent_affinity["cpus"], "requested CPU is outside observed permitted mask")
        manifest["launcher_affinity"] = parent_affinity
        profiles = {profile["id"]: profile for profile in plan["profiles"]}
        output_identities = {}
        variant_metadata = {}
        for run in plan["runs"]:
            require(input_snapshot(plan, plan_path) == before, "source, binary or frozen plan changed before run")
            require(affinity_snapshot() == parent_affinity, "launcher affinity availability or mask changed")
            execution = run_process(store, run, plan["settings"])
            records.append(execution)
            manifest["unattempted_run_ids"].remove(run["id"])
            errors = execution["errors"]
            try:
                require(not execution["timed_out"] and execution["returncode"] == 0,
                        "benchmark did not exit successfully within its timeout")
                require(not errors, "execution errors were recorded")
                for stream in execution["streams"].values():
                    require(not stream["truncated"] and not stream["errors"], "stream incomplete or write failed")
                affinity = execution["child_affinity"]
                require(type(affinity) is dict and affinity.get("error") is None,
                        "child affinity error")
                require(canonical(affinity.get("exec_argv")) == canonical(run["argv"]), "child command differs from plan")
                require(canonical(affinity.get("requested_cpu")) == canonical(cpu), "child CPU request differs from plan")
                require(canonical(affinity.get("before")) == canonical(parent_affinity), "child inherited affinity differs from launcher")
                expected_affinity = parent_affinity if cpu is None else {**parent_affinity, "cpus": [cpu]}
                require(canonical(affinity.get("after")) == canonical(expected_affinity), "child actual affinity differs from expected policy")
                raw = bounded_read(output / execution["streams"]["stdout"]["artifact"], MAX_STDOUT_BYTES)
                validation = validate_benchmark(strict_json(raw), profiles[run["profile_id"]])
                execution["validation"] = validation
                identity = canonical({"workload": validation["workload"], "output": validation["output_identity"]})
                pid = run["profile_id"]
                require(pid not in output_identities or output_identities[pid] == identity,
                        "repeat/baseline/candidate workload or output fingerprint changed")
                output_identities[pid] = identity
                metadata = canonical({"identity": validation["identity"], "build": validation["build"],
                                      "environment": validation["environment"]})
                label = run["variant"]
                require(label not in variant_metadata or variant_metadata[label] == metadata,
                        "same binary emitted inconsistent source/compiler/environment metadata")
                variant_metadata[label] = metadata
                require(input_snapshot(plan, plan_path) == before, "source, binary or frozen plan changed during run")
            except (ValueError, OSError, KeyError, TypeError, OverflowError) as error:
                errors.append(str(error))
            execution["valid"] = not errors
            store.json("runs/" + run["id"] + ".execution.json", execution)
            print(f"{run['id']}: {'verified' if execution['valid'] else 'FAILED'}", file=sys.stderr, flush=True)
            require(execution["valid"], "invalid run: " + run["id"])
        comparison = matched_comparison(plan, records)
        code = 0
    except (ValueError, OSError, TypeError, KeyError, OverflowError, KeyboardInterrupt) as error:
        manifest["errors"].append(type(error).__name__ + ": " + str(error))
        code = 1 if records else 3
    finally:
        if plan is not None and before is not None:
            try:
                after = input_snapshot(plan, plan_path)
                store.json("input-hashes-after.json", after, final=True)
                manifest["input_hashes_unchanged"] = after == before
                require(after == before, "source, binary or plan changed between final snapshots")
            except (OSError, ValueError) as error:
                manifest["input_hashes_unchanged"] = False
                manifest["errors"].append("final input snapshot: " + str(error))
                code = 1
        try:
            store.json("machine-after.json", machine_snapshot(), final=True)
            store.verify()
            if code == 0 and not manifest["errors"]:
                store.json("comparison.json", comparison, final=True)
        except (OSError, ValueError) as error:
            manifest["errors"].append("final artifact/environment check: " + str(error))
            code = 1
        manifest["complete"] = code == 0 and not manifest["errors"] and not manifest["unattempted_run_ids"]
        manifest["finished_utc"] = utc()
        manifest["exit_code"] = code
        manifest["artifact_hashes"] = dict(store.artifacts)
        manifest["retained_bytes_before_manifest"] = store.used
        try:
            store.json("manifest.json", manifest, final=True)
        except (OSError, ValueError) as error:
            print("Unable to finish manifest; partial evidence retained: " + str(error), file=sys.stderr)
            code = 3
    print(json.dumps({"complete": code == 0, "exit_code": code,
                      "manifest": str(output / "manifest.json"), "errors": manifest["errors"]}, allow_nan=False))
    return code


def main(argv=None):
    argv = sys.argv[1:] if argv is None else argv
    if argv and argv[0] == "_child":
        try:
            return child_main(argv[1:])
        except (OSError, ValueError) as error:
            print("callback wrapper: " + str(error), file=sys.stderr)
            return 3
    parser = argparse.ArgumentParser(description=__doc__)
    modes = parser.add_mutually_exclusive_group(required=True)
    modes.add_argument("--plan", action="store_true", help="print frozen JSON; do not inspect or execute benchmark binaries")
    modes.add_argument("--run-plan", type=Path, help="execute the exact reviewed plan created by this harness")
    parser.add_argument("--output-dir", "--output", dest="output", type=Path, help="NEW directory for execution evidence; existing paths are refused")
    parser.add_argument("--competing-activity", help="honest description of known concurrent work; required for execution")
    parser.add_argument("--baseline", type=Path, help="plan only; default build/baseline/rhodes_vst3_quadrature_benchmark")
    parser.add_argument("--candidate", type=Path, help="plan only; default build/vst3/rhodes_vst3_quadrature_benchmark")
    parser.add_argument("--profile", action="append", choices=PROFILE_IDS, help="plan only; repeat to select a fixed subset (default all 12)")
    parser.add_argument("--blocks", type=int, help=f"plan only; 1..{MAX_BLOCKS}, default 256")
    parser.add_argument("--warmup-blocks", type=int, help=f"plan only; 0..{MAX_WARMUP}, default 16")
    parser.add_argument("--cycles", type=int, help=f"plan only; 1..{MAX_CYCLES}, default 2")
    parser.add_argument("--timeout-seconds", type=int, help=f"plan only; 1..{MAX_TIMEOUT_SECONDS} per process, default 60")
    parser.add_argument("--cpu", type=int, help="plan only; explicit Linux CPU ID; default inherited affinity, observed before exec")
    args = parser.parse_args(argv)
    plan_options = {"baseline": args.baseline, "candidate": args.candidate, "profile_ids": args.profile,
                    "blocks": args.blocks, "warmup_blocks": args.warmup_blocks,
                    "cycles": args.cycles, "timeout_seconds": args.timeout_seconds, "cpu": args.cpu}
    try:
        if args.plan:
            require(args.output is None and args.competing_activity is None, "--plan writes only stdout; execution options are not allowed")
            settings = {key: value for key, value in plan_options.items() if value is not None}
            settings.setdefault("baseline", ROOT / "build/baseline/rhodes_vst3_quadrature_benchmark")
            settings.setdefault("candidate", ROOT / "build/vst3/rhodes_vst3_quadrature_benchmark")
            print(json.dumps(make_plan(**settings), indent=2, allow_nan=False))
            return 0
        require(all(value is None for value in plan_options.values()), "--run-plan cannot override frozen plan options")
        require(args.output is not None and args.competing_activity is not None,
                "--run-plan requires --output-dir and --competing-activity")
        return execute_plan(args.run_plan, args.output, args.competing_activity)
    except (ValueError, OSError) as error:
        print("compare_vst3_callbacks: " + str(error), file=sys.stderr)
        return 2 if isinstance(error, ValueError) else 3


if __name__ == "__main__":
    raise SystemExit(main())
