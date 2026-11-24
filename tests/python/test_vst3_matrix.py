"""Evidence integrity checks for the frozen callback matrix launcher."""
from __future__ import annotations

import copy
from contextlib import redirect_stderr
import importlib.util
import io
from pathlib import Path
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
SPEC = importlib.util.spec_from_file_location("matrix", ROOT / "tools/run_vst3_matrix.py")
matrix = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(matrix)


class MatrixEvidenceTests(unittest.TestCase):
    def setUp(self):
        self.plan, _ = matrix.load_plan()
        self.profile = self.plan["profiles"][0]
        self.variant = self.plan["variants"][0]
        values = [2.0] * 512
        self.raw = {
            "schema_version": 1, "benchmark": "synthetic_vst3_process", "complete": True,
            "environment": {"machine_label": "q6-same-host-matrix-20261123", "platform": "Linux"},
            "build": {"configuration": "Release", "ndebug": True, "fast_math_macro": False},
            "identity": {"plugin_name": "Rhodes Research Prototype", "state_model": 1,
                         "latency_frames": 0, "oversampling_factor": 1,
                         "internal_sample_rate_hz": 48000},
            "workload": {"id": "held", "sample_format_bits": 32, "sample_rate_hz": 48000,
                "block_frames": 64, "requested_blocks": 512, "measured_blocks": 512,
                "requested_warmup_blocks": 32, "completed_warmup_blocks": 32,
                "selected_key_count": 0, "retrigger_interval_blocks": 375,
                "offered_note_events": 0, "offered_parameter_points": 0,
                "initial_normalized_gain": 0.5, "initial_sustain": False,
                "note_intensity_binary32": 0.699999988079071},
            "integrity": {"warmup_success": True, "successful_callbacks": 512,
                "failed_callbacks": 0, "last_result_code": 0, "frame_cursor": 32768,
                "active_note_owners": 0, "expected_note_owners": 0,
                "note_getter_reads": 0, "parameter_point_getter_reads": 0,
                "finite_output_and_checksum": True, "stereo_equal": True,
                "silence_flags_correct": True, "output_presence_correct": True,
                "nonzero_output_expected": False, "nonzero_channel_samples": 0,
                "checksum_sum_squares": 0.0, "peak_abs_digital_output": 0.0,
                "output_fnv1a64": "1234567890abcdef"},
            "timing": {"clock": "std::chrono::steady_clock", "raw_callback_us": values,
                       "summary": matrix.statistics(values, 1e6 * 64 / 48000)},
        }

    def verify(self, raw=None, variant=None):
        return matrix.verify_result(raw or self.raw, self.profile, variant or self.variant)

    def test_frozen_plan_and_valid_synthetic_output(self):
        self.assertEqual(len(self.plan["runs"]), 72)
        self.assertTrue(self.verify()["valid"])

    def test_counter_claims_do_not_replace_actual_workload(self):
        for group, name, value in [
            ("workload", "measured_blocks", 511),
            ("workload", "completed_warmup_blocks", 31),
            ("integrity", "frame_cursor", 32767),
            ("integrity", "note_getter_reads", 1),
            ("integrity", "parameter_point_getter_reads", 1),
            ("integrity", "active_note_owners", 1),
            ("integrity", "stereo_equal", False),
            ("integrity", "nonzero_channel_samples", 1),
            ("identity", "latency_frames", 48),
            ("environment", "machine_label", "uncontrolled"),
            ("build", "configuration", "Debug"),
            ("timing", "clock", "unknown"),
        ]:
            with self.subTest(group=group, name=name):
                raw = copy.deepcopy(self.raw)
                raw[group][name] = value
                self.assertFalse(self.verify(raw)["valid"])

    def test_invalid_numerics_types_and_forged_summary(self):
        self.assertFalse(matrix.verify_result([], self.profile, self.variant)["valid"])
        for value in (float("nan"), float("inf"), -1.0, True):
            raw = copy.deepcopy(self.raw)
            raw["timing"]["raw_callback_us"][0] = value
            self.assertFalse(self.verify(raw)["valid"])
        raw = copy.deepcopy(self.raw)
        raw["timing"]["summary"]["max_us"] = 1.0
        self.assertFalse(self.verify(raw)["valid"])
        raw = copy.deepcopy(self.raw)
        raw["complete"] = 1
        self.assertFalse(self.verify(raw)["valid"])

    def test_slow_callback_is_retained_without_failing_numerical_integrity(self):
        self.raw["timing"]["raw_callback_us"][5] = 2000.0
        self.raw["timing"]["summary"] = matrix.statistics(
            self.raw["timing"]["raw_callback_us"], 1e6 * 64 / 48000)
        result = self.verify()
        self.assertTrue(result["valid"])
        self.assertEqual(result["summary"]["deadline_misses"], 1)
        self.assertEqual(result["summary"]["max_us"], 2000.0)

    def test_quadrature_identity_rejects_uniform_rate_claims(self):
        variant = self.plan["variants"][2]
        self.raw["identity"] = {k: variant[k] for k in (
            "plugin_name", "state_model", "latency_frames", "quadrature_nodes", "observation_scheme")}
        self.assertTrue(self.verify(variant=variant)["valid"])
        self.raw["identity"]["oversampling_factor"] = 6
        self.assertFalse(self.verify(variant=variant)["valid"])

    def test_nearest_rank_and_strict_period_rule(self):
        result = matrix.statistics([7.0, 2.0, 3.0, 1.0], 3.0)
        self.assertEqual(result["p50_us"], 2.0)
        self.assertEqual(result["p95_us"], 7.0)
        self.assertEqual(result["deadline_misses"], 1)
        self.assertEqual(result["mean_us"], 3.25)

    def test_existing_evidence_directory_is_not_overwritten(self):
        with tempfile.TemporaryDirectory() as folder:
            path = Path(folder)
            (path / "sentinel").write_text("retained")
            with redirect_stderr(io.StringIO()):
                code = matrix.main(["--output", folder, "--sdk", folder,
                                    "--competing-activity", "unit test; no executable"])
            self.assertEqual(code, 3)
            self.assertEqual(list(path.iterdir()), [path / "sentinel"])
            self.assertEqual((path / "sentinel").read_text(), "retained")


if __name__ == "__main__":
    unittest.main()
