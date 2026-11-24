#!/usr/bin/env python3
"""Execute and retain the frozen, offline VST3 callback observation protocol.

This launcher does not build, change scheduling priority, discard slow callbacks,
or interpret nominal-period overruns as audio-device underruns.
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
import subprocess
import sys
import time

ROOT = Path(__file__).resolve().parents[1]
PLAN = ROOT / "docs/validation/quadrature-benchmark-plan-20261123.json"
PLAN_HASH = "df03962a1c3406a7528b3e7d71c784e65f891ad940e7a04ea1c46006ddec5fad"
LIMITS = [
    "Offline concrete process() calls; no DAW, loader or audio device is timed.",
    "Observed maxima include scheduling interruptions and are not worst-case bounds.",
    "Nominal-period misses are retained observations, not measured device underruns.",
    "Synthetic source parameters; no physical or perceptual fidelity is established.",
    "Two rounds on one shared container do not establish statistical independence.",
]


def canonical(value):
    return json.dumps(value, sort_keys=True, separators=(",", ":"),
                      ensure_ascii=True, allow_nan=False).encode()


def digest(raw):
    return hashlib.sha256(raw).hexdigest()


def utc():
    return datetime.now(timezone.utc).isoformat()


def write_json(path, value):
    with path.open("x", encoding="utf-8") as stream:
        json.dump(value, stream, indent=2, allow_nan=False)
        stream.write("\n")


def command(argv, cwd=ROOT):
    try:
        result = subprocess.run(argv, cwd=cwd, capture_output=True, text=True,
                                timeout=20, check=False)
        return {"argv": argv, "returncode": result.returncode,
                "stdout": result.stdout, "stderr": result.stderr}
    except (OSError, subprocess.TimeoutExpired) as error:
        return {"argv": argv, "unavailable": str(error)}


def read_optional(path):
    try:
        return Path(path).read_text()
    except (OSError, UnicodeError) as error:
        return {"unavailable": str(error)}


def machine_snapshot():
    paths = ["/proc/cpuinfo", "/proc/meminfo", "/proc/loadavg", "/proc/stat",
             "/proc/self/cgroup", "/proc/self/status", "/etc/os-release",
             "/sys/fs/cgroup/cpu.max", "/sys/fs/cgroup/cpu.stat",
             "/sys/fs/cgroup/memory.max", "/sys/fs/cgroup/memory.current",
             "/sys/fs/cgroup/cpuset.cpus.effective",
             "/sys/devices/system/cpu/smt/active",
             "/sys/devices/system/cpu/intel_pstate/no_turbo",
             "/sys/devices/system/cpu/cpufreq/boost"]
    for cpu in sorted(os.sched_getaffinity(0)):
        base = f"/sys/devices/system/cpu/cpu{cpu}"
        paths += [base + "/cpufreq/scaling_governor",
                  base + "/cpufreq/scaling_cur_freq",
                  base + "/topology/physical_package_id",
                  base + "/topology/core_id"]
    return {"utc": utc(), "monotonic_ns": time.monotonic_ns(),
            "platform": platform.platform(), "uname": list(platform.uname()),
            "python": sys.version, "python_executable": sys.executable,
            "affinity": sorted(os.sched_getaffinity(0)),
            "scheduler": os.sched_getscheduler(0),
            "priority": os.getpriority(os.PRIO_PROCESS, 0),
            "files": {name: read_optional(name) for name in paths},
            "process_activity": command(["ps", "-eo", "pid,ppid,comm,pcpu,pmem"]),
            "virtualization": command(["systemd-detect-virt"]),
            "steady_clock_nominal_period": "not separately probed; C++ clock name retained in raw output"}


def source_paths(sdk):
    paths = set()
    # All tracked C++ inputs, build definitions and the launcher are retained.
    tracked = subprocess.check_output(["git", "ls-files", "-z"], cwd=ROOT).split(b"\0")
    for raw in tracked:
        if not raw:
            continue
        path = ROOT / os.fsdecode(raw)
        if path.suffix in {".cpp", ".hpp", ".h", ".cmake"} or path.name == "CMakeLists.txt":
            paths.add(path)
    paths.add(Path(__file__).resolve())
    for part in (sdk, sdk / "base", sdk / "cmake", sdk / "pluginterfaces", sdk / "public.sdk"):
        tracked = subprocess.check_output(["git", "ls-files", "-z"], cwd=part).split(b"\0")
        for raw in tracked:
            if raw:
                path = part / os.fsdecode(raw)
                if path.is_file():
                    paths.add(path)
    targets = ["rhodes_vst3_benchmark", "rhodes_vst3_filtered_benchmark", "rhodes_vst3_quadrature_benchmark"]
    inputs = command(["ninja", "-C", "build/vst3", "-t", "inputs", *targets])
    if inputs.get("returncode") != 0:
        raise RuntimeError("cannot inventory actual Ninja target dependencies")
    for name in inputs["stdout"].splitlines():
        path = (ROOT / "build/vst3" / name).resolve()
        if path.is_file():
            paths.add(path)
    for name in ("CMakeCache.txt", "compile_commands.json", "build.ninja", "CMakeFiles/rules.ninja"):
        paths.add(ROOT / "build/vst3" / name)
    return sorted(paths)


def hashes(paths):
    return {str(path): digest(path.read_bytes()) for path in paths}


def build_preflight(sdk, binaries):
    cache = {}
    for line in (ROOT / "build/vst3/CMakeCache.txt").read_text().splitlines():
        if line and not line.startswith(("#", "//")) and "=" in line and ":" in line:
            key, value = line.split("=", 1)
            cache[key.split(":", 1)[0]] = value
    if Path(cache["RHODES_VST3_SDK_ROOT"]).resolve() != sdk.resolve():
        raise RuntimeError("requested SDK is not the SDK recorded by this build")
    if cache["CMAKE_BUILD_TYPE"] != "Release" or cache["CMAKE_GENERATOR"] != "Ninja":
        raise RuntimeError("this launcher requires the documented Release Ninja build")
    flags = {k: v for k, v in cache.items() if "FLAGS" in k}
    if any("-fsanitize=" in v for v in flags.values()) or cache.get("RHODES_ENABLE_SANITIZERS") == "ON":
        raise RuntimeError("sanitizer instrumentation is outside this frozen protocol")
    freshness = command(["ninja", "-C", "build/vst3", "-n", *[p.name for p in binaries]])
    if freshness.get("returncode") != 0 or "ninja: no work to do." not in freshness.get("stdout", ""):
        raise RuntimeError("benchmark build is stale or cannot be checked; build before capture")
    expected = {".": "3cdf9ca5d1f5b1b21e0a86832aa4abe55607bd96",
                "base": "fcf9da0bd27a16f7f03773a3a39822f28f5c8477",
                "cmake": "054c9143cbb8d47fc4694e473f2ee3b4d951a8f5",
                "pluginterfaces": "4f547e8e102b47de4a8b8aaf343c73b700786372",
                "public.sdk": "586dc5e6c8012c3e4b01c79389375cbe96bdb1da"}
    for part, revision in expected.items():
        actual = command(["git", "rev-parse", "HEAD"], sdk / part)
        dirty = command(["git", "status", "--porcelain=v1"], sdk / part)
        if actual.get("stdout", "").strip() != revision or dirty.get("stdout", "").strip() or dirty.get("returncode") != 0:
            raise RuntimeError("pinned SDK revision/cleanliness check failed: " + part)
    return {"configured_compiler": cache["CMAKE_CXX_COMPILER"],
            "compiler_version": command([cache["CMAKE_CXX_COMPILER"], "--version"]),
            "freshness_check": freshness, "cache_flags": flags}


def statistics(values, period):
    if not values or any(type(x) not in (int, float) or not math.isfinite(x) or x < 0 for x in values):
        raise ValueError("durations must be finite, nonnegative and nonempty")
    ordered = sorted(values)
    total = 0.0
    for value in ordered:  # Matches the benchmark's sorted binary64 accumulation.
        total += value
    result = {"observations": len(values), "mean_us": total / len(values),
              "max_us": ordered[-1], "deadline_us": period,
              "deadline_misses": sum(x > period for x in values)}
    for percentile in (50, 95, 99):
        result[f"p{percentile}_us"] = ordered[(len(values) * percentile + 99) // 100 - 1]
    return result


def verify_result(raw, profile, variant):
    """Independent workload/integrity checks; never gate on callback speed."""
    errors = []

    def equal(actual, expected, label):
        if type(actual) is not type(expected) and not (
                type(actual) in (int, float) and type(expected) in (int, float)):
            errors.append(label + ": wrong type")
        elif actual != expected:
            errors.append(label + ": differs from frozen expectation")

    try:
        equal(raw["schema_version"], 1, "schema_version")
        equal(raw["benchmark"], "synthetic_vst3_process", "benchmark")
        equal(raw["complete"], True, "complete")
        equal(raw["environment"]["machine_label"], "q6-same-host-matrix-20261123", "machine label")
        equal(raw["environment"]["platform"], "Linux", "platform")
        equal(raw["build"]["configuration"], "Release", "build configuration")
        equal(raw["build"]["ndebug"], True, "NDEBUG")
        equal(raw["build"]["fast_math_macro"], False, "fast-math macro")
        equal(raw["timing"]["clock"], "std::chrono::steady_clock", "clock")
        identity, workload, integrity = raw["identity"], raw["workload"], raw["integrity"]
        for name in ("plugin_name", "state_model", "latency_frames"):
            equal(identity[name], variant[name], "identity." + name)
        rate = profile["sample_rate_hz"]
        if variant["id"] == "quadrature":
            for name in ("quadrature_nodes", "observation_scheme"):
                equal(identity[name], variant[name], "identity." + name)
            for name in variant["forbidden_identity_fields"]:
                if name in identity:
                    errors.append("forbidden quadrature identity field: " + name)
        else:
            factor = variant["expected_oversampling_factor_by_rate"][str(rate)]
            equal(identity["oversampling_factor"], factor, "oversampling_factor")
            equal(identity["internal_sample_rate_hz"], rate * factor, "internal_sample_rate_hz")
        expected_work = {
            "id": profile["workload"], "sample_format_bits": profile["sample_format_bits"],
            "sample_rate_hz": rate, "block_frames": profile["block_frames"],
            "requested_blocks": profile["measured_callbacks"],
            "measured_blocks": profile["measured_callbacks"],
            "requested_warmup_blocks": profile["warmup_callbacks"],
            "completed_warmup_blocks": profile["warmup_callbacks"],
            "selected_key_count": profile["active_keys"],
            "retrigger_interval_blocks": profile["expected_retrigger_interval_blocks"],
            "offered_note_events": profile["expected_note_events"],
            "offered_parameter_points": profile["expected_parameter_points"],
            "initial_normalized_gain": 0.5, "initial_sustain": False,
            "note_intensity_binary32": 0.699999988079071,
        }
        for name, expected in expected_work.items():
            equal(workload[name], expected, "workload." + name)
        expected_integrity = {
            "warmup_success": True, "successful_callbacks": profile["measured_callbacks"],
            "failed_callbacks": 0, "last_result_code": 0,
            "frame_cursor": profile["expected_measured_host_frames"],
            "active_note_owners": profile["expected_final_note_owners"],
            "expected_note_owners": profile["expected_final_note_owners"],
            "note_getter_reads": profile["expected_note_events"],
            "parameter_point_getter_reads": profile["expected_parameter_points"],
            "finite_output_and_checksum": True, "stereo_equal": True,
            "silence_flags_correct": True, "output_presence_correct": True,
            "nonzero_output_expected": profile["expected_nonzero_output"],
        }
        for name, expected in expected_integrity.items():
            equal(integrity[name], expected, "integrity." + name)
        for name in ("checksum_sum_squares", "peak_abs_digital_output"):
            value = integrity[name]
            if type(value) not in (int, float) or not math.isfinite(value) or value < 0:
                errors.append("invalid integrity." + name)
        present = integrity["nonzero_channel_samples"]
        if type(present) is not int or not 0 <= present <= 2 * profile["expected_measured_host_frames"]:
            errors.append("invalid nonzero_channel_samples")
        elif (present > 0) != profile["expected_nonzero_output"]:
            errors.append("unexpected output presence")
        fingerprint = integrity["output_fnv1a64"]
        if not isinstance(fingerprint, str) or len(fingerprint) != 16 or any(
                c not in "0123456789abcdef" for c in fingerprint):
            errors.append("invalid output fingerprint")
        values = raw["timing"]["raw_callback_us"]
        equal(len(values), profile["measured_callbacks"], "raw duration count")
        summary = statistics(values, 1e6 * profile["block_frames"] / rate)
        for name, expected in summary.items():
            equal(raw["timing"]["summary"][name], expected, "timing.summary." + name)
        return {"valid": not errors, "errors": errors, "summary": summary,
                "mean_fsum_us": math.fsum(values) / len(values),
                "mean_input_order_us": sum(values) / len(values)}
    except (KeyError, TypeError, ValueError, OverflowError) as error:
        errors.append("malformed benchmark output: " + str(error))
        return {"valid": False, "errors": errors}


def load_plan(path=PLAN):
    raw = path.read_bytes()
    plan = json.loads(raw)
    unsigned = {k: v for k, v in plan.items() if k != "plan_sha256"}
    if plan["plan_sha256"] != PLAN_HASH or digest(canonical(unsigned)) != PLAN_HASH:
        raise ValueError("frozen plan hash mismatch; use a new protocol for changed inputs")
    return plan, raw


def child(argv):
    cpu, record = int(argv[0]), Path(argv[1])
    executable = argv[2:]
    os.sched_setaffinity(0, {cpu})
    actual = sorted(os.sched_getaffinity(0))
    if actual != [cpu]:
        raise RuntimeError("child affinity did not match requested single CPU")
    write_json(record, {"utc": utc(), "pid": os.getpid(), "affinity": actual,
                        "exec_argv": executable, "scheduler": os.sched_getscheduler(0),
                        "priority": os.getpriority(os.PRIO_PROCESS, 0)})
    try:
        os.execv(executable[0], executable)
    except OSError as error:
        write_json(record.with_suffix(".exec-error.json"), {"error": str(error)})
        raise


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, required=True, help="new evidence directory")
    parser.add_argument("--sdk", type=Path, required=True, help="pinned SDK used for this build")
    parser.add_argument("--competing-activity", required=True,
                        help="honest description of other activity during capture")
    args = parser.parse_args(argv)
    output = args.output.resolve()
    try:
        output.mkdir(parents=False, exist_ok=False)
    except OSError as error:
        print(f"Cannot create exclusive evidence directory: {error}", file=sys.stderr)
        return 3
    records, raw_results = [], {}
    manifest = {"schema_version": 1, "status": "operational_failure", "started_utc": utc(),
                "launcher_argv": sys.argv, "launcher_sha256": digest(Path(__file__).read_bytes()),
                "competing_activity": args.competing_activity, "limits": LIMITS, "results": records}
    status = 3
    try:
        if sys.platform != "linux" or not hasattr(os, "sched_setaffinity"):
            raise RuntimeError("frozen protocol requires Linux single-CPU affinity")
        plan, plan_raw = load_plan()
        (output / "plan.json").write_bytes(plan_raw)
        manifest["plan_raw_sha256"] = digest(plan_raw)
        manifest["plan_canonical_sha256"] = PLAN_HASH
        allowed = sorted(os.sched_getaffinity(0))
        cpu = allowed[0]
        manifest["allowed_affinity"] = allowed
        manifest["chosen_cpu"] = cpu
        write_json(output / "machine-before.json", machine_snapshot())
        compiled = source_paths(args.sdk.resolve())
        binaries = sorted({(ROOT / run["argv"][0]).resolve() for run in plan["runs"]})
        preflight = build_preflight(args.sdk.resolve(), binaries)
        retained = compiled + binaries
        before = hashes(retained)
        write_json(output / "input-hashes-before.json", before)
        metadata = {"git_head": command(["git", "rev-parse", "HEAD"]),
                    "git_tree": command(["git", "rev-parse", "HEAD^{tree}"]),
                    "git_status": command(["git", "status", "--porcelain=v1"]),
                    "source_diff": command(["git", "diff", "HEAD", "--", "src", "include", "plugins", "apps", "cmake", "CMakeLists.txt"]),
                    "build_preflight": preflight,
                    "linkage": [command(["ldd", str(p)]) for p in binaries],
                    "build_commands": command(["ninja", "-C", "build/vst3", "-t", "commands",
                        *[p.name for p in binaries]]),
                    "sdk": {str(part): {"head": command(["git", "rev-parse", "HEAD"], part),
                                        "status": command(["git", "status", "--porcelain=v1"], part)}
                        for part in [args.sdk, *[args.sdk / p for p in ("base", "cmake", "pluginterfaces", "public.sdk")]]},
                    "methodology_sha256": digest((ROOT / "docs/BENCHMARKING.md").read_bytes())}
        write_json(output / "build-metadata.json", metadata)
        for name in ("CMakeCache.txt", "compile_commands.json", "build.ninja"):
            (output / name).write_bytes((ROOT / "build/vst3" / name).read_bytes())
        profiles = {p["id"]: p for p in plan["profiles"]}
        variants = {v["id"]: v for v in plan["variants"]}
        for run in plan["runs"]:
            if sorted(os.sched_getaffinity(0)) != allowed or hashes(retained) != before:
                raise RuntimeError("compiled input, binary, or allowed affinity changed")
            argv = [str((ROOT / run["argv"][0]).resolve()), *run["argv"][1:]]
            affinity_path = output / (run["id"] + ".affinity.json")
            record = {"id": run["id"], "sequence": run["sequence"], "round": run["round"],
                      "profile_id": run["profile_id"], "variant": run["variant"],
                      "argv": argv, "cwd": str(ROOT), "started_utc": utc(),
                      "start_monotonic_ns": time.monotonic_ns(), "status": "incomplete"}
            stdout, stderr = output / run["stdout_file"], output / run["stderr_file"]
            with stdout.open("xb") as out, stderr.open("xb") as err:
                process = subprocess.Popen([sys.executable, str(Path(__file__).resolve()),
                    "--_child", str(cpu), str(affinity_path), *argv], cwd=ROOT, stdout=out, stderr=err)
                try:
                    record["returncode"] = process.wait(timeout=plan["execution"]["timeout_seconds_per_process"])
                except subprocess.TimeoutExpired:
                    process.kill()
                    record["returncode"] = process.wait()
                    record["timed_out"] = True
            record.update(ended_utc=utc(), end_monotonic_ns=time.monotonic_ns(),
                          stdout=stdout.name, stderr=stderr.name,
                          stdout_sha256=digest(stdout.read_bytes()), stderr_sha256=digest(stderr.read_bytes()))
            if affinity_path.exists():
                record["child_affinity"] = json.loads(affinity_path.read_text())
            try:
                raw = json.loads(stdout.read_bytes(), parse_constant=lambda value: (_ for _ in ()).throw(ValueError(value)))
                result = verify_result(raw, profiles[run["profile_id"]], variants[run["variant"]])
                record["verification"] = result
                if record["returncode"] == 0 and result["valid"] and record.get("child_affinity", {}).get("affinity") == [cpu]:
                    record["status"] = "complete"
                if isinstance(raw, dict) and isinstance(raw.get("integrity"), dict):
                    raw_results[run["id"]] = raw
            except (ValueError, UnicodeError, TypeError) as error:
                record["parse_error"] = str(error)
            write_json(output / run["record_file"], record)
            records.append(record)
            print(f"{run['sequence']}/72 {run['id']}: {record['status']}", flush=True)
            if record.get("child_affinity", {}).get("affinity") != [cpu] or affinity_path.with_suffix(".exec-error.json").exists():
                raise RuntimeError("child affinity or executable setup failed; remaining runs are unattempted")
        after = hashes(retained)
        write_json(output / "input-hashes-after.json", after)
        manifest["input_hashes_unchanged"] = before == after
        pairs = []
        for profile in profiles:
            for variant in variants:
                left = raw_results.get(f"r1-{profile}-{variant}", {}).get("integrity", {})
                right = raw_results.get(f"r2-{profile}-{variant}", {}).get("integrity", {})
                mismatches = [field for field in plan["validation_and_analysis"]["repeat_checks"]
                              if field.split(".")[1] not in left or field.split(".")[1] not in right
                              or left[field.split(".")[1]] != right[field.split(".")[1]]]
                pairs.append({"profile_id": profile, "variant": variant, "mismatches": mismatches})
        manifest["repeat_pairs"] = pairs
        valid = before == after and all(r["status"] == "complete" for r in records) and all(not p["mismatches"] for p in pairs)
        manifest["status"] = "complete" if valid else "integrity_failure"
        status = 0 if valid else 1
    except (OSError, ValueError, KeyError, IndexError, RuntimeError, subprocess.SubprocessError) as error:
        manifest["operational_error"] = str(error)
    finally:
        manifest["ended_utc"] = utc()
        manifest["attempted_runs"] = len(records)
        manifest["unattempted_run_ids"] = [r["id"] for r in locals().get("plan", {}).get("runs", [])
                                            if r["id"] not in {x["id"] for x in records}]
        if sys.platform == "linux" and hasattr(os, "sched_getaffinity"):
            write_json(output / "machine-after.json", machine_snapshot())
        if "before" in locals() and not (output / "input-hashes-after.json").exists():
            try:
                after = hashes(retained)
                write_json(output / "input-hashes-after.json", after)
                manifest["input_hashes_unchanged"] = before == after
            except OSError as error:
                manifest["final_hash_error"] = str(error)
        write_json(output / "manifest.json", manifest)
    return status


if __name__ == "__main__":
    if len(sys.argv) > 1 and sys.argv[1] == "--_child":
        child(sys.argv[2:])
    else:
        raise SystemExit(main())
