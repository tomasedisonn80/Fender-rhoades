"""Build-independent contracts for the frozen ABBA callback comparison harness.

Records below are synthetic parser fixtures, not benchmark observations. Event
counts and statistical answers are supplied independently of the implementation.
Every process launch is mocked; neither benchmark binaries nor SDKs are needed.
"""
from __future__ import annotations

import copy
from contextlib import ExitStack, contextmanager, redirect_stderr, redirect_stdout
from fractions import Fraction
import hashlib
import importlib.util
import io
import json
import math
from pathlib import Path, PureWindowsPath
import subprocess
import tempfile
from types import SimpleNamespace
import unittest
from unittest import mock


ROOT = Path(__file__).resolve().parents[2]
SPEC = importlib.util.spec_from_file_location(
    "callback_compare_under_test", ROOT / "tools/compare_vst3_callbacks.py")
callback = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(callback)


def profile(**changes):
    result = {"id": "p02", "sample_rate_hz": 48000, "block_frames": 64,
              "active_keys": 1, "workload": "held", "sample_format_bits": 64,
              "measured_blocks": 2, "warmup_blocks": 0}
    result.update(changes)
    return result


def benchmark(selected, *, interval=375, notes=1, points=0, owners=1,
              silent=False, values=None, summary=None):
    """Supply explicit schedule counts and a constant-duration default oracle.

    The two nonzero channel samples are a minimal stereo schema fixture, not
    synthesized audio. No production statistics or scheduling helper is called.
    """
    blocks = selected["measured_blocks"]
    frames = selected["block_frames"]
    deadline = float(Fraction(1_000_000 * frames, selected["sample_rate_hz"]))
    if values is None:
        values = [2.0] * blocks
    if summary is None:
        summary = {"observations": blocks, "mean_us": 2.0, "p50_us": 2.0,
                   "p95_us": 2.0, "p99_us": 2.0, "max_us": 2.0,
                   "deadline_us": deadline, "deadline_misses": 0}
    return {
        "schema_version": 1, "benchmark": "synthetic_vst3_process", "complete": True,
        "identity": {
            "source_revision": "synthetic-unit-fixture", "source_diff_hash": "unrecorded",
            "plugin_name": "Rhodes Research Quadrature", "state_model": 3,
            "latency_frames": 48, "quadrature_nodes": 6,
            "observation_scheme": "gauss-legendre-host-cell",
            "timing_scope": "RhodesProcessor::process including host getters, validation, ownership, automation, output clearing, rendering and mailbox publication",
        },
        "environment": {
            "machine_label": "matched-q6-callbacks-abba-v1", "platform": "fixture",
            "architecture": "fixture", "cpu_model": None, "kernel": None,
            "power_policy": None, "affinity": "uncontrolled", "priority": "uncontrolled",
            "competing_load": "uncontrolled", "floating_point_rounding_code": 0,
            "flush_to_zero": "not inspected",
        },
        "build": {
            "configuration": "fixture", "compiler": "fixture", "complete_flags": "unrecorded",
            "ndebug": True, "fast_math_macro": False, "sanitizer": "not recorded",
            "lto": "not recorded",
        },
        "workload": {
            "id": selected["workload"], "sample_format_bits": selected["sample_format_bits"],
            "sample_rate_hz": selected["sample_rate_hz"], "block_frames": frames,
            "requested_blocks": blocks, "measured_blocks": blocks,
            "requested_warmup_blocks": selected["warmup_blocks"],
            "completed_warmup_blocks": selected["warmup_blocks"],
            "selected_key_count": selected["active_keys"], "retrigger_interval_blocks": interval,
            "offered_note_events": notes, "offered_parameter_points": points,
            "initial_normalized_gain": 0.5, "initial_sustain": False,
            "note_intensity_binary32": 0.699999988079071,
        },
        "timing": {"clock": "std::chrono::steady_clock",
                   "percentile_rule": "nearest rank, one-based ceil(N*p/100)",
                   "summary": summary, "raw_callback_us": values},
        "integrity": {
            "warmup_success": True, "successful_callbacks": blocks, "failed_callbacks": 0,
            "last_result_code": 0, "frame_cursor": blocks * frames,
            "active_note_owners": owners, "expected_note_owners": owners,
            "note_getter_reads": notes, "parameter_point_getter_reads": points,
            "finite_output_and_checksum": True, "stereo_equal": True,
            "silence_flags_correct": True, "nonzero_output_expected": not silent,
            "output_presence_correct": True, "nonzero_channel_samples": 0 if silent else 2,
            "checksum_sum_squares": 0.0 if silent else 0.5,
            "peak_abs_digital_output": 0.0 if silent else 0.5,
            "output_fnv1a64": "0123456789abcdef",
            "allocation_evidence": "not measured by this executable",
        },
        "limits": ["Synthetic parser fixture; no measured performance or acoustic claim."],
    }


def make_plan(**changes):
    settings = {"baseline": ROOT / "absent-unit-baseline",
                "candidate": ROOT / "absent-unit-candidate"}
    settings.update(changes)
    return callback.make_plan(**settings)


def field_at(value, path):
    for key in path:
        value = value[key]
    return value


class PlanTests(unittest.TestCase):
    def test_default_fixed_matrix_counts_caps_and_no_binary_access(self):
        with mock.patch.object(callback, "file_identity", side_effect=AssertionError("binary read")), \
                mock.patch.object(callback.subprocess, "Popen", side_effect=AssertionError("launch")):
            plan = make_plan()
            self.assertEqual(plan, make_plan())
        expected = [
            ("p01", 48000, 64, 0, "held", 64), ("p02", 48000, 64, 1, "held", 64),
            ("p03", 48000, 64, 12, "held", 64), ("p04", 48000, 64, 73, "held", 64),
            ("p05", 192000, 256, 0, "held", 64), ("p06", 192000, 256, 1, "held", 64),
            ("p07", 192000, 256, 12, "held", 64), ("p08", 192000, 256, 73, "held", 64),
            ("p09", 48000, 256, 12, "pedal", 64), ("p10", 192000, 64, 73, "pedal", 32),
            ("p11", 192000, 64, 12, "gain", 32), ("p12", 48000, 256, 73, "gain", 64),
        ]
        self.assertEqual([tuple(p[k] for k in ("id", "sample_rate_hz", "block_frames",
                         "active_keys", "workload", "sample_format_bits"))
                          for p in plan["profiles"]], expected)
        self.assertEqual(plan["totals"], {
            "profiles": 12, "process_runs": 96, "matched_pairs": 48,
            "measured_callbacks": 24576, "warmup_callbacks": 1536,
            "measured_host_frames": 3932160, "sum_process_timeout_budgets_seconds": 5760})
        self.assertEqual(plan["caps"], {
            "profiles": 12, "runs": 192, "blocks_per_run": 1024,
            "warmup_blocks_per_run": 64, "cycles": 4, "timeout_seconds_per_run": 120,
            "stdout_bytes_per_run": 262144, "stderr_bytes_per_run": 65536,
            "capture_bytes": 134217728})
        self.assertEqual(plan["affinity"], {"requested_cpu": None, "policy": "inherited"})
        self.assertEqual(plan["harness_sha256"], hashlib.sha256(callback.SCRIPT.read_bytes()).hexdigest())

    def test_subset_is_canonical_abba_and_reverses_only_profile_order(self):
        plan = make_plan(profile_ids=["p12", "p02", "p09"], blocks=3, warmup_blocks=1, cycles=2)
        self.assertEqual(plan["settings"]["profile_ids"], ["p02", "p09", "p12"])
        expected = []
        for cycle, ids in ((1, ["p02", "p09", "p12"]), (2, ["p12", "p09", "p02"])):
            for pid in ids:
                for position, variant in enumerate(["baseline", "candidate", "candidate", "baseline"], 1):
                    expected.append((cycle, pid, position, variant))
        self.assertEqual([tuple(r[k] for k in ("cycle", "profile_id", "position", "variant"))
                          for r in plan["runs"]], expected)
        self.assertEqual(plan["runs"][0]["argv"], [
            plan["settings"]["baseline"], "--blocks", "3", "--warmup-blocks", "1",
            "--block-size", "64", "--sample-rate", "48000", "--active-keys", "1",
            "--workload", "held", "--sample-format", "64", "--machine", "matched-q6-callbacks-abba-v1"])
        self.assertEqual(plan["totals"]["measured_host_frames"], 13824)
        self.assertEqual(plan["totals"]["matched_pairs"], 12)

    def test_integer_and_selection_bounds_reject_bool_and_coercion(self):
        bounds = {"blocks": (1, 1024), "warmup_blocks": (0, 64), "cycles": (1, 4),
                  "timeout_seconds": (1, 120), "cpu": (0, 1048575)}
        for key, (low, high) in bounds.items():
            for bad in (low - 1, high + 1, True, float(low), str(low)):
                with self.subTest(key=key, bad=bad), self.assertRaises(ValueError):
                    make_plan(**{key: bad})
        for ids in ([], ("p01",), ["p01", "p01"], ["p13"], [1], ["p01"] * 13):
            with self.subTest(ids=ids), self.assertRaises(ValueError):
                make_plan(profile_ids=ids)
        for value in ("", "bad\x00path", 1):
            with self.subTest(binary=value), self.assertRaises(ValueError):
                make_plan(baseline=value)
        maximum = make_plan(blocks=1024, warmup_blocks=64, cycles=4,
                            timeout_seconds=120, cpu=1048575)
        self.assertEqual(len(maximum["runs"]), 192)
        self.assertEqual(maximum["totals"]["measured_callbacks"], 196608)

    def test_load_keeps_exact_bytes_and_rejects_any_frozen_plan_change(self):
        plan = make_plan(profile_ids=["p02"], blocks=2, warmup_blocks=0, cycles=1)
        with tempfile.TemporaryDirectory() as temporary:
            path = Path(temporary) / "plan.json"
            raw = json.dumps(plan, indent=3).encode() + b"\n"
            path.write_bytes(raw)
            self.assertEqual(callback.load_plan(path), (plan, raw))
            mutations = [((), "schema_version", 1.0), ((), "harness_sha256", "0" * 64),
                         (("caps",), "runs", 193), (("settings",), "blocks", True),
                         (("settings",), "unknown", 1), (("runs", 0), "argv", ["replaced"]),
                         (("profiles", 0), "sample_rate_hz", 44100)]
            for parent, name, replacement in mutations:
                with self.subTest(field=(parent, name)):
                    changed = copy.deepcopy(plan)
                    field_at(changed, parent)[name] = replacement
                    path.write_text(json.dumps(changed), encoding="utf-8")
                    with self.assertRaises(ValueError):
                        callback.load_plan(path)
            path.write_bytes(raw)
            with mock.patch.object(callback, "MAX_PLAN_BYTES", len(raw) - 1), \
                    self.assertRaisesRegex(ValueError, "byte cap"):
                callback.load_plan(path)


class JsonAndStatisticsTests(unittest.TestCase):
    def test_strict_json_rejects_duplicate_nonfinite_trailing_and_invalid_bytes(self):
        invalid = [b'{"x":1,"x":2}', b'{"nested":{"x":1,"x":2}}', b"NaN",
                   b"Infinity", b"-Infinity", b"[1e400]", b"[-1e400]",
                   b"{} trailing", b"{} {}", b'{"x":', b'"\xff"']
        for raw in invalid:
            with self.subTest(raw=raw), self.assertRaises(ValueError):
                callback.strict_json(raw)
        self.assertEqual(callback.strict_json(b'{"x":[0,1.25,null,true]}'),
                         {"x": [0, 1.25, None, True]})

    def test_nearest_rank_is_not_interpolation_and_period_equality_is_not_a_miss(self):
        result = callback.timing_statistics(list(range(100, 0, -1)), 99.0)
        self.assertEqual(result, {"observations": 100, "mean_us": 50.5, "max_us": 100,
                                 "deadline_us": 99.0, "deadline_misses": 1,
                                 "p50_us": 50, "p95_us": 95, "p99_us": 99})
        result = callback.timing_statistics([8.0, 1.0, 4.0, 2.0], 4.0)
        self.assertEqual(result["p50_us"], 2.0)
        self.assertEqual(result["mean_us"], 3.75)
        self.assertEqual(result["deadline_misses"], 1)
        around_period = [math.nextafter(4.0, 0.0), 4.0, math.nextafter(4.0, math.inf)]
        self.assertEqual(callback.timing_statistics(around_period, 4.0)["deadline_misses"], 1)

    def test_mean_uses_native_sorted_accumulation_and_preserves_fsum_diagnostic(self):
        # Ascending accumulation preserves both unit observations before 1e16.
        self.assertEqual(callback.timing_statistics([1e16, 1.0, 1.0], 1e16)["mean_us"],
                         3333333333333334.0)
        selected = profile(measured_blocks=10)
        summary = {"observations": 10, "mean_us": 0.09999999999999999,
                   "p50_us": 0.1, "p95_us": 0.1, "p99_us": 0.1, "max_us": 0.1,
                   "deadline_us": 4000.0 / 3, "deadline_misses": 0}
        raw = benchmark(selected, values=[0.1] * 10, summary=summary)
        result = callback.validate_benchmark(raw, selected)
        self.assertEqual(result["summary"]["mean_us"], 0.09999999999999999)
        self.assertEqual(result["mean_fsum_us"], 0.1)
        raw["timing"]["summary"]["mean_us"] = 0.1
        with self.assertRaisesRegex(ValueError, "mean_us"):
            callback.validate_benchmark(raw, selected)

    def test_invalid_raw_durations_and_overflow_cannot_be_statistics(self):
        for values in ([], (), [1.0] * 1025, [-1.0], [True], ["1"], [None],
                       [math.nan], [math.inf], [1e308, 1e308]):
            with self.subTest(values=str(values)[:70]), self.assertRaises(ValueError):
                callback.timing_statistics(values, 100.0)


class BenchmarkValidationTests(unittest.TestCase):
    def setUp(self):
        self.selected = profile()
        self.raw = benchmark(self.selected)

    def test_minimal_independent_fixture_and_optional_msvc_field_are_valid(self):
        self.assertTrue(callback.validate_benchmark(self.raw, self.selected)["valid"])
        self.raw["build"]["msvc_full_version"] = 193933523
        self.assertTrue(callback.validate_benchmark(self.raw, self.selected)["valid"])
        for value in (True, 0, 2**63):
            self.raw["build"]["msvc_full_version"] = value
            with self.subTest(value=value), self.assertRaises(ValueError):
                callback.validate_benchmark(self.raw, self.selected)

    def test_every_object_requires_exact_fields(self):
        paths = [(), ("identity",), ("environment",), ("build",), ("workload",),
                 ("timing",), ("timing", "summary"), ("integrity",)]
        for path in paths:
            for mutation in ("missing", "unexpected", "wrong_type"):
                with self.subTest(path=path, mutation=mutation):
                    raw = copy.deepcopy(self.raw)
                    target = field_at(raw, path)
                    if mutation == "missing":
                        del target[next(iter(target))]
                    elif mutation == "unexpected":
                        target["unexpected"] = None
                    elif path:
                        field_at(raw, path[:-1])[path[-1]] = []
                    else:
                        raw = []
                    with self.assertRaises(ValueError):
                        callback.validate_benchmark(raw, self.selected)

    def test_type_coercion_and_changed_q6_identity_are_rejected(self):
        changes = [
            (("schema_version",), True), (("complete",), 1),
            (("identity", "state_model"), 3.0), (("identity", "state_model"), 1),
            (("identity", "latency_frames"), 0), (("identity", "quadrature_nodes"), 4),
            (("identity", "observation_scheme"), "uniform-six-times"),
            (("environment", "machine_label"), "unmatched"),
            (("environment", "floating_point_rounding_code"), False),
            (("environment", "cpu_model"), "invented"), (("build", "ndebug"), 1),
            (("build", "fast_math_macro"), 0), (("workload", "measured_blocks"), 2.0),
            (("workload", "initial_sustain"), 0), (("workload", "initial_normalized_gain"), True),
            (("workload", "note_intensity_binary32"), 0.7),
            (("integrity", "successful_callbacks"), 2.0),
            (("integrity", "nonzero_channel_samples"), True),
            (("timing", "summary", "observations"), 2.0),
            (("timing", "summary", "deadline_misses"), False),
        ]
        for path, value in changes:
            with self.subTest(path=path, value=value):
                raw = copy.deepcopy(self.raw)
                field_at(raw, path[:-1])[path[-1]] = value
                with self.assertRaises(ValueError):
                    callback.validate_benchmark(raw, self.selected)

    def test_forged_summary_and_incomplete_phase_counters_are_rejected(self):
        for name in self.raw["timing"]["summary"]:
            with self.subTest(summary=name):
                raw = copy.deepcopy(self.raw)
                raw["timing"]["summary"][name] += 1
                with self.assertRaises(ValueError):
                    callback.validate_benchmark(raw, self.selected)
        for group, key, value in (("workload", "completed_warmup_blocks", 1),
                                  ("integrity", "warmup_success", False),
                                  ("integrity", "failed_callbacks", 1),
                                  ("integrity", "frame_cursor", 127),
                                  ("integrity", "last_result_code", 1)):
            with self.subTest(counter=key):
                raw = copy.deepcopy(self.raw)
                raw[group][key] = value
                with self.assertRaises(ValueError):
                    callback.validate_benchmark(raw, self.selected)
        self.raw["timing"]["raw_callback_us"].pop()
        with self.assertRaisesRegex(ValueError, "length"):
            callback.validate_benchmark(self.raw, self.selected)

    def test_all_fixed_profiles_have_independent_event_and_getter_counts(self):
        # At 256 measured blocks: interval 375/1500 fires once; interval 94
        # fires at block indices 0, 94, 188. Pedal offers on/off for each key.
        cases = [
            ("p01", 48000, 64, 0, "held", 64, 375, 0, 0, 0),
            ("p02", 48000, 64, 1, "held", 64, 375, 1, 0, 1),
            ("p03", 48000, 64, 12, "held", 64, 375, 12, 0, 12),
            ("p04", 48000, 64, 73, "held", 64, 375, 73, 0, 73),
            ("p05", 192000, 256, 0, "held", 64, 375, 0, 0, 0),
            ("p06", 192000, 256, 1, "held", 64, 375, 1, 0, 1),
            ("p07", 192000, 256, 12, "held", 64, 375, 12, 0, 12),
            ("p08", 192000, 256, 73, "held", 64, 375, 73, 0, 73),
            ("p09", 48000, 256, 12, "pedal", 64, 94, 72, 65536, 0),
            ("p10", 192000, 64, 73, "pedal", 32, 1500, 146, 16384, 0),
            ("p11", 192000, 64, 12, "gain", 32, 1500, 12, 16384, 12),
            ("p12", 48000, 256, 73, "gain", 64, 94, 219, 65536, 73),
        ]
        for pid, rate, frames, keys, workload, bits, interval, notes, points, owners in cases:
            with self.subTest(profile=pid):
                selected = profile(id=pid, sample_rate_hz=rate, block_frames=frames,
                                   active_keys=keys, workload=workload, sample_format_bits=bits,
                                   measured_blocks=256, warmup_blocks=16)
                raw = benchmark(selected, interval=interval, notes=notes, points=points,
                                owners=owners, silent=keys == 0)
                self.assertTrue(callback.validate_benchmark(raw, selected)["valid"])
                for group, key in (("workload", "offered_note_events"),
                                   ("workload", "offered_parameter_points"),
                                   ("integrity", "note_getter_reads"),
                                   ("integrity", "parameter_point_getter_reads"),
                                   ("integrity", "active_note_owners")):
                    changed = copy.deepcopy(raw)
                    changed[group][key] += 1
                    with self.assertRaises(ValueError):
                        callback.validate_benchmark(changed, selected)

    def test_retrigger_boundary_and_warmup_are_not_counted_twice(self):
        for blocks, notes in ((375, 1), (376, 2)):
            with self.subTest(blocks=blocks):
                selected = profile(measured_blocks=blocks, warmup_blocks=64)
                result = callback.validate_benchmark(benchmark(selected, notes=notes), selected)
                self.assertEqual(result["workload"]["offered_note_events"], notes)
        for blocks, notes, points in ((94, 24, 24064), (95, 48, 24320)):
            with self.subTest(pedal_blocks=blocks):
                selected = profile(block_frames=256, active_keys=12, workload="pedal",
                                   measured_blocks=blocks, warmup_blocks=64)
                self.assertTrue(callback.validate_benchmark(benchmark(
                    selected, interval=94, notes=notes, points=points, owners=0), selected)["valid"])

    def test_zero_key_and_one_frame_startup_silence_do_not_extend_to_longer_audio(self):
        cases = [(profile(active_keys=0), dict(notes=0, owners=0, silent=True)),
                 (profile(block_frames=1, measured_blocks=1), dict(interval=24000, silent=True)),
                 (profile(block_frames=1, measured_blocks=2), dict(interval=24000)),
                 (profile(block_frames=2, measured_blocks=1), dict(interval=12000))]
        for selected, kwargs in cases:
            with self.subTest(frames=selected["block_frames"], blocks=selected["measured_blocks"],
                              keys=selected["active_keys"]):
                raw = benchmark(selected, **kwargs)
                self.assertTrue(callback.validate_benchmark(raw, selected)["valid"])
                raw["integrity"]["nonzero_output_expected"] = not raw["integrity"]["nonzero_output_expected"]
                with self.assertRaises(ValueError):
                    callback.validate_benchmark(raw, selected)

    def test_output_presence_finiteness_stereo_and_fingerprint_are_checked(self):
        changes = [("nonzero_channel_samples", 0), ("nonzero_channel_samples", 1),
                   ("nonzero_channel_samples", 258), ("checksum_sum_squares", 0.0),
                   ("peak_abs_digital_output", 0.0), ("stereo_equal", False),
                   ("finite_output_and_checksum", False), ("silence_flags_correct", False),
                   ("output_presence_correct", False)]
        changes += [(key, value) for key in ("checksum_sum_squares", "peak_abs_digital_output")
                    for value in (-1, True, None, math.nan, math.inf)]
        changes += [("output_fnv1a64", value) for value in (None, "f" * 15, "F" * 16, "g" * 16)]
        for key, value in changes:
            with self.subTest(key=key, value=value):
                raw = copy.deepcopy(self.raw)
                raw["integrity"][key] = value
                with self.assertRaises(ValueError):
                    callback.validate_benchmark(raw, self.selected)


class ComparisonTests(unittest.TestCase):
    def setUp(self):
        self.plan = make_plan(profile_ids=["p02"], blocks=2, warmup_blocks=0, cycles=1)
        self.records = []
        for run, duration in zip(self.plan["runs"], (1.0, 3.0, 8.0, 2.0)):
            summary = {"observations": 2, "mean_us": duration, "p50_us": duration,
                       "p99_us": duration, "max_us": duration, "deadline_misses": 0}
            self.records.append({**run, "valid": True, "validation": {
                "summary": summary, "workload": {"fixture": "identical"},
                "output_identity": {"checksum": 0.5, "fingerprint": "identical"}}})

    def test_pairs_are_12_and_43_and_median_is_of_ratios(self):
        result = callback.matched_comparison(self.plan, self.records)
        pairs = result["pairs"]
        self.assertEqual([(p["baseline_run"], p["candidate_run"]) for p in pairs],
                         [(self.records[0]["id"], self.records[1]["id"]),
                          (self.records[3]["id"], self.records[2]["id"])])
        for key in ("p50_us", "p99_us", "max_us", "mean_us"):
            self.assertEqual(result["profiles"][0]["ratios"][key], {
                "all_pair_ratios": [3.0, 4.0], "median_pair_ratio": 3.5, "undefined_pairs": 0})
        observations = result["profiles"][0]["observations"]
        self.assertEqual(observations["baseline"]["median_of_run_p50_us"], 1.5)
        self.assertEqual(observations["candidate"]["median_of_run_p50_us"], 5.5)
        self.assertEqual(observations["candidate"]["maximum_observed_us"], 8.0)
        self.assertEqual(observations["candidate"]["callbacks"], 4)

    def test_zero_denominator_retains_null_reason_and_undefined_pair(self):
        for key in ("p50_us", "p99_us", "max_us", "mean_us"):
            self.records[0]["validation"]["summary"][key] = 0.0
        result = callback.matched_comparison(self.plan, self.records)
        self.assertEqual(result["pairs"][0]["candidate_over_baseline"]["mean_us"],
                         {"value": None, "reason": "zero baseline denominator"})
        self.assertEqual(result["profiles"][0]["ratios"]["mean_us"], {
            "all_pair_ratios": [None, 4.0], "median_pair_ratio": None, "undefined_pairs": 1})
        self.assertEqual(result["pairs"][0]["deadline_misses"]["candidate_over_baseline"],
                         {"value": None, "reason": "zero baseline denominator"})

    def test_missing_invalid_duplicate_or_exactly_mismatched_records_cannot_compare(self):
        variants = [self.records[:-1], copy.deepcopy(self.records), copy.deepcopy(self.records)]
        variants[1][1]["valid"] = False
        variants[2][1]["id"] = variants[2][0]["id"]
        for records in variants:
            with self.subTest(records=records), self.assertRaises(ValueError):
                callback.matched_comparison(self.plan, records)
        for field in ("workload", "output_identity"):
            changed = copy.deepcopy(self.records)
            changed[1]["validation"][field]["extra"] = math.nextafter(0.5, 1.0)
            with self.subTest(field=field), self.assertRaisesRegex(ValueError, "identity changed"):
                callback.matched_comparison(self.plan, changed)


class ControlledProcess:
    """In-memory pipes with deterministic exit, timeout and post-run mutation."""
    def __init__(self, stdout, stderr, *, returncode=0, pending=False, after_wait=None):
        self.stdout = io.BytesIO(stdout)
        self.stderr = io.BytesIO(stderr)
        self.returncode = returncode
        self.pending = pending
        self.after_wait = after_wait
        self.pid = 424242
        self.killed = False

    def poll(self):
        return None if self.pending and not self.killed else self.returncode

    def wait(self, timeout=None):
        if self.after_wait is not None:
            self.after_wait()
            self.after_wait = None
        if self.pending and not self.killed:
            raise subprocess.TimeoutExpired("mock child", timeout)
        return self.returncode

    def kill(self):
        self.killed = True
        self.returncode = -9


class CaptureTests(unittest.TestCase):
    @contextmanager
    def fixture(self, *, mode="normal", output_change=None, mutation=None):
        with tempfile.TemporaryDirectory() as temporary, ExitStack() as stack:
            root = Path(temporary) / "source"
            for name in ("include", "src", "plugins/vst3", "cmake", "tools", "apps",
                         "build/baseline", "build/candidate"):
                (root / name).mkdir(parents=True, exist_ok=True)
            script = root / "tools/compare_vst3_callbacks.py"
            source = root / "include/fixture.hpp"
            for path in (root / "CMakeLists.txt", root / "apps/vst3_benchmark.cpp", script, source):
                path.write_bytes(b"uncompiled source fixture\n")
            baseline, candidate = root / "build/baseline/bench", root / "build/candidate/bench"
            for binary in (baseline, candidate):
                binary.write_bytes(b"never executed fixture\n")
                binary.chmod(0o755)
            build_metadata = baseline.parent / "CMakeCache.txt"
            build_metadata.write_bytes(b"fixture metadata\n")
            stack.enter_context(mock.patch.object(callback, "ROOT", root))
            stack.enter_context(mock.patch.object(callback, "SCRIPT", script))
            plan = callback.make_plan(baseline=baseline, candidate=candidate, profile_ids=["p02"],
                                      blocks=2, warmup_blocks=0, cycles=1, timeout_seconds=1)
            plan_path = Path(temporary) / "plan.json"
            plan_path.write_text(json.dumps(plan), encoding="utf-8")
            affinity = {"status": "unsupported", "platform": "unit-fixture", "cpus": None}
            state = SimpleNamespace(plan=plan, plan_path=plan_path, output=Path(temporary) / "capture",
                                    calls=[], processes=[], stdout=[], stderr=[],
                                    source=source, baseline=baseline, candidate=candidate)

            def popen(argv, **kwargs):
                index = len(state.calls)
                state.calls.append((argv, kwargs))
                if mode == "launch_failure":
                    raise OSError("controlled launch failure")
                raw = benchmark(plan["profiles"][0])
                if output_change is not None:
                    output_change(index, raw)
                stdout = json.dumps(raw, allow_nan=False).encode() + b"\n"
                stderr = b"retained fixture stderr\x00\xff\n"
                if mode == "stdout_cap":
                    stdout = b"x" * (262144 + 17)
                elif mode == "stderr_cap":
                    stderr = b"y" * (65536 + 17)
                elif mode == "bad_json":
                    stdout = b'{"duplicate":1,"duplicate":2}\n'
                state.stdout.append(stdout)
                state.stderr.append(stderr)
                Path(argv[3]).write_text(json.dumps({
                    "exec_argv": argv[5:], "requested_cpu": None, "error": None,
                    "before": affinity, "after": affinity}), encoding="utf-8")

                def alter_input():
                    target = {"source": source, "baseline": baseline, "candidate": candidate,
                              "plan": plan_path, "build_metadata": build_metadata}.get(mutation)
                    if target is not None:
                        target.write_bytes(target.read_bytes() + b"changed\n")
                    elif mutation == "source_replacement":
                        replacement = source.with_suffix(".replacement")
                        replacement.write_bytes(source.read_bytes())
                        replacement.replace(source)

                child = ControlledProcess(stdout, stderr, returncode=23 if mode == "nonzero" else 0,
                                          pending=mode == "timeout",
                                          after_wait=alter_input if mutation and index == 0 else None)
                state.processes.append(child)
                return child

            stack.enter_context(mock.patch.object(callback, "affinity_snapshot", return_value=affinity))
            stack.enter_context(mock.patch.object(callback, "machine_snapshot", return_value={"fixture": True}))
            stack.enter_context(mock.patch.object(callback.subprocess, "Popen", side_effect=popen))
            stack.enter_context(mock.patch.object(callback.os, "killpg", create=True,
                                                  side_effect=lambda *args: state.processes[-1].kill()))
            if mode == "timeout":
                stack.enter_context(mock.patch.object(callback.time, "monotonic", side_effect=[0.0, 2.0, 2.0]))
            elif mode == "late_zero_exit":
                stack.enter_context(mock.patch.object(callback.time, "monotonic", side_effect=[0.0, 2.0]))
            yield state

    def execute(self, state):
        with redirect_stderr(io.StringIO()), redirect_stdout(io.StringIO()):
            code = callback.execute_plan(state.plan_path, state.output, "unit fixture; no process executes")
        return code, json.loads((state.output / "manifest.json").read_bytes())

    def assert_stopped(self, state, manifest, count):
        self.assertFalse(manifest["complete"])
        self.assertEqual(manifest["exit_code"], 1)
        self.assertEqual(len(state.calls), count)
        self.assertEqual(len(manifest["attempted_runs"]), count)
        self.assertEqual(manifest["unattempted_run_ids"], [r["id"] for r in state.plan["runs"][count:]])
        self.assertFalse((state.output / "comparison.json").exists())
        self.assertFalse(manifest["attempted_runs"][-1]["valid"])
        for run in manifest["attempted_runs"]:
            retained = json.loads((state.output / ("runs/" + run["id"] + ".execution.json")).read_bytes())
            self.assertEqual(retained, run)
        for name, expected in manifest["artifact_hashes"].items():
            raw = (state.output / name).read_bytes()
            self.assertEqual(len(raw), expected["bytes"])
            self.assertEqual(hashlib.sha256(raw).hexdigest(), expected["sha256"])

    def test_complete_mock_capture_retains_all_four_runs_and_comparison(self):
        with self.fixture() as state:
            code, manifest = self.execute(state)
            self.assertEqual(code, 0, manifest["errors"])
            self.assertTrue(manifest["complete"])
            self.assertEqual(len(state.calls), 4)
            self.assertEqual(manifest["unattempted_run_ids"], [])
            self.assertTrue(manifest["input_hashes_unchanged"])
            comparison = json.loads((state.output / "comparison.json").read_bytes())
            self.assertEqual(len(comparison["pairs"]), 2)
            self.assertEqual(comparison["profiles"][0]["ratios"]["mean_us"]["all_pair_ratios"], [1.0, 1.0])

    def test_stream_caps_retain_exact_prefixes_and_stop_without_retry(self):
        for mode, label, cap in (("stdout_cap", "stdout", 262144), ("stderr_cap", "stderr", 65536)):
            with self.subTest(mode=mode), self.fixture(mode=mode) as state:
                code, manifest = self.execute(state)
                self.assertEqual(code, 1)
                self.assert_stopped(state, manifest, 1)
                stream = manifest["attempted_runs"][0]["streams"][label]
                self.assertTrue(stream["truncated"])
                self.assertEqual(stream["observed_bytes"], cap + 17)
                self.assertEqual(stream["retained_bytes"], cap)
                self.assertEqual((state.output / stream["artifact"]).read_bytes(), getattr(state, label)[0][:cap])

    def test_timeout_and_late_zero_exit_remain_failed_with_raw_evidence(self):
        for mode in ("timeout", "late_zero_exit"):
            with self.subTest(mode=mode), self.fixture(mode=mode) as state:
                code, manifest = self.execute(state)
                self.assertEqual(code, 1)
                self.assert_stopped(state, manifest, 1)
                run = manifest["attempted_runs"][0]
                self.assertTrue(run["timed_out"])
                self.assertEqual(run["returncode"], -9 if mode == "timeout" else 0)
                self.assertEqual(state.processes[0].killed, mode == "timeout")
                self.assertEqual((state.output / run["streams"]["stdout"]["artifact"]).read_bytes(), state.stdout[0])

    def test_exit_launch_and_json_failures_preserve_attempt_and_stop(self):
        for mode in ("nonzero", "launch_failure", "bad_json"):
            with self.subTest(mode=mode), self.fixture(mode=mode) as state:
                code, manifest = self.execute(state)
                self.assertEqual(code, 1)
                self.assert_stopped(state, manifest, 1)
                run = manifest["attempted_runs"][0]
                for label in ("stdout", "stderr"):
                    expected = b"" if mode == "launch_failure" else getattr(state, label)[0]
                    self.assertEqual((state.output / run["streams"][label]["artifact"]).read_bytes(), expected)
                self.assertEqual(run["returncode"], None if mode == "launch_failure" else 23 if mode == "nonzero" else 0)

    def test_exact_candidate_and_repeat_output_mismatch_suppresses_summary(self):
        changes = [(1, "checksum_sum_squares", math.nextafter(0.5, 1.0)),
                   (1, "peak_abs_digital_output", math.nextafter(0.5, 1.0)),
                   (1, "nonzero_channel_samples", 4),
                   (1, "output_fnv1a64", "fedcba9876543210"),
                   (2, "output_fnv1a64", "fedcba9876543210")]
        for changed_index, key, value in changes:
            def alter(index, raw):
                if index == changed_index:
                    raw["integrity"][key] = value
            with self.subTest(index=changed_index, key=key), self.fixture(output_change=alter) as state:
                code, manifest = self.execute(state)
                self.assertEqual(code, 1)
                self.assert_stopped(state, manifest, changed_index + 1)
                failed = manifest["attempted_runs"][-1]
                self.assertTrue(failed["validation"]["valid"])
                self.assertIn("output fingerprint changed", " ".join(failed["errors"]))

    def test_source_binary_plan_metadata_and_same_bytes_replacement_are_detected(self):
        for mutation in ("source", "baseline", "candidate", "plan", "build_metadata", "source_replacement"):
            with self.subTest(mutation=mutation), self.fixture(mutation=mutation) as state:
                code, manifest = self.execute(state)
                self.assertEqual(code, 1)
                self.assert_stopped(state, manifest, 1)
                self.assertFalse(manifest["input_hashes_unchanged"])
                self.assertIn("changed during run", " ".join(manifest["attempted_runs"][0]["errors"]))
                if mutation == "source_replacement":
                    before = json.loads((state.output / "input-hashes-before.json").read_bytes())
                    after = json.loads((state.output / "input-hashes-after.json").read_bytes())
                    # Source snapshots deliberately retain native relative paths.
                    source_key = str(state.source.relative_to(callback.ROOT))
                    first = before["source"]["files"][source_key]
                    last = after["source"]["files"][source_key]
                    self.assertEqual(first["sha256"], last["sha256"])
                    self.assertNotEqual(first, last)


class StorageRefusalTests(unittest.TestCase):
    def test_existing_file_or_directory_is_preserved_before_plan_read_or_launch(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            regular = root / "file"
            regular.write_bytes(b"untouched")
            directory = root / "directory"
            directory.mkdir()
            (directory / "sentinel").write_bytes(b"untouched")
            for output in (regular, directory):
                with self.subTest(output=output.name), \
                        mock.patch.object(callback, "load_plan") as load, \
                        mock.patch.object(callback.subprocess, "Popen") as popen, \
                        redirect_stderr(io.StringIO()):
                    code = callback.main(["--run-plan", str(root / "missing-plan"),
                                          "--output-dir", str(output), "--competing-activity", "unit fixture"])
                    self.assertEqual(code, 2)
                    load.assert_not_called()
                    popen.assert_not_called()
            self.assertEqual(regular.read_bytes(), b"untouched")
            self.assertEqual(list(directory.iterdir()), [directory / "sentinel"])
            self.assertEqual((directory / "sentinel").read_bytes(), b"untouched")

    def test_dangling_output_symlink_is_refused(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            output = root / "link"
            try:
                output.symlink_to(root / "absent-target", target_is_directory=True)
            except (NotImplementedError, OSError) as error:
                self.skipTest("symlinks unavailable: " + str(error))
            with self.assertRaisesRegex(ValueError, "refusing existing path"):
                callback.execute_plan(root / "missing-plan", output, "unit fixture")
            self.assertTrue(output.is_symlink())
            self.assertFalse((root / "absent-target").exists())

    def test_absent_parent_and_empty_activity_do_not_create_output(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            for output, activity in ((root / "absent/capture", "fixture"),
                                     (root / "capture", "   "), (root / "capture", "x" * 4097)):
                with self.subTest(activity=len(activity)), self.assertRaises(ValueError):
                    callback.execute_plan(root / "plan", output, activity)
                self.assertFalse(output.exists())

    def test_artifact_exclusive_write_and_mutation_verification(self):
        with tempfile.TemporaryDirectory() as temporary:
            store = callback.EvidenceStore(Path(temporary))
            store.write("retained", b"original")
            with self.assertRaises(FileExistsError):
                store.write("retained", b"overwrite")
            self.assertEqual((store.directory / "retained").read_bytes(), b"original")
            store.verify()
            (store.directory / "retained").write_bytes(b"mutation")
            with self.assertRaisesRegex(ValueError, "artifact mutated"):
                store.verify()

    def test_windows_artifact_paths_keep_exact_inventory_validation(self):
        with tempfile.TemporaryDirectory() as temporary:
            store = callback.EvidenceStore(Path(temporary))
            store.write("runs/retained.json", b"original")

            def entry(name):
                path = mock.Mock()
                path.is_dir.return_value = False
                path.relative_to.return_value = PureWindowsPath(name)
                return path

            retained = entry("runs/retained.json")
            # Only directory enumeration is simulated; hashing reads real bytes.
            with mock.patch.object(Path, "rglob", return_value=[retained]):
                store.verify()
            retained.relative_to.assert_called_once_with(store.directory)
            for names in ([], [retained, entry("runs/unexpected.json")]):
                with self.subTest(count=len(names)), \
                        mock.patch.object(Path, "rglob", return_value=names), \
                        self.assertRaisesRegex(ValueError, "unexpected or missing retained artifact"):
                    store.verify()

    def test_capture_budget_preserves_final_record_reserve(self):
        with tempfile.TemporaryDirectory() as temporary, \
                mock.patch.object(callback, "MAX_CAPTURE_BYTES", 64), \
                mock.patch.object(callback, "FINAL_RESERVE_BYTES", 16):
            store = callback.EvidenceStore(Path(temporary))
            store.reserve(48)
            with self.assertRaisesRegex(ValueError, "capture byte cap"):
                store.reserve(1)
            self.assertEqual(store.used, 48)
            store.reserve(16, final=True)
            self.assertEqual(store.used, 64)
            with self.assertRaises(ValueError):
                store.reserve(1, final=True)


if __name__ == "__main__":
    unittest.main()
