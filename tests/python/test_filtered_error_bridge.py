"""Focused offline bridge oracles; no production or full-case Q sweep.

Import/plan checks use only the standard library. Numerical checks skip when
the optional NumPy/SciPy dependencies are absent. Explicit cell sums and an
independent matrix exponential check the bridge without using its convolution
or one-sided-state helpers as the expected result.
"""
from __future__ import annotations

from copy import deepcopy
import importlib.util
import json
import math
from pathlib import Path
import subprocess
import sys
from types import SimpleNamespace
import unittest
from unittest import mock


ROOT = Path(__file__).resolve().parents[2]
SPEC = importlib.util.spec_from_file_location(
    "filtered_error_bridge_tests", ROOT / "tools/filtered_error_bridge.py")
if SPEC is None or SPEC.loader is None:
    raise RuntimeError("Cannot load filtered-error bridge")
bridge = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = bridge
SPEC.loader.exec_module(bridge)
reference = bridge.reference
HAVE_SCIENCE = (importlib.util.find_spec("numpy") is not None
                and importlib.util.find_spec("scipy") is not None)


class FilteredErrorBridgePlanTests(unittest.TestCase):
    def test_import_and_plan_work_without_site_packages(self):
        script = """
import json
import sys
from tools import filtered_error_bridge as bridge
plan = bridge.reference.make_plan('pilot')
assert not any(name.split('.')[0] in ('numpy', 'scipy') for name in sys.modules)
print(json.dumps({'case_count': plan['case_count'], 'gates': plan['gates']}))
"""
        process = subprocess.run([sys.executable, "-S", "-B", "-c", script],
                                 cwd=ROOT, text=True, capture_output=True, check=False)
        self.assertEqual(process.returncode, 0, process.stderr)
        result = json.loads(process.stdout)
        self.assertEqual(result["case_count"], 36)
        self.assertEqual(result["gates"], reference.GATES)

    def test_fixed_case_and_original_gates_remain_frozen(self):
        cases = [case for case in reference.build_cases("full")
                 if case.id == "r08-chord-all73-u10"]
        self.assertEqual(len(cases), 1)
        case = cases[0]
        self.assertEqual((case.sample_rate_hz, case.frames, case.keys, case.intensity),
                         (192000.0, 4096, tuple(range(73)), 1.0))
        self.assertEqual(len(case.events), 736)
        self.assertEqual(reference.reset_epochs(case.frames, case.events),
                         ((0, 1023), (1023, 4096)))
        self.assertEqual(reference.object_hash(case.document()),
                         "579b3844d0fd32ef8fa9706f189b5d6255c0c93b2218e64d235ed4b233cbc4bc")
        self.assertEqual(reference.GATES, {
            "comparison_a_peak_volts": 1e-12,
            "reference_refinement_peak_volts": 1e-9,
            "reference_refinement_rms_volts": 1e-10,
            "comparison_b_exploratory_peak_volts": 1e-4,
            "comparison_b_exploratory_rms_volts": 1e-5,
        })


@unittest.skipUnless(HAVE_SCIENCE, "Optional offline bridge checks need NumPy/SciPy")
class FilteredErrorBridgeNumericalTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        import numpy as np
        cls.np = np
        cls.normalizer = reference.quadrature(64).normalizer

    def cell_sum_oracle(self, right, left, factor, epochs):
        """Sum each delay cell separately, with explicit epoch masks.

        The rectangle sum is N, using Z rather than the discrete DC sum.
        Neither expected vector uses convolution or any bridge helper.
        """
        np = self.np
        support = 96 * factor
        taps = reference.kernel(np.arange(support + 1) / factor)
        frames = len(right) // factor
        cells = np.zeros((frames, support))
        rectangle = np.zeros(frames)
        for start, stop in epochs:
            first = start * factor
            for frame in range(start, stop):
                current = frame * factor
                for delay in range(min(support, current - first + 1)):
                    near, far = current - delay, current - delay - 1
                    # Even a supplied physical old left limit is erased here.
                    x_near = float(left[near]) if near > first else 0.0
                    x_far = float(right[far]) if far >= first else 0.0
                    cells[frame, delay] = (
                        taps[delay] * x_near + taps[delay + 1] * x_far
                    ) / (2.0 * factor * self.normalizer)
                rectangle[frame] = math.fsum(
                    float(taps[delay]) * float(right[current - delay])
                    for delay in range(min(support, current - first) + 1)
                ) / (factor * self.normalizer)
        trapezoid = np.asarray([math.fsum(row) for row in cells])
        return trapezoid, rectangle

    def step_source(self, frames, factor, steps):
        right = self.np.zeros(frames * factor)
        for index, jump in steps:
            right[index:] += jump
        left = right.copy()
        for index, jump in steps:
            left[index] -= jump
        rows = [{"event_index": number, "internal_index": index,
                 "jump_volts": jump, "type": "synthetic_step"}
                for number, (index, jump) in enumerate(steps)]
        return right, left, rows

    def event_oracle(self, index, jump, frames, factor, epoch_stop):
        result = self.np.zeros(frames)
        for frame in range(epoch_stop):
            delay = frame * factor - index
            if 0 <= delay < 96 * factor:
                weight = 1.0 if delay == 0 else 0.5
                result[frame] = (jump * weight * float(reference.kernel(delay / factor))
                                 / (factor * self.normalizer))
        return result

    def test_constant_source_retains_both_finite_support_endpoints(self):
        np = self.np
        for factor in (1, 2, 4):
            with self.subTest(factor=factor):
                frames, amplitude = 110, 0.75
                epochs = ((0, frames),)
                right = np.full(frames * factor, amplitude)
                left = right.copy()
                left[0] = -17.0  # Must be treated as zero epoch history.
                saved_right, saved_left = right.copy(), left.copy()
                right.setflags(write=False)
                left.setflags(write=False)
                expected, normalized = self.cell_sum_oracle(right, left, factor, epochs)
                actual = bridge.trapezoid_reference(right, left, factor, epochs, self.normalizer)
                arrays, rows = bridge.endpoint_attribution(right, left, factor, epochs, self.normalizer)
                np.testing.assert_allclose(actual, expected, rtol=0.0, atol=5e-16)
                np.testing.assert_allclose(normalized - expected, arrays["endpoint_event_direct_volts"],
                                           rtol=0.0, atol=8e-16)
                self.assertEqual(actual[0], 0.0)
                self.assertEqual(len(rows), 1)
                self.assertEqual((rows[0]["internal_index"], rows[0]["jump_volts"]), (0, amplitude))
                endpoints = reference.kernel(np.asarray([0.0, 96.0]))
                self.assertTrue(np.all(np.abs(endpoints) > 1e-8))
                self.assertEqual(arrays["support_delayed_endpoint_volts"][95], 0.0)
                self.assertAlmostEqual(arrays["support_delayed_endpoint_volts"][96],
                                       amplitude * endpoints[1] / (2 * factor * self.normalizer), delta=1e-20)
                self.assertAlmostEqual(arrays["support_current_endpoint_volts"][97],
                                       amplitude * endpoints[0] / (2 * factor * self.normalizer), delta=1e-20)
                self.assertEqual(arrays["event_contributions_volts"][0, 96], 0.0)
                np.testing.assert_array_equal(right, saved_right)
                np.testing.assert_array_equal(left, saved_left)

    def test_single_and_adjacent_steps_match_manual_cells_and_event_weights(self):
        np = self.np
        for factor in (1, 2, 4):
            first = 3 * factor
            fixtures = (((first, 0.75),),
                        ((first, 0.75), (first + 1, -0.25)),
                        ((first, 0.75), (first + factor, -0.25)))
            for steps in fixtures:
                with self.subTest(factor=factor, steps=steps):
                    frames, epochs = 104, ((0, 104),)
                    right, left, rows = self.step_source(frames, factor, steps)
                    expected, normalized = self.cell_sum_oracle(right, left, factor, epochs)
                    actual = bridge.trapezoid_reference(right, left, factor, epochs, self.normalizer)
                    arrays, retained = bridge.endpoint_attribution(
                        right, left, factor, epochs, self.normalizer, rows)
                    np.testing.assert_allclose(actual, expected, rtol=0.0, atol=5e-16)
                    np.testing.assert_allclose(normalized - expected, arrays["endpoint_event_attribution_volts"],
                                               rtol=0.0, atol=8e-16)
                    for number, (index, jump) in enumerate(steps):
                        oracle = self.event_oracle(index, jump, frames, factor, frames)
                        np.testing.assert_allclose(arrays["event_contributions_volts"][number], oracle,
                                                   rtol=2e-15, atol=1e-18)
                        self.assertEqual(retained[number]["event_index"], number)
                    self.assertEqual(actual[3], 0.0)
                    np.testing.assert_array_equal(arrays["event_jump_attribution_residual_internal_volts"],
                                                  np.zeros(frames * factor))

    def test_reset_erases_old_and_same_time_pre_reset_strikes(self):
        np = self.np
        for factor, rate in ((1, 192000.0), (2, 96000.0), (4, 48000.0)):
            with self.subTest(factor=factor):
                events = (bridge.Event(0, "on", 0, 0.8), bridge.Event(0, "on", 4, 0.4),
                          bridge.Event(6, "on", 0, 0.2), bridge.Event(6, "reset"),
                          bridge.Event(6, "on", 4, 0.3), bridge.Event(6, "reset"),
                          bridge.Event(6, "on", 0, 0.7), bridge.Event(6, "on", 4, 0.6))
                case = bridge.Case("reset-order", "selfcheck", rate, 110, (0, 4), 1.0, events)
                fresh = bridge.Case("fresh", "selfcheck", rate, 104, (0, 4), 1.0,
                                    (bridge.Event(0, "on", 0, 0.7), bridge.Event(0, "on", 4, 0.6)))
                right, left, rows = bridge.one_sided_source(case)
                fresh_right, fresh_left, _ = bridge.one_sided_source(fresh)
                self.assertEqual(len(rows), len(events))
                self.assertEqual([row["event_index"] for row in rows], list(range(len(events))))
                np.testing.assert_array_equal(right[6 * factor:], fresh_right)
                np.testing.assert_array_equal(left[6 * factor:], fresh_left)
                self.assertEqual(left[6 * factor], 0.0)
                self.assertEqual([row["jump_volts"] for row in rows[2:6]], [0.0] * 4)
                self.assertEqual([row["discarded_by_same_time_reset"] for row in rows[2:6]],
                                 [True, True, True, False])
                self.assertTrue(all(abs(row["physical_jump_volts"]) > 1e-6 for row in rows[2:6]))
                self.assertAlmostEqual(math.fsum(row["jump_volts"] for row in rows[6:]),
                                       right[6 * factor], delta=1e-18)
                epochs = ((0, 6), (6, 110))
                expected, normalized = self.cell_sum_oracle(right, left, factor, epochs)
                actual = bridge.trapezoid_reference(right, left, factor, epochs, self.normalizer)
                fresh_result = bridge.trapezoid_reference(
                    fresh_right, fresh_left, factor, ((0, 104),), self.normalizer)
                np.testing.assert_allclose(actual, expected, rtol=0.0, atol=2e-17)
                np.testing.assert_array_equal(actual[6:], fresh_result)
                arrays, _ = bridge.endpoint_attribution(right, left, factor, epochs, self.normalizer, rows)
                np.testing.assert_array_equal(arrays["event_contributions_volts"][:6, 6:], np.zeros((6, 104)))
                np.testing.assert_allclose(normalized - expected, arrays["endpoint_event_attribution_volts"],
                                           rtol=0.0, atol=3e-17)

    def test_zero_intensity_release_has_no_jump_but_keeps_its_event_window(self):
        np = self.np
        events = (bridge.Event(0, "on", 72, 0.8), bridge.Event(110, "on", 72, 0.0))
        case = bridge.Case("zero-on", "selfcheck", 48000.0, 220, (72,), 0.8, events)
        released = bridge.Case("off", "selfcheck", 48000.0, 220, (72,), 0.8,
                               (events[0], bridge.Event(110, "off", 72)))
        right, left, rows = bridge.one_sided_source(case)
        off_right, off_left, _ = bridge.one_sided_source(released)
        np.testing.assert_array_equal(right, off_right)
        np.testing.assert_array_equal(left, off_left)
        self.assertEqual(rows[1]["physical_jump_volts"], 0.0)
        self.assertEqual(rows[1]["jump_volts"], 0.0)
        self.assertEqual(right[440], left[440])
        sampled = reference.sampled_reference(right, 4, ((0, 220),))
        trapezoid, _ = self.cell_sum_oracle(right, left, 4, ((0, 220),))
        arrays, metrics = bridge.decompose_arrays(
            sampled, sampled, trapezoid, right, left, 4, ((0, 220),), self.normalizer,
            sample_rate_hz=48000.0, event_rows=rows)
        self.assertEqual([(window["start_frame"], window["stop_frame_inclusive"])
                          for window in metrics["event_windows"]], [(0, 96), (110, 206)])
        self.assertEqual(metrics["event_windows"][1]["event_indices"], [1])
        self.assertEqual(metrics["event_window_union"]["total_error_volts"]["sample_count"], 194)
        np.testing.assert_array_equal(arrays["event_contributions_volts"][1], np.zeros(220))
        self.assertIsNone(metrics["frame_816"])

    def test_exact_left_propagation_uses_no_epsilon_or_off_grid_query(self):
        from scipy.linalg import expm
        np = self.np
        rate, factor, offset, key = 48000.0, 4, 11, 72
        case = bridge.Case("exact-limit", "selfcheck", rate, 24, (key,), 1.0,
                           (bridge.Event(0, "on", key, 0.55), bridge.Event(offset, "on", key, 0.7)))
        states, propagate = reference.KeyTrajectory.states, reference.modal_solution

        def grid_only(trajectory, positions):
            internal = np.asarray(positions) * factor
            np.testing.assert_array_equal(internal, np.rint(internal))
            self.assertTrue(np.all((internal >= 0) & (internal < case.frames * factor)))
            return states(trajectory, positions)

        def exact_elapsed(q0, v0, omega0, beta, seconds):
            if np.ndim(seconds) == 0:
                self.assertIn(float(seconds), (0.0, offset / rate))
            return propagate(q0, v0, omega0, beta, seconds)

        with mock.patch.object(reference.KeyTrajectory, "states", grid_only), \
                mock.patch.object(reference, "modal_solution", exact_elapsed):
            right, left, rows = bridge.one_sided_source(case)
        model = reference.MODEL
        omega = (2 * math.pi * model["frequency_reference_hz"]
                 * math.exp2((key - model["reference_key"]) / model["semitones_per_octave"])
                 * np.asarray(model["frequency_ratios"]))
        impulse = model["maximum_velocity_change_m_per_s"] * np.asarray(model["strike_coupling"])
        q, v = np.zeros(3), np.zeros(3)
        for mode, damping in enumerate(model["free_decay_per_s"]):
            matrix = np.asarray([[0.0, 1.0], [-omega[mode] ** 2, -2.0 * damping]])
            q[mode], v[mode] = expm(matrix * (offset / rate)) @ np.asarray([0.0, impulse[mode] * 0.55])

        def loaded(speed_modes):
            observation = np.asarray(model["pickup_observation"])
            position = float(q @ observation)
            speed = float(speed_modes @ observation)
            a1, a2, a3 = model["flux_polynomial_coefficients"]
            return (-(a1 + 2 * a2 * position + 3 * a3 * position * position) * speed
                    * model["loaded_harp_gain_numerator"] / model["loaded_harp_gain_denominator"] / 3)

        before, after = loaded(v), loaded(v + 0.7 * impulse)
        self.assertAlmostEqual(left[offset * factor], before, delta=3e-17)
        self.assertAlmostEqual(right[offset * factor], after, delta=3e-17)
        self.assertAlmostEqual(rows[1]["jump_volts"], after - before, delta=3e-17)
        no_events = np.ones(len(right), dtype=bool)
        no_events[[0, offset * factor]] = False
        np.testing.assert_array_equal(right[no_events], left[no_events])

    def test_smooth_source_has_only_support_endpoint_attribution(self):
        np = self.np
        for factor in (1, 2, 4):
            with self.subTest(factor=factor):
                frames, epochs = 104, ((0, 104),)
                time = np.arange(frames * factor) / factor
                right = 0.1 * np.sin(0.07 * time) + 0.0005 * time * time
                left = right.copy()
                expected, normalized = self.cell_sum_oracle(right, left, factor, epochs)
                actual = bridge.trapezoid_reference(right, left, factor, epochs, self.normalizer)
                arrays, rows = bridge.endpoint_attribution(right, left, factor, epochs, self.normalizer)
                self.assertEqual(rows, [])
                self.assertEqual(arrays["event_contributions_volts"].shape, (0, frames))
                np.testing.assert_array_equal(arrays["event_grid_contribution_volts"], np.zeros(frames))
                np.testing.assert_allclose(actual, expected, rtol=0.0, atol=1e-15)
                np.testing.assert_allclose(normalized - expected, arrays["support_endpoint_volts"],
                                           rtol=0.0, atol=2e-15)
                self.assertNotEqual(arrays["support_delayed_endpoint_volts"][97], 0.0)

    def test_malformed_values_shapes_epochs_and_event_rows_are_rejected(self):
        np = self.np
        valid = {"right": np.zeros(8), "left": np.zeros(8), "factor": 2,
                 "epochs": ((0, 4),), "normalizer": self.normalizer}
        invalid = [
            {"right": []}, {"right": [[0.0] * 8]}, {"right": [complex(1.0, 1.0)] * 8},
            {"right": [math.nan] * 8}, {"left": [math.inf] * 8}, {"left": [0.0] * 7},
            {"right": [0.0] * 7, "left": [0.0] * 7},
            *({"factor": value} for value in (True, 1.0, 3)),
            *({"normalizer": value} for value in (0.0, -1.0, math.nan, math.inf, True, "1")),
            *({"epochs": value} for value in (
                (), None, ((0, 3),), ((1, 4),), ((0, 1), (2, 4)),
                ((0, 3), (2, 4)), ((0, 0), (0, 4)), ((0, 5),),
                ((False, 4),), ((0, 4.0),), ((0, 1, 4),), (None,))),
        ]
        for changes in invalid:
            for function in (bridge.trapezoid_reference, bridge.endpoint_attribution):
                with self.subTest(function=function.__name__, changes=changes), self.assertRaises(ValueError):
                    function(**(valid | changes))
        malformed_rows = [None, {}, {"internal_index": 0}, {"jump_volts": 0.0},
                          {"internal_index": 2, "jump_volts": 0.0, "offset_host_frames": 2.0}]
        malformed_rows += [{"internal_index": value, "jump_volts": 0.0}
                           for value in (-1, 8, True, 1.0)]
        malformed_rows += [{"internal_index": 0, "jump_volts": value}
                           for value in (math.nan, math.inf, True, "0", None)]
        for row in malformed_rows:
            with self.subTest(row=row), self.assertRaises(ValueError):
                bridge.endpoint_attribution(**valid, event_rows=[row])
        decomposition = {"production": np.zeros(4), "same_rate": np.zeros(4),
                         "continuous": np.zeros(4), **valid}
        for changes in ({"production": [0.0] * 3}, {"same_rate": [math.nan] * 4},
                        {"continuous": [[0.0] * 4]}, {"continuous": [1j] * 4},
                        *({"sample_rate_hz": value} for value in (0.0, math.nan, math.inf, True))):
            with self.subTest(decomposition=changes), self.assertRaises(ValueError):
                bridge.decompose_arrays(**(decomposition | changes))
        case = {"id": "malformed", "family": "selfcheck", "sample_rate_hz": 48000.0,
                "frames": 4, "keys": (0,), "intensity": 1.0, "events": ()}
        for changes in ({"sample_rate_hz": math.nan}, {"frames": 0}, {"keys": (0, 0)},
                        {"events": (bridge.Event(2, "on", 0, 1.0), bridge.Event(1, "off", 0))},
                        {"events": (bridge.Event(4, "on", 0, 1.0),)},
                        {"events": (bridge.Event(1, "on", 4, 1.0),)}):
            with self.subTest(case=changes), self.assertRaises(ValueError):
                bridge.one_sided_source(SimpleNamespace(**(case | changes)))

    def test_decomposition_exposes_missing_attribution_and_cannot_pass_original_b(self):
        np = self.np
        frames, factor, epochs = 820, 2, ((0, 820),)
        right, left, rows = self.step_source(frames, factor, ((6, 0.75), (8, -0.25), (220, 0.0)))
        rows[2]["type"] = "on"
        rows[2]["value"] = 0.0
        rows[0]["provenance"] = "synthetic algebra fixture"
        sampled = reference.sampled_reference(right, factor, epochs)
        expected_trapezoid, _ = self.cell_sum_oracle(right, left, factor, epochs)
        # Deliberately biased algebra input, not a newly computed Q64 reference.
        continuous = expected_trapezoid - 0.001
        production = sampled + 4e-13
        case = bridge.Case("bookkeeping-is-not-acceptance", "selfcheck", 96000.0, frames, (0,), 1.0,
                           (bridge.Event(3, "on", 0, 0.75), bridge.Event(4, "on", 0, 0.25),
                            bridge.Event(110, "on", 0, 0.0)))
        curves = {"same_rate_volts": sampled, "continuous_q16_volts": continuous.copy(),
                  "continuous_q32_volts": continuous.copy(), "continuous_q64_volts": continuous.copy()}
        original_assessment = reference.assess_case(case, production, curves)
        originals = [value.copy() for value in (production, sampled, continuous, right, left)]
        original_rows, original_gates = deepcopy(rows), deepcopy(reference.GATES)
        arrays, metrics = bridge.decompose_arrays(
            production, sampled, continuous, right, left, factor, epochs, self.normalizer,
            sample_rate_hz=case.sample_rate_hz, event_rows=rows)
        np.testing.assert_allclose(arrays["trapezoid_volts"], expected_trapezoid, rtol=0.0, atol=5e-16)
        self.assertGreater(np.max(np.abs(arrays["total_error_volts"])), 1e-4)
        discrete_normalizer = math.fsum(reference.kernel(np.arange(96 * factor + 1) / factor)) / factor
        expected_normalized = sampled * (discrete_normalizer / self.normalizer)
        terms = {
            "implementation_error_volts": production - sampled,
            "normalization_error_volts": sampled - expected_normalized,
            "endpoint_event_error_volts": expected_normalized - expected_trapezoid,
            "cell_integration_error_volts": expected_trapezoid - continuous,
        }
        for name, expected in terms.items():
            np.testing.assert_allclose(arrays[name], expected, rtol=0.0, atol=5e-16)
            self.assertGreater(np.max(np.abs(arrays[name])), 0.0, name)
        for name in ("decomposition_residual_volts", "endpoint_event_direct_residual_volts",
                     "endpoint_event_attribution_residual_volts", "event_contribution_residual_volts"):
            np.testing.assert_allclose(arrays[name], np.zeros(frames), rtol=0.0, atol=1e-12)
        self.assertEqual(metrics["frame_816"]["total_error_volts"], production[816] - continuous[816])
        total = production - continuous
        maximum_frame = max(range(frames), key=lambda frame: abs(total[frame]))
        total_metrics = metrics["arrays"]["total_error_volts"]
        self.assertEqual(total_metrics["maximum_error_frame"], maximum_frame)
        self.assertEqual(total_metrics["maximum_error_time_seconds"], maximum_frame / case.sample_rate_hz)
        self.assertAlmostEqual(total_metrics["signed_mean_error_volts"], math.fsum(total) / frames, delta=1e-18)
        self.assertAlmostEqual(total_metrics["rms_error_volts"],
                               math.sqrt(math.fsum(value * value for value in total) / frames), delta=1e-17)
        self.assertEqual(metrics["event_window_union"]["total_error_volts"]["sample_count"], 195)
        self.assertEqual(metrics["outside_event_window_union"]["total_error_volts"]["sample_count"], 625)
        self.assertEqual(metrics["event_rows"][0]["provenance"], rows[0]["provenance"])
        self.assertEqual(metrics["event_rows"][2]["metrics"]["maximum_absolute_error_volts"], 0.0)
        incomplete, _ = bridge.decompose_arrays(
            production, sampled, continuous, right, left, factor, epochs, self.normalizer,
            sample_rate_hz=case.sample_rate_hz, event_rows=[rows[0], rows[2]])
        expected_missing = self.event_oracle(8, -0.25, frames, factor, frames)
        self.assertEqual(incomplete["event_jump_attribution_residual_internal_volts"][8], -0.25)
        np.testing.assert_allclose(incomplete["endpoint_event_attribution_residual_volts"], expected_missing,
                                   rtol=0.0, atol=1e-12)
        np.testing.assert_allclose(incomplete["decomposition_residual_volts"], np.zeros(frames),
                                   rtol=0.0, atol=1e-12)
        self.assertEqual(original_assessment["comparison_a"]["status"], "pass")
        self.assertTrue(original_assessment["reference_refinement"]["resolved"])
        self.assertEqual(original_assessment["comparison_b"]["status"], "fail")
        self.assertEqual(reference.assess_case(case, production, curves), original_assessment)
        self.assertEqual(reference.GATES, original_gates)
        self.assertEqual(rows, original_rows)
        for actual, original in zip((production, sampled, continuous, right, left), originals):
            np.testing.assert_array_equal(actual, original)


if __name__ == "__main__":
    unittest.main()
