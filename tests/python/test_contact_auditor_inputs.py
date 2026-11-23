"""Contact-auditor input rejection tests, not physical-model acceptance evidence.

Only the optional oracle backend is replaced: a fixed two-mass analytic endpoint
fixture keeps these validation tests runnable with the standard library alone.
The production parser, sample checks, event checks and CLI reporting run intact.
"""
from __future__ import annotations

from contextlib import redirect_stdout
import copy
import importlib.util
import io
import json
import math
from pathlib import Path
import sys
import tempfile
from types import SimpleNamespace
import unittest
from unittest.mock import patch


ROOT = Path(__file__).resolve().parents[2]
SPEC = importlib.util.spec_from_file_location(
    "rhodes_contact_auditor_input_test_target", ROOT / "tools/validate_contact_reference.py")
if SPEC is None or SPEC.loader is None:
    raise RuntimeError("Cannot load contact auditor")
auditor = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = auditor
SPEC.loader.exec_module(auditor)


def endpoint_samples():
    """Independent conservative impact endpoints: masses 8 g/2 g, k=20 kN/m."""
    duration = 0.002
    separation = math.pi / math.sqrt(20000.0 / 0.0016)
    start = {
        "time_s": 0.0, "q_m": [0.0, 0.0], "v_m_per_s": [0.0, 0.0],
        "hammer_position_m": 0.0, "hammer_velocity_m_per_s": 1.0,
        "hammer_force_n": 0.0, "damper_force_n": 0.0,
        "hammer_impulse_ns": 0.0, "damper_impulse_ns": 0.0,
        "modal_loss_j": 0.0, "hammer_loss_j": 0.0, "damper_loss_j": 0.0,
        "actuator_work_j": 0.0, "launch_work_j": 0.0, "energy_j": 0.004,
        "modal_acceleration_m_per_s2": [0.0, 0.0],
        "hammer_acceleration_m_per_s2": 0.0,
    }
    end = copy.deepcopy(start)
    end.update(time_s=duration, q_m=[1.6 * duration - 0.8 * separation, 0.0],
               v_m_per_s=[1.6, 0.0], hammer_position_m=0.6 * duration + 0.2 * separation,
               hammer_velocity_m_per_s=0.6, hammer_impulse_ns=0.0032)
    return [start, end], separation


class ContactAuditorInputTests(unittest.TestCase):
    def setUp(self):
        samples, separation = endpoint_samples()
        self.fixture = auditor.fixtures()["conservative_impact"]
        self.reference = {s["time_s"]: s for s in samples}
        events = [{"contact": "hammer", "kind": "onset", "time_s": 0.0},
                  {"contact": "hammer", "kind": "separation", "time_s": separation}]
        oracle = SimpleNamespace(
            fixture=self.fixture, method="test-only fixed analytic endpoints",
            sample=lambda t: copy.deepcopy(self.reference[t]), events=events)
        self.run = {
            "fixture": self.fixture.name, "success": True,
            "maximum_step_s": 20e-6, "event_tolerance_s": 10e-9,
            "samples": copy.deepcopy(samples),
            "events": [{**e, "bracket_lo_s": e["time_s"], "bracket_hi_s": e["time_s"]}
                       for e in events],
            "diagnostics": {"max_abs_cumulative_residual_j": 0.0,
                            "sum_abs_interval_residual_j": 0.0},
        }
        for target, value in (
                ("oracle_for", lambda name: oracle),
                ("initial_vector", lambda f: [*f.q, *f.v, f.x, f.vh, 0, 0, 0, 0, 0, 0, 0])):
            replacement = patch.object(auditor, target, side_effect=value)
            replacement.start()
            self.addCleanup(replacement.stop)
        numerical_backend = patch.object(
            auditor, "numerics", side_effect=AssertionError("Input tests must not require SciPy"))
        numerical_backend.start()
        self.addCleanup(numerical_backend.stop)

    def assertFailure(self, run, message):
        result = auditor.audit_run(run)
        self.assertEqual(result["status"], "fail", result)
        self.assertTrue(any(message in item for item in result["failures"]), result)
        return result

    def test_valid_endpoint_data_remain_explicitly_incomplete(self):
        result = auditor.audit_run(self.run)
        self.assertEqual(result["status"], "incomplete")
        self.assertEqual(result["failures"], [])
        self.assertEqual(result["missing"], [
            "independently located peak compression and force (extrema audit not yet implemented)"])

    def test_unknown_frozen_fixture_is_rejected(self):
        self.run["fixture"] = "invented_fixture"
        self.assertFailure(self.run, "Unknown frozen fixture")

    def test_sample_times_reject_nonfinite_outside_duplicate_and_reverse(self):
        cases = [[0.0, value] for value in
                 (math.nan, math.inf, -math.inf, -1e-6, 0.0021, 0.0)]
        cases.append([0.002, 0.0])
        for times in cases:
            with self.subTest(times=times):
                run = copy.deepcopy(self.run)
                for sample, time in zip(run["samples"], times):
                    sample["time_s"] = time
                self.assertFailure(run, "Sample times must be finite, strictly increasing")

    def test_each_compared_scalar_rejects_nan_and_both_infinities(self):
        expected = self.reference[0.0]
        scalars = [key for key in expected if key not in {
            "time_s", "q_m", "v_m_per_s", "modal_acceleration_m_per_s2",
            "hammer_acceleration_m_per_s2"}]
        for key in [*scalars, "q_m", "v_m_per_s"]:
            for value in (math.nan, math.inf, -math.inf):
                for index in (range(2) if key in {"q_m", "v_m_per_s"} else (None,)):
                    with self.subTest(key=key, value=value, index=index):
                        actual = copy.deepcopy(expected)
                        if index is None:
                            actual[key] = value
                        else:
                            actual[key][index] = value
                        _, failures = auditor.compare_samples(actual, expected, self.fixture)
                        self.assertTrue(any(item.startswith(key) for item in failures), failures)

    def test_nonfinite_sample_reaches_run_failure(self):
        self.run["samples"][-1]["hammer_position_m"] = math.nan
        self.assertFailure(self.run, "hammer_position_m")

    def test_events_reject_wrong_contact_kind_order_and_count(self):
        for change in ("contact", "kind", "reverse", "missing", "extra"):
            with self.subTest(change=change):
                run = copy.deepcopy(self.run)
                if change in {"contact", "kind"}:
                    run["events"][0][change] = "unknown"
                elif change == "reverse":
                    run["events"].reverse()
                elif change == "missing":
                    run["events"].pop()
                else:
                    run["events"].append(copy.deepcopy(run["events"][-1]))
                self.assertFailure(run, "event order/classification differs")

    def test_event_brackets_reject_nonfinite_reversed_outside_and_wide(self):
        time = self.run["events"][-1]["time_s"]
        bounds = [(math.nan, time), (time, math.inf), (time + 1e-9, time),
                  (time + 1e-9, time + 2e-9), (time - 1e-8, time + 1e-8)]
        for lo, hi in bounds:
            with self.subTest(lo=lo, hi=hi):
                run = copy.deepcopy(self.run)
                run["events"][-1].update(bracket_lo_s=lo, bracket_hi_s=hi)
                self.assertFailure(run, "invalid event bracket or width")

    def test_wrong_event_time_fails_with_valid_narrow_bracket(self):
        event = self.run["events"][-1]
        wrong = event["time_s"] + 1e-5
        event.update(time_s=wrong, bracket_lo_s=wrong, bracket_hi_s=wrong)
        self.assertFailure(self.run, "event timing differs from independent root")

    def test_nonfinite_and_excessive_energy_diagnostics_fail(self):
        for key in self.run["diagnostics"]:
            for value in (math.nan, math.inf, -math.inf, 1.0):
                with self.subTest(key=key, value=value):
                    run = copy.deepcopy(self.run)
                    run["diagnostics"][key] = value
                    self.assertFailure(run, key + " exceeds frozen energy budget")

    def test_negative_accumulated_physical_losses_fail(self):
        for key in ("modal_loss_j", "hammer_loss_j", "damper_loss_j"):
            with self.subTest(key=key):
                run = copy.deepcopy(self.run)
                run["samples"][-1][key] = -1e-6
                self.assertFailure(run, "negative accumulated physical loss " + key)

    def test_explicit_candidate_failure_is_retained_with_samples(self):
        self.run.update(success=False, failure_code=7)
        self.assertFailure(self.run, "Candidate reported explicit failure")

    def test_missing_coverage_and_diagnostics_cannot_be_called_pass(self):
        self.run["samples"] = self.run["samples"][:1]
        self.run["diagnostics"] = {}
        result = auditor.audit_run(self.run)
        self.assertEqual(result["status"], "incomplete")
        for text in ("complete trajectory including", "accepted-interval max_abs",
                     "accepted-interval sum_abs"):
            self.assertTrue(any(text in item for item in result["missing"]), result)

    def test_cli_reports_structured_failure_for_invalid_json_and_numeric_text(self):
        invalid_number = copy.deepcopy(self.run)
        invalid_number["maximum_step_s"] = "not-a-number"
        for payload in ("{broken", json.dumps({"runs": [invalid_number]})):
            with self.subTest(payload=payload[:50]), tempfile.TemporaryDirectory() as directory:
                path = Path(directory) / "candidate.json"
                path.write_text(payload, encoding="utf-8")
                output = io.StringIO()
                args = ["validate_contact_reference.py", "--candidate", str(path)]
                with patch.object(sys, "argv", args), redirect_stdout(output):
                    code = auditor.main()
                result = json.loads(output.getvalue())
                self.assertEqual(code, 1)
                self.assertEqual(result["status"], "fail")
                self.assertTrue(result["error"])


if __name__ == "__main__":
    unittest.main()
