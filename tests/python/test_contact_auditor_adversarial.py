"""Independent parser-boundary rejection checks, not physics acceptance.

Known remaining alias-overflow and action-metadata defects are recorded in
W4-A067 rather than enshrined as accepted behavior. All retained expectations
below describe valid incomplete coverage or explicit malformed-input failure.
"""
from contextlib import contextmanager
import copy
import csv
import hashlib
import importlib.util
import io
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
SPEC = importlib.util.spec_from_file_location("a067_contact_adversarial_target", SCRIPT)
if SPEC is None or SPEC.loader is None:
    raise RuntimeError("Cannot load contact auditor")
audit = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = audit
SPEC.loader.exec_module(audit)


def candidate():
    # Independent endpoint formula for the frozen 8 g / 2 g conservative pair.
    separation = math.pi / math.sqrt(20000.0 / 0.0016)
    duration = 0.002
    first = {
        "time_s": 0.0, "q_m": [0.0, 0.0], "v_m_per_s": [0.0, 0.0],
        "hammer_position_m": 0.0, "hammer_velocity_m_per_s": 1.0,
        "hammer_force_n": 0.0, "damper_force_n": 0.0,
        "hammer_impulse_ns": 0.0, "damper_impulse_ns": 0.0,
        "modal_loss_j": 0.0, "hammer_loss_j": 0.0, "damper_loss_j": 0.0,
        "actuator_work_j": 0.0, "launch_work_j": 0.0, "energy_j": 0.004,
        "modal_acceleration_m_per_s2": [0.0, 0.0],
        "hammer_acceleration_m_per_s2": 0.0,
    }
    last = copy.deepcopy(first)
    last.update(time_s=duration, q_m=[1.6 * duration - 0.8 * separation, 0.0],
                v_m_per_s=[1.6, 0.0],
                hammer_position_m=0.6 * duration + 0.2 * separation,
                hammer_velocity_m_per_s=0.6, hammer_impulse_ns=0.0032)
    return {
        "fixture": "conservative_impact", "success": True, "failure_code": 0,
        "maximum_step_s": 20e-6, "event_tolerance_s": 10e-9,
        "samples": [first, last],
        "events": [dict(contact="hammer", kind=kind, time_s=time,
                        bracket_lo_s=time, bracket_hi_s=time)
                   for kind, time in (("onset", 0.0), ("separation", separation))],
        "diagnostics": {"max_abs_cumulative_residual_j": 0.0,
                        "sum_abs_interval_residual_j": 0.0},
    }


def scalar_aliases(run):
    result = copy.deepcopy(run)
    for sample in result["samples"]:
        sample["q0_m"], sample["q1_m"] = sample.pop("q_m")
        sample["v0_m_per_s"], sample["v1_m_per_s"] = sample.pop("v_m_per_s")
    return result


def csv_candidate(change=None):
    run = scalar_aliases(candidate())
    sample = run["samples"][0]
    sample.pop("modal_acceleration_m_per_s2")
    row = {"fixture": run["fixture"], "maximum_step_s": run["maximum_step_s"],
           "event_tolerance_s": run["event_tolerance_s"], **sample}
    if change:
        row.update(change)
    text = io.StringIO(newline="")
    writer = csv.DictWriter(text, fieldnames=list(row))
    writer.writeheader()
    writer.writerow(row)
    return text.getvalue()


@contextmanager
def fixed_endpoint_oracle():
    run = candidate()
    reference = {sample["time_s"]: sample for sample in run["samples"]}
    fixture = audit.fixtures()["conservative_impact"]
    oracle = SimpleNamespace(
        fixture=fixture, method="independent fixed analytic endpoints",
        sample=lambda time: copy.deepcopy(reference[time]), events=run["events"])
    vector = [*fixture.q, *fixture.v, fixture.x, fixture.vh, 0, 0, 0, 0, 0, 0, 0]
    with patch.object(audit, "oracle_for", return_value=oracle), \
            patch.object(audit, "initial_vector", return_value=vector), \
            patch.object(audit, "numerics", side_effect=AssertionError("No optional backend allowed")):
        yield


class ContactAuditorAdversarialTests(unittest.TestCase):
    def assertMalformed(self, run):
        with patch.object(audit, "oracle_for", side_effect=AssertionError("Unexpected oracle call")):
            result = audit.audit_run(run)
            refinement = audit.refinement_gates([run])
        for report in (result, refinement):
            self.assertEqual(report["status"], "fail", report)
            self.assertTrue(report["failures"], report)
            json.dumps(report, allow_nan=False)

    def assertCliFailure(self, body, suffix=".json"):
        with tempfile.TemporaryDirectory() as temporary:
            path = Path(temporary) / ("candidate" + suffix)
            output = Path(temporary) / "result.json"
            path.write_text(body, encoding="utf-8")
            before = hashlib.sha256(path.read_bytes()).hexdigest()
            completed = subprocess.run(
                [sys.executable, "-S", str(SCRIPT), "--candidate", str(path),
                 "--output", str(output)], cwd=ROOT, text=True, capture_output=True, timeout=15)
            self.assertEqual(completed.returncode, 1, completed.stderr)
            self.assertNotIn("Traceback", completed.stderr)
            report = json.loads(output.read_text(encoding="utf-8"))
            self.assertEqual(report["status"], "fail", report)
            json.dumps(report, allow_nan=False)
            self.assertEqual(json.loads(completed.stdout)["status"], "fail")
            self.assertEqual(hashlib.sha256(path.read_bytes()).hexdigest(), before)

    def test_canonical_vectors_reject_enormous_integers_before_backend(self):
        for field in ("q_m", "v_m_per_s", "modal_acceleration_m_per_s2"):
            for index in (0, 1):
                with self.subTest(field=field, index=index):
                    run = candidate()
                    run["samples"][0][field][index] = 10 ** 400
                    self.assertMalformed(run)

    def test_canonical_enormous_integer_has_structured_cli_failure(self):
        run = candidate()
        run["samples"][0]["q_m"][0] = 10 ** 400
        self.assertCliFailure(json.dumps({"runs": [run]}))

    def test_unknown_nested_integer_overflow_is_not_ignored(self):
        for scope in ("root", "sample", "event"):
            run = candidate()
            target = {"root": run, "sample": run["samples"][0],
                      "event": run["events"][0]}[scope]
            target["extension"] = {"sequence": [{"value": -(10 ** 400)}]}
            self.assertMalformed(run)

    def test_failure_code_cannot_contain_an_unrepresentable_integer(self):
        run = candidate()
        run["failure_code"] = 10 ** 400
        self.assertMalformed(run)

    def test_snapshot_optional_numbers_reject_nonfinite_and_nonnumeric_values(self):
        for field in ("time_s", "requested_velocity_m_per_s", "work_j"):
            for value in (math.inf, -math.inf, math.nan, False, "0", 10 ** 400):
                run = candidate()
                before = copy.deepcopy(run["samples"][0])
                run["relaunch_snapshots"] = [{"before": before, "after": copy.deepcopy(before),
                                               field: value}]
                self.assertMalformed(run)

    def test_snapshot_unknown_nonfinite_extension_rejects(self):
        run = candidate()
        before = copy.deepcopy(run["samples"][0])
        run["relaunch_snapshots"] = [{"before": before, "after": copy.deepcopy(before),
                                       "extension": {"values": [math.inf]}}]
        self.assertMalformed(run)

    def test_legacy_nonfinite_numeric_text_rejects_with_structured_failure(self):
        for field, value in (("q0_m", "nan"), ("q1_m", "inf"),
                             ("v0_m_per_s", "-inf"), ("v1_m_per_s", "1e400")):
            run = scalar_aliases(candidate())
            run["samples"][0][field] = value
            self.assertCliFailure(json.dumps({"runs": [run]}))

    def test_missing_legacy_modal_component_is_a_structured_cli_failure(self):
        for field in ("q1_m", "v1_m_per_s"):
            run = scalar_aliases(candidate())
            del run["samples"][0][field]
            self.assertCliFailure(json.dumps({"runs": [run]}))

    def test_csv_nonfinite_numbers_are_rejected_before_optional_imports(self):
        for field, value in (("q0_m", "nan"), ("v1_m_per_s", "1e400"),
                             ("maximum_step_s", "inf"), ("time_s", "-inf")):
            self.assertCliFailure(csv_candidate({field: value}), ".csv")

    def test_csv_missing_or_invalid_numeric_cells_have_structured_failure(self):
        self.assertCliFailure(csv_candidate({"q0_m": "not-a-number"}), ".csv")
        self.assertCliFailure(csv_candidate({"v1_m_per_s": ""}), ".csv")
        self.assertCliFailure("fixture,maximum_step_s\nconservative_impact,0.00002\n", ".csv")

    def test_rejection_does_not_modify_the_candidate_object(self):
        run = candidate()
        run["samples"][0]["q_m"] = [False, 0.0]
        before = copy.deepcopy(run)
        self.assertMalformed(run)
        self.assertEqual(run, before)

    def test_valid_scalar_aliases_preserve_the_incomplete_physics_gate(self):
        original = candidate()
        legacy = scalar_aliases(original)
        with tempfile.TemporaryDirectory() as temporary:
            path = Path(temporary) / "legacy.json"
            text = json.dumps({"runs": [legacy]})
            path.write_text(text, encoding="utf-8")
            normalized = audit.read_runs(path)[0]
            self.assertEqual(path.read_text(encoding="utf-8"), text)
        for actual, expected in zip(normalized["samples"], original["samples"]):
            self.assertEqual(actual["q_m"], expected["q_m"])
            self.assertEqual(actual["v_m_per_s"], expected["v_m_per_s"])
        with fixed_endpoint_oracle():
            result = audit.audit_run(normalized)
        self.assertEqual(result["status"], "incomplete", result)
        self.assertEqual(result["failures"], [])
        self.assertIn("independently located peak compression and force (extrema audit not yet implemented)",
                      result["missing"])


if __name__ == "__main__":
    unittest.main()
