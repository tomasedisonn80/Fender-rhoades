"""Independent diagnostic checks; no full or pilot waveform study is run here.

The plan/protocol tests need only Python's stdlib. Numerical selfchecks skip
without installed optional NumPy/SciPy. Set RHODES_AUDIO_PROBE to exercise three
small 160-frame production/reference comparisons; this is not matrix coverage.
"""
from __future__ import annotations

from collections import Counter
from contextlib import redirect_stderr, redirect_stdout
import importlib.util
import io
import json
import math
import os
from pathlib import Path
import re
import subprocess
import sys
import tempfile
import unittest
from unittest import mock


ROOT = Path(__file__).resolve().parents[2]
TOOL = ROOT / "tools/validate_filtered_audio.py"
SPEC = importlib.util.spec_from_file_location("filtered_audio_reference", TOOL)
if SPEC is None or SPEC.loader is None:
    raise RuntimeError("Cannot load filtered audio reference")
reference = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = reference
SPEC.loader.exec_module(reference)
HAVE_SCIENCE = (importlib.util.find_spec("numpy") is not None
                and importlib.util.find_spec("scipy") is not None)
PROBE = os.environ.get("RHODES_AUDIO_PROBE")


class FilteredAudioPlanTests(unittest.TestCase):
    def test_full_matrix_and_exact_rate_boundaries(self):
        cases = reference.build_cases("full")
        self.assertEqual(len(cases), 2097)
        self.assertEqual(sum(case.frames for case in cases), 2534400)
        self.assertEqual(Counter(case.family for case in cases),
                         {"attack": 1971, "events": 72, "chord": 54})
        self.assertEqual(len({case.id for case in cases}), len(cases))
        for rate in reference.RATES:
            attacks = [case for case in cases if case.sample_rate_hz == rate and case.family == "attack"]
            self.assertEqual({(case.keys[0], case.intensity) for case in attacks},
                             {(key, intensity) for key in range(73) for intensity in (.1, .7, 1.)})
        self.assertGreater(reference.RATES[3], 48000.0)
        self.assertGreater(reference.RATES[7], 96000.0)
        self.assertEqual([reference.oversampling_factor(rate) for rate in reference.RATES],
                         [4, 4, 4, 2, 2, 2, 2, 1, 1])

    def test_pilot_is_exact_predeclared_subset_and_plan_hash_is_deterministic(self):
        full = {case.id: case.document() for case in reference.build_cases("full")}
        pilot = reference.build_cases("pilot")
        self.assertEqual((len(pilot), sum(case.frames for case in pilot)), (36, 92160))
        self.assertEqual(Counter(case.family for case in pilot),
                         {"attack": 18, "events": 12, "chord": 6})
        for case in pilot:
            self.assertEqual(case.intensity, 1.0)
            self.assertEqual(case.document(), full[case.id])
        plan = reference.make_plan("full")
        digest = plan.pop("plan_sha256")
        self.assertEqual(digest, reference.object_hash(plan))
        self.assertEqual(digest, reference.make_plan("full")["plan_sha256"])
        self.assertEqual(plan["gates"], {
            "comparison_a_peak_volts": 1e-12,
            "reference_refinement_peak_volts": 1e-9,
            "reference_refinement_rms_volts": 1e-10,
            "comparison_b_exploratory_peak_volts": 1e-4,
            "comparison_b_exploratory_rms_volts": 1e-5,
        })

    def test_script_globals_once_and_same_offset_reset_before_all_strikes(self):
        events = reference.event_script(tuple(range(73)), .7)
        self.assertEqual(len(events), 736)
        self.assertEqual(Counter(event.type for event in events),
                         {"on": 511, "off": 219, "sustain": 4, "all_off": 1, "reset": 1})
        self.assertEqual([e.offset for e in events if e.type == "sustain"], [255, 511, 2048, 2560])
        at_reset = [e for e in events if e.offset == 1023]
        self.assertEqual(at_reset[0], reference.Event(1023, "reset"))
        self.assertEqual([e.key for e in at_reset[1:]], list(range(73)))
        self.assertEqual([e.value for e in events if e.offset == 2303], [0.0] * 73)
        self.assertEqual(reference.reset_epochs(4096, events), ((0, 1023), (1023, 4096)))
        self.assertEqual(len(reference.event_script(reference.SPARSE_KEYS, 1.0)), 76)

    def test_help_and_both_plans_work_without_site_packages(self):
        for arguments in (("--help",), ("--plan", "--suite", "full"),
                          ("--plan", "--suite", "pilot")):
            with self.subTest(arguments=arguments):
                process = subprocess.run([sys.executable, "-S", str(TOOL), *arguments],
                                         capture_output=True, text=True, check=False)
                self.assertEqual(process.returncode, 0, process.stderr)
                if "--plan" in arguments:
                    plan = json.loads(process.stdout)
                    self.assertIn(plan["case_count"], (36, 2097))
                    self.assertTrue(all(float.fromhex(case["sample_rate_binary64_hex"]) == case["sample_rate_hz"]
                                        for case in plan["cases"]))

    def test_input_rejections_do_not_reorder_or_coerce_events(self):
        for args in ((-1, "on", 0, 1.), (0, "on", 73, 1.),
                     (True, "on", 0, 1.), (0, "sustain", 4, 1.),
                     (0, "sustain", 0, .5), (0, "reset", 0, 1.),
                     (0, "off", 0, 1.), (0, "on", 0, math.nan),
                     (0, "on", 0, True), (0, "unknown", 0, 0.)):
            with self.subTest(args=args), self.assertRaises(ValueError):
                reference.Event(*args)
        events = (reference.Event(2, "on", 0, 1.), reference.Event(1, "off", 0))
        with self.assertRaises(ValueError):
            reference.Case("unordered", "check", 48000., 3, (0,), 1., events)

    def test_output_directory_and_exclusive_file_never_overwrite(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            output = root / "result"
            reference.create_output(output)
            marker = output / "marker"
            reference.write_exclusive(marker, b"keep\n")
            with self.assertRaises(ValueError):
                reference.create_output(output)
            with self.assertRaises(FileExistsError):
                reference.write_exclusive(marker, b"replace\n")
            self.assertEqual(marker.read_bytes(), b"keep\n")
            link = root / "result-link"
            try:
                link.symlink_to(output, target_is_directory=True)
            except (OSError, NotImplementedError):
                return
            with self.assertRaises(ValueError):
                reference.create_output(link)


@unittest.skipUnless(HAVE_SCIENCE, "Optional offline numerical selfchecks need NumPy/SciPy")
class FilteredAudioNumericalTests(unittest.TestCase):
    def setUp(self):
        import numpy as np
        self.np = np

    def small_case(self, events=None, frames=160, key=0, rate=48000.):
        if events is None:
            events = (reference.Event(16, "on", key, 1.),)
        return reference.Case("focused-check", "selfcheck", rate, frames, (key,), 1., events)

    def test_analytic_modes_against_independent_matrix_exponential(self):
        from scipy.linalg import expm
        np = self.np
        q0, v0 = np.asarray([1e-5, -2e-5, 7e-6]), np.asarray([.012, -.006, .002])
        omega, beta = np.asarray([250., 950., 14000.]), np.asarray([.8, 35., 45.])
        for seconds in (0., .00001, .003, .08):
            q, v = reference.modal_solution(q0, v0, omega, beta, seconds)
            for mode in range(3):
                matrix = np.asarray([[0., 1.], [-omega[mode] ** 2, -2 * beta[mode]]])
                expected = expm(matrix * seconds) @ np.asarray([q0[mode], v0[mode]])
                np.testing.assert_allclose([q[mode], v[mode]], expected, rtol=2e-12, atol=1e-14)

    def test_retrigger_release_pedal_and_all_off_preserve_mechanics(self):
        np = self.np
        initial = (reference.Event(0, "on", 0, 1.),)
        events = (*initial, reference.Event(10, "sustain", 0, 1.),
                  reference.Event(11, "off", 0), reference.Event(12, "all_off"),
                  reference.Event(13, "sustain", 0, 0.), reference.Event(14, "on", 0, 0.),
                  reference.Event(15, "on", 0, .7))
        trajectory = reference.KeyTrajectory(48000., 0, events)
        self.assertEqual([segment.start_frame for segment in trajectory.segments], [0, 10, 11, 12, 13, 14, 15])
        for segment in trajectory.segments[:4]:
            np.testing.assert_array_equal(segment.beta, [.8, 3., 7.])
        for segment in trajectory.segments[4:6]:
            np.testing.assert_array_equal(segment.beta, [30., 35., 45.])
        before = reference.KeyTrajectory(48000., 0, events[:-1])
        q_before, v_before = before.states(np.asarray([15.]))
        q_after, v_after = trajectory.states(np.asarray([15.]))
        np.testing.assert_allclose(q_after, q_before, rtol=0., atol=1e-18)
        np.testing.assert_allclose(v_after - v_before, np.asarray([[.06, .027, .012]]) * .7,
                                   rtol=0., atol=2e-17)
        zero_on = reference.KeyTrajectory(48000., 0, (*initial, reference.Event(7, "on", 0, 0.)))
        off = reference.KeyTrajectory(48000., 0, (*initial, reference.Event(7, "off", 0)))
        np.testing.assert_array_equal(zero_on.loaded_volts(np.arange(40)), off.loaded_volts(np.arange(40)))

    def test_reset_order_erases_prior_motion_and_history_without_alignment(self):
        np = self.np
        events = (reference.Event(0, "on", 0, 1.), reference.Event(100, "reset"),
                  reference.Event(100, "on", 0, .7))
        case = self.small_case(events=events, frames=260)
        curves = reference.ReferenceBank().curves(case)
        fresh = self.small_case(events=(reference.Event(0, "on", 0, .7),), frames=160)
        fresh_curves = reference.ReferenceBank().curves(fresh)
        for name, values in curves.items():
            start = 400 if name == "source_internal_volts" else 100
            np.testing.assert_allclose(values[start:], fresh_curves[name], rtol=0., atol=2e-16)
        reversed_events = (reference.Event(100, "on", 0, .7), reference.Event(100, "reset"))
        reversed_case = self.small_case(events=reversed_events, frames=160)
        for values in reference.ReferenceBank().curves(reversed_case).values():
            np.testing.assert_array_equal(values, np.zeros_like(values))

    def test_local_nonlinearity_then_loaded_sum(self):
        np = self.np
        events = (reference.Event(0, "on", 0, 1.), reference.Event(0, "on", 4, 1.))
        case = reference.Case("two-keys", "selfcheck", 48000., 100, (0, 4), 1., events)
        total = reference.ReferenceBank().curves(case)
        for name in total:
            single_values = [reference.ReferenceBank().curves(
                self.small_case(events=(reference.Event(0, "on", key, 1.),), frames=100, key=key))[name]
                             for key in (0, 4)]
            np.testing.assert_allclose(total[name], single_values[0] + single_values[1], rtol=0., atol=0.)
        self.assertAlmostEqual(total["source_internal_volts"][0], -.027624 * 12000 / 12019 * (1 / 4 + 1 / 3), delta=1e-17)
        # A nonlinear conversion of global motion would introduce cross-key products.
        a, b = (reference.KeyTrajectory(48000., key, events) for key in (0, 4))
        qa, va = a.states(np.asarray([80.]))
        qb, vb = b.states(np.asarray([80.]))
        observation = np.asarray([1., .3, .08])
        x, v = float(np.sum((qa / 4 + qb / 3) * observation)), float(np.sum((va / 4 + vb / 3) * observation))
        wrong = -(.4 + 1600 * x + 1200000 * x * x) * v * 12000 / 12019
        self.assertGreater(abs(wrong - total["source_internal_volts"][320]), 1e-7)

    def test_fir_recipe_matches_frozen_serialization_without_importing_it(self):
        np = self.np
        text = (ROOT / "include/rhodes/audio_filter_coefficients.hpp").read_text()
        for factor in (1, 2, 4):
            body = re.search(rf"coefficients_l{factor} \{{(.*?)\}};", text, re.DOTALL).group(1)
            production = np.asarray([float.fromhex(token.strip()) for token in body.split(",") if token.strip()])
            independent = reference.independent_fir(factor)
            self.assertLessEqual(float(np.max(np.abs(production - independent))), 5e-15)
            self.assertAlmostEqual(math.fsum(independent), 1., delta=3e-16)
            np.testing.assert_array_equal(independent, independent[::-1])

    def test_unit_cell_kernel_has_correct_zero_history_delay_and_reset(self):
        np = self.np
        epochs = ((0, 120), (120, 220))
        for order in (16, 32, 64):
            output = reference.continuous_reference(lambda t: np.where(t >= 0, 1., 0.),
                                                     220, epochs, reference.quadrature(order))
            for onset in (0, 120):
                self.assertEqual(output[onset], 0.)
                self.assertAlmostEqual(output[onset + 48], .5, delta=2e-15)
                self.assertAlmostEqual(output[onset + 96], 1., delta=2e-15)

    def test_continuous_result_against_separate_adaptive_integrals(self):
        from scipy.integrate import quad
        np = self.np
        case = self.small_case(key=72, rate=math.nextafter(96000., math.inf))
        trajectory = reference.KeyTrajectory(case.sample_rate_hz, 72, case.events)
        curves = reference.ReferenceBank().curves(case)
        # Integrate only causally nonzero cells and retain the declared delay.
        frame = 68
        cells = [quad(lambda s: float(reference.kernel(s)) *
                      float(trajectory.loaded_volts(np.asarray(frame - s))),
                      j, j + 1, epsabs=1e-15, epsrel=1e-12)[0]
                 for j in range(frame - 16)]
        expected = math.fsum(cells) / reference.kernel_normalizer_report()["adaptive_cell_quad"]
        self.assertAlmostEqual(float(curves["continuous_q64_volts"][frame]), expected, delta=2e-13)
        self.assertEqual(float(curves["continuous_q64_volts"][16]), 0.)
        for order in (16, 32, 64):
            self.assertTrue(np.all(np.isfinite(curves[f"continuous_q{order}_volts"])))

    def test_both_refinement_gates_are_mandatory_and_failures_remain_inconclusive(self):
        np = self.np
        case = self.small_case(events=(reference.Event(10, "on", 0, 1.),), frames=120)
        for broken in ("continuous_q16_volts", "continuous_q32_volts", "continuous_q64_volts"):
            curves = {name: np.zeros(120) for name in
                      ("same_rate_volts", "continuous_q16_volts", "continuous_q32_volts", "continuous_q64_volts")}
            curves[broken][20] = 2e-8
            result = reference.assess_case(case, np.zeros(120), curves)
            self.assertEqual(result["comparison_a"]["status"], "pass")
            self.assertFalse(result["reference_refinement"]["resolved"])
            self.assertEqual(result["comparison_b"]["status"], "inconclusive: reference not resolved")
            self.assertIsNone(result["comparison_b"]["metrics"])
            self.assertIsNone(result["event_windows"][0]["metrics"]["comparison_b"])
            self.assertEqual(len(result["raw_array_sha256_little_endian_float64"]), 5)

    def test_refinement_rms_and_exploratory_rms_are_separate_limits(self):
        np = self.np
        case = self.small_case(frames=160)
        curves = {name: np.zeros(160) for name in
                  ("same_rate_volts", "continuous_q16_volts", "continuous_q32_volts", "continuous_q64_volts")}
        curves["continuous_q16_volts"][:] = 5e-10
        result = reference.assess_case(case, np.zeros(160), curves)
        metrics = result["reference_refinement"]["metrics"]["q16_q32"]
        self.assertLess(metrics["maximum_absolute_error_volts"], 1e-9)
        self.assertGreater(metrics["rms_error_volts"], 1e-10)
        self.assertEqual(result["comparison_b"]["status"], "inconclusive: reference not resolved")
        curves["continuous_q16_volts"][:] = 0.
        result = reference.assess_case(case, np.full(160, 2e-5), curves)
        self.assertLess(result["comparison_b"]["metrics"]["maximum_absolute_error_volts"], 1e-4)
        self.assertEqual(result["comparison_b"]["status"], "fail")

    def test_unresolved_reference_raw_arrays_and_hashes_are_retained(self):
        np = self.np
        case = self.small_case(frames=20)
        curves = {name: np.zeros(20) for name in
                  ("same_rate_volts", "continuous_q16_volts", "continuous_q32_volts", "continuous_q64_volts")}
        curves["continuous_q16_volts"][19] = 1e-6
        valid = {"schema_version": 1, "model": "synthetic-filtered-audio", "frames": 20,
                 "event_count": 1, "oversampling_factor": 4, "latency_frames": 48,
                 "sample_rate_hz": 48000., "internal_sample_rate_hz": 192000.,
                 "samples_volts": [0.] * 20}
        bank = mock.Mock()
        bank.curves.return_value = curves
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory) / "focused"
            reference.create_output(output)
            with mock.patch.object(reference, "run_probe", return_value=(reference.canonical_bytes(valid), b"")):
                result = reference.execute_case(Path(sys.executable), output, case, bank, "selfcheck")
            self.assertEqual(result["comparison_b"]["status"], "inconclusive: reference not resolved")
            path = output / result["raw_arrays_path"]
            self.assertEqual(reference.file_hash(path), result["raw_npz_file_sha256"])
            with np.load(path, allow_pickle=False) as raw:
                np.testing.assert_array_equal(raw["continuous_q16_volts"], curves["continuous_q16_volts"])
                self.assertEqual(reference.array_hash(raw["continuous_q16_volts"]),
                                 result["raw_array_sha256_little_endian_float64"]["continuous_q16_volts"])

    def test_incomplete_run_preserves_bad_stdout_and_nonzero_probe_failures(self):
        for behavior in (dict(return_value=(b"invalid json\n", b"diagnostic\n")),
                         dict(side_effect=reference.ProbeFailure("failed probe", b"partial\n", b"error\n"))):
            with self.subTest(behavior=behavior), tempfile.TemporaryDirectory() as directory:
                output = Path(directory) / "incomplete"
                with mock.patch.object(reference, "run_probe", **behavior), \
                        mock.patch.object(reference, "source_snapshot", return_value={"selfcheck": "fixed"}), \
                        redirect_stdout(io.StringIO()), redirect_stderr(io.StringIO()):
                    code = reference.run_study(Path(sys.executable), output, "pilot")
                self.assertEqual(code, 3)
                summary = json.loads((output / "summary.json").read_text())
                self.assertEqual(summary["status"], "incomplete")
                self.assertEqual(summary["completed_cases"], 0)
                self.assertEqual(summary["expected_cases"], 36)
                first = reference.build_cases("pilot")[0]
                self.assertTrue((output / "probe" / f"{first.id}.stdin.txt").is_file())
                self.assertNotEqual((output / "probe" / f"{first.id}.stdout.json").read_bytes(), b"")
                self.assertNotEqual((output / "probe" / f"{first.id}.stderr.txt").read_bytes(), b"")

    def test_full_record_and_inclusive_event_window_metrics(self):
        np = self.np
        events = (reference.Event(10, "reset"), reference.Event(10, "on", 0, 1.))
        case = self.small_case(events=events, frames=120)
        curves = {name: np.zeros(120) for name in
                  ("same_rate_volts", "continuous_q16_volts", "continuous_q32_volts", "continuous_q64_volts")}
        production = np.zeros(120)
        production[106], production[107] = -2e-4, 3e-4
        result = reference.assess_case(case, production, curves)
        self.assertEqual(result["comparison_b"]["status"], "fail")
        metrics = result["comparison_b"]["metrics"]
        self.assertEqual(metrics["maximum_error_frame"], 107)
        self.assertEqual(metrics["maximum_error_time_seconds"], 107 / 48000.)
        self.assertEqual(metrics["signed_error_at_maximum_volts"], 3e-4)
        self.assertEqual(result["event_windows"][0]["event_indices"], [0, 1])
        inside = result["event_window_union"]["comparison_b"]
        outside = result["outside_event_window_union"]["comparison_b"]
        self.assertEqual((inside["sample_count"], outside["sample_count"]), (97, 23))
        self.assertEqual(inside["maximum_error_frame"], 106)
        self.assertEqual(outside["maximum_error_frame"], 107)
        self.assertIsNone(reference.error_metrics(production, production, 48000., [])["rms_error_volts"])

    def test_probe_metadata_samples_and_raw_hash_precision(self):
        np = self.np
        case = self.small_case(frames=20, rate=math.nextafter(48000., math.inf))
        valid = {"schema_version": 1, "model": "synthetic-filtered-audio", "frames": 20,
                 "event_count": 1, "oversampling_factor": 2, "latency_frames": 48,
                 "sample_rate_hz": case.sample_rate_hz,
                 "internal_sample_rate_hz": case.sample_rate_hz * 2, "samples_volts": [0.] * 20}
        samples, metadata = reference.parse_probe_output(reference.canonical_bytes(valid), case)
        self.assertEqual(metadata["sample_rate_hz"], case.sample_rate_hz)
        self.assertEqual(reference.array_hash(samples.astype(">f8")), reference.array_hash(samples.astype("<f8")))
        for key, wrong in (("sample_rate_hz", 48000.), ("oversampling_factor", 4),
                           ("latency_frames", 47), ("frames", True),
                           ("samples_volts", [False] * 20), ("samples_volts", [0.] * 19)):
            with self.subTest(key=key, wrong=wrong), self.assertRaises(ValueError):
                reference.parse_probe_output(reference.canonical_bytes({**valid, key: wrong}), case)
        with self.assertRaises(ValueError):
            reference.parse_probe_output(b'{"samples_volts":[NaN]}', case)

    @unittest.skipUnless(PROBE, "Set RHODES_AUDIO_PROBE for three small independent binary comparisons")
    def test_three_small_production_comparisons_keep_exact_internal_phase(self):
        bank = reference.ReferenceBank()
        for rate, key in ((32000., 0), (math.nextafter(48000., math.inf), 72),
                          (math.nextafter(96000., math.inf), 72)):
            case = self.small_case(rate=rate, key=key)
            raw, _ = reference.run_probe(Path(PROBE).resolve(), case)
            production, _ = reference.parse_probe_output(raw, case)
            result = reference.assess_case(case, production, bank.curves(case))
            self.assertEqual(result["comparison_a"]["status"], "pass", result["comparison_a"])
            self.assertTrue(result["reference_refinement"]["resolved"], result["reference_refinement"])


if __name__ == "__main__":
    unittest.main()
