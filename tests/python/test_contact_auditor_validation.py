"""Malformed evidence must fail before physics, refinement, or serialization."""

import copy
import importlib.util
import json
import math
from pathlib import Path
import subprocess
import sys
import tempfile
from types import SimpleNamespace
import unittest
from unittest.mock import patch


ROOT = Path(__file__).resolve().parents[2]
SCRIPT = ROOT / "tools/validate_contact_reference.py"
SPEC = importlib.util.spec_from_file_location("contact_auditor_validation_target", SCRIPT)
if SPEC is None or SPEC.loader is None:
    raise RuntimeError("Cannot load contact auditor")
audit = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = audit
SPEC.loader.exec_module(audit)


def candidate():
    # Independently constructed initial state: 8 g hammer at 1 m/s, stationary
    # resonator and zero initial compression. This row has 0.004 J of energy.
    sample = {
        "time_s": 0.0, "q_m": [0.0, 0.0], "v_m_per_s": [0.0, 0.0],
        "hammer_position_m": 0.0, "hammer_velocity_m_per_s": 1.0,
        "hammer_force_n": 0.0, "damper_force_n": 0.0,
        "hammer_impulse_ns": 0.0, "damper_impulse_ns": 0.0,
        "modal_loss_j": 0.0, "hammer_loss_j": 0.0, "damper_loss_j": 0.0,
        "actuator_work_j": 0.0, "launch_work_j": 0.0, "energy_j": 0.004,
        "modal_acceleration_m_per_s2": [0.0, 0.0],
        "hammer_acceleration_m_per_s2": 0.0,
    }
    return {
        "fixture": "conservative_impact", "success": True,
        "maximum_step_s": 20e-6, "event_tolerance_s": 10e-9,
        "relative_tolerance": 1e-8, "absolute_tolerance": 1e-12,
        "samples": [sample],
        "events": [{"contact": "hammer", "kind": "onset", "time_s": 0.0,
                    "bracket_lo_s": 0.0, "bracket_hi_s": 0.0}],
        "diagnostics": {"max_abs_cumulative_residual_j": 0.0,
                        "sum_abs_interval_residual_j": 0.0},
    }


class ContactAuditorValidationTests(unittest.TestCase):
    def setUp(self):
        self.run = candidate()
        forbidden = patch.object(audit, "oracle_for", side_effect=AssertionError(
            "Malformed input must fail before constructing an oracle"))
        forbidden.start()
        self.addCleanup(forbidden.stop)

    def assertMalformed(self, run, text):
        failures = audit.validate_run_input(run)
        self.assertTrue(any(text in item for item in failures), failures)
        result = audit.audit_run(run)
        self.assertEqual(result["status"], "fail", result)
        self.assertTrue(result["failures"])
        json.dumps(result, allow_nan=False)
        return result

    def test_controls_require_finite_positive_numbers_without_boolean_coercion(self):
        names = ("maximum_step_s", "event_tolerance_s", "relative_tolerance",
                 "absolute_tolerance", "velocity_absolute_tolerance",
                 "energy_absolute_tolerance", "impulse_absolute_tolerance", "output_step_s")
        values = (0, -1, math.nan, math.inf, -math.inf, True, False,
                  "0.00001", None, [], {}, 10 ** 400)
        for key in names:
            for value in values:
                with self.subTest(key=key, value=str(value)[:20]):
                    run = copy.deepcopy(self.run)
                    run[key] = value
                    self.assertMalformed(run, key)

    def test_required_controls_cannot_be_omitted_but_optional_axis_can(self):
        for key in ("maximum_step_s", "event_tolerance_s"):
            run = copy.deepcopy(self.run)
            del run[key]
            self.assertMalformed(run, key)
        del self.run["relative_tolerance"]
        del self.run["absolute_tolerance"]
        self.assertEqual(audit.validate_run_input(self.run), [])

    def test_vectors_have_exactly_two_finite_numeric_components(self):
        malformed = ([], [0], [0, 0, 0], "00", (0, 0), None, {},
                     [math.nan, 0], [0, math.inf], [False, 0], ["0", 0], [[0], 0])
        for key in ("q_m", "v_m_per_s", "modal_acceleration_m_per_s2"):
            for value in malformed:
                with self.subTest(key=key, value=value):
                    run = copy.deepcopy(self.run)
                    run["samples"][0][key] = value
                    self.assertMalformed(run, key)

    def test_scalar_acceleration_and_observations_are_finite_numbers(self):
        keys = [key for key, value in self.run["samples"][0].items()
                if isinstance(value, float) and key != "time_s"]
        keys += ["pickup_voltage_v", "instantaneous_power_residual_w", "actuator_power_w"]
        for key in keys:
            for value in (math.nan, math.inf, -math.inf, True, "0", None):
                with self.subTest(key=key, value=value):
                    run = copy.deepcopy(self.run)
                    run["samples"][0][key] = value
                    self.assertMalformed(run, key)

    def test_missing_acceleration_is_incomplete_evidence_not_malformed(self):
        del self.run["samples"][0]["modal_acceleration_m_per_s2"]
        del self.run["samples"][0]["hammer_acceleration_m_per_s2"]
        self.assertEqual(audit.validate_run_input(self.run), [])

    def test_negative_magnitude_diagnostics_reject(self):
        for key in ("max_abs_cumulative_residual_j", "sum_abs_interval_residual_j",
                    "maximum_power_residual_w", "initial_energy_j",
                    "minimum_accepted_step_s", "maximum_accepted_step_s"):
            for value in (-1, math.nan, math.inf, "0", True):
                with self.subTest(key=key, value=value):
                    run = copy.deepcopy(self.run)
                    run["diagnostics"][key] = value
                    self.assertMalformed(run, key)

    def test_diagnostic_counts_reject_fractional_and_boolean_values(self):
        for value in (-1, 1.5, True, "12", math.nan):
            run = copy.deepcopy(self.run)
            run["diagnostics"]["accepted_steps"] = value
            self.assertMalformed(run, "accepted_steps")
        self.run["diagnostics"] = {}
        self.assertEqual(audit.validate_run_input(self.run), [])

    def test_explicit_failure_survives_empty_samples(self):
        self.run.update(success=False, failure_code=5, samples=[])
        self.assertEqual(audit.validate_run_input(self.run), [])
        result = audit.audit_run(self.run)
        self.assertEqual(result["status"], "fail")
        self.assertIn("trajectory samples", result["missing"])
        self.assertIn("Candidate reported explicit failure", result["failures"][0])
        self.run["success"] = True
        self.run["failure_code"] = 0
        result = audit.audit_run(self.run)
        self.assertEqual(result["status"], "incomplete")
        self.assertEqual(result["failures"], [])

    def test_success_and_failure_code_types_are_explicit(self):
        for value in ("false", 0, 1, None):
            run = copy.deepcopy(self.run)
            run["success"] = value
            self.assertMalformed(run, "success")
        for value in (True, -1, 1.5, "3"):
            run = copy.deepcopy(self.run)
            run["failure_code"] = value
            self.assertMalformed(run, "failure_code")

    def test_malformed_containers_fail_without_index_or_attribute_errors(self):
        self.assertMalformed([], "run must be an object")
        for key in ("samples", "events", "relaunch_snapshots"):
            for value in ({}, None, "wrong", [None]):
                run = copy.deepcopy(self.run)
                run[key] = value
                self.assertMalformed(run, key)
        self.run["diagnostics"] = []
        self.assertMalformed(self.run, "diagnostics")

    def test_nonfinite_event_time_fails_even_with_missing_brackets(self):
        for value in (math.nan, math.inf, -1, 0.003, "0", False):
            run = copy.deepcopy(self.run)
            event = run["events"][0]
            event["time_s"] = value
            del event["bracket_lo_s"]
            del event["bracket_hi_s"]
            self.assertMalformed(run, "time_s")

    def test_extensions_cannot_smuggle_nonfinite_values_into_json_output(self):
        self.run["events"][0]["unrecognized_metadata"] = {"nested": [math.nan]}
        self.assertMalformed(self.run, "unrecognized_metadata.nested[0]")

    def test_snapshot_vectors_use_same_shape_and_finiteness_rules(self):
        before = copy.deepcopy(self.run["samples"][0])
        self.run["relaunch_snapshots"] = [{"before": before, "after": copy.deepcopy(before)}]
        self.assertEqual(audit.validate_run_input(self.run), [])
        self.run["relaunch_snapshots"][0]["after"]["v_m_per_s"] = [0]
        self.assertMalformed(self.run, "relaunch_snapshots[0].after.v_m_per_s")

    def test_refinement_rejects_malformed_metadata_before_sorting_or_comparison(self):
        self.run["relative_tolerance"] = math.nan
        result = audit.refinement_gates([self.run])
        self.assertEqual(result["status"], "fail")
        self.assertTrue(result["failures"])
        self.assertEqual(result["comparisons"], {})
        json.dumps(result, allow_nan=False)

    def test_finite_but_overflowing_evidence_still_yields_serializable_failure(self):
        fixture = audit.fixtures()["conservative_impact"]
        expected = copy.deepcopy(self.run["samples"][0])
        oracle = SimpleNamespace(fixture=fixture, method="fixed independent initial state",
                                 sample=lambda time: expected,
                                 events=copy.deepcopy(self.run["events"]))
        mutations = ({"q_m": [1e308, 0]}, {"v_m_per_s": [1e308, 0]},
                     {"modal_loss_j": 1e308, "hammer_loss_j": 1e308},
                     {"hammer_acceleration_m_per_s2": 1e308,
                      "hammer_velocity_m_per_s": 1e100})
        vector = [*fixture.q, *fixture.v, fixture.x, fixture.vh, 0, 0, 0, 0, 0, 0, 0]
        with patch.object(audit, "oracle_for", return_value=oracle), \
                patch.object(audit, "initial_vector", return_value=vector):
            for mutation in mutations:
                with self.subTest(mutation=mutation):
                    run = copy.deepcopy(self.run)
                    run["samples"][0].update(mutation)
                    result = audit.audit_run(run)
                    self.assertEqual(result["status"], "fail")
                    self.assertTrue(result["failures"])
                    json.dumps(result, allow_nan=False)

    def test_real_cli_rejects_malformed_evidence_without_optional_dependencies(self):
        cases = []
        for field, value in (("maximum_step_s", math.nan), ("event_tolerance_s", 0)):
            run = candidate()
            run[field] = value
            cases.append({"runs": [run]})
        for field, value in (("q_m", [0]), ("v_m_per_s", [0, 0, 0]),
                             ("modal_acceleration_m_per_s2", [math.nan, 0]),
                             ("hammer_acceleration_m_per_s2", math.inf)):
            run = candidate()
            run["samples"][0][field] = value
            cases.append({"runs": [run]})
        run = candidate()
        run["diagnostics"]["sum_abs_interval_residual_j"] = -1
        cases.extend(({"runs": [run]}, {"runs": {}}, {"runs": [None]}, []))
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "candidate.json"
            output = Path(directory) / "audit.json"
            for index, payload in enumerate(cases):
                with self.subTest(case=index):
                    path.write_text(json.dumps(payload), encoding="utf-8")
                    result = subprocess.run([sys.executable, "-S", str(SCRIPT), "--candidate",
                                             str(path), "--output", str(output)],
                                            text=True, capture_output=True, check=False)
                    self.assertEqual(result.returncode, 1, result.stderr + result.stdout)
                    self.assertEqual(result.stderr, "")
                    self.assertEqual(json.loads(result.stdout)["status"], "fail")
                    report = json.loads(output.read_text())
                    self.assertEqual(report["status"], "fail")
                    self.assertTrue(report["error"])
                    self.assertNotIn("NaN", output.read_text())

    def test_real_cli_retains_failure_without_samples(self):
        self.run.update(success=False, failure_code=5, samples=[])
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "candidate.json"
            path.write_text(json.dumps({"runs": [self.run]}), encoding="utf-8")
            result = subprocess.run([sys.executable, "-S", str(SCRIPT), "--candidate", str(path)],
                                    text=True, capture_output=True, check=False)
        self.assertEqual(result.returncode, 1, result.stderr)
        self.assertEqual(result.stderr, "")
        report = json.loads(result.stdout)
        self.assertEqual(report["runs"][0]["status"], "fail")
        self.assertIn("explicit failure", report["runs"][0]["failures"][0])


if __name__ == "__main__":
    unittest.main()
