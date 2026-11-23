"""Action evidence must match its frozen command and independent trajectory.

The retained unit oracle is a hand-derived two-mass separation/relaunch, so this
suite needs no NumPy/SciPy. The real analytic and Radau oracles are checked in the
source-bound A070 experiment; these tests exercise the actual parser and gates.
"""
import copy
import hashlib
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
SPEC = importlib.util.spec_from_file_location("contact_auditor_actions_target", SCRIPT)
if SPEC is None or SPEC.loader is None:
    raise RuntimeError("Cannot load contact auditor")
audit = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = audit
SPEC.loader.exec_module(audit)

SEPARATION = math.pi / math.sqrt(20000.0 / 0.0016)
WORK = 0.008 * (1.0 - 0.6**2) / 2


def sample(time, position, velocity, hammer_position, hammer_velocity, impulse, work, energy):
    return {
        "time_s": time, "q_m": [position, 0.0], "v_m_per_s": [velocity, 0.0],
        "hammer_position_m": hammer_position, "hammer_velocity_m_per_s": hammer_velocity,
        "hammer_force_n": 0.0, "damper_force_n": 0.0,
        "hammer_impulse_ns": impulse, "damper_impulse_ns": 0.0,
        "modal_loss_j": 0.0, "hammer_loss_j": 0.0, "damper_loss_j": 0.0,
        "actuator_work_j": 0.0, "launch_work_j": work, "energy_j": energy,
        "modal_acceleration_m_per_s2": [0.0, 0.0],
        "hammer_acceleration_m_per_s2": 0.0,
    }


def candidate():
    # At separation, momentum conservation gives 1.6 m/s for the 2 g modal
    # mass and 0.6 m/s for the 8 g hammer. Resetting only hammer speed to 1 m/s
    # adds 0.00256 J, after which the bodies continue separating freely.
    t = SEPARATION
    first = sample(0.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.004)
    before = sample(t, 0.8*t, 1.6, 0.8*t, 0.6, 0.0032, 0.0, 0.004)
    after = sample(t, 0.8*t, 1.6, 0.8*t, 1.0, 0.0032, WORK, 0.004+WORK)
    final = sample(0.002, 0.8*t+1.6*(0.002-t), 1.6,
                   0.8*t+0.002-t, 1.0, 0.0032, WORK, 0.004+WORK)
    events = [{"contact": "hammer", "kind": kind, "time_s": time,
               "bracket_lo_s": time, "bracket_hi_s": time}
              for kind, time in (("onset", 0.0), ("separation", t), ("relaunch", t))]
    return {
        "fixture": "event_at", "success": True, "failure_code": 0,
        "maximum_step_s": 20e-6, "event_tolerance_s": 10e-9,
        "samples": [first, final], "events": events,
        "diagnostics": {"max_abs_cumulative_residual_j": 0.0,
                        "sum_abs_interval_residual_j": 0.0},
        "relaunch_snapshots": [{"time_s": t, "requested_velocity_m_per_s": 1.0,
                                "success": True, "failure_code": 0, "work_j": WORK,
                                "before": before, "after": after}],
    }


def legacy(run):
    run = copy.deepcopy(run)
    for row in run["samples"]:
        row["q0_m"], row["q1_m"] = row.pop("q_m")
        row["v0_m_per_s"], row["v1_m_per_s"] = row.pop("v_m_per_s")
    return run


class ContactAuditorActionsTests(unittest.TestCase):
    cli_calls = 0

    def setUp(self):
        self.run = candidate()
        reference = {row["time_s"]: copy.deepcopy(row) for row in self.run["samples"]}
        f = audit.fixtures()["event_at"]
        # These independent vectors are not extracted from candidate snapshots.
        before = [0.8*SEPARATION, 0.0, 1.6, 0.0, 0.8*SEPARATION, 0.6,
                  0.0, 0.0, 0.0, 0.0, 0.0, 0.0032, 0.0]
        after = [0.8*SEPARATION, 0.0, 1.6, 0.0, 0.8*SEPARATION, 1.0,
                 0.0, 0.0, 0.0, 0.0, WORK, 0.0032, 0.0]
        self.oracle = SimpleNamespace(
            fixture=f, method="independent exact two-mass action endpoints",
            sample=lambda time: copy.deepcopy(reference[time]),
            events=copy.deepcopy(self.run["events"]),
            relaunch_states=((SEPARATION, before, after),))
        for target, replacement in (
                ("oracle_for", lambda name: self.oracle),
                ("initial_vector", lambda fixture: [*fixture.q, *fixture.v,
                                                    fixture.x, fixture.vh, 0, 0, 0, 0, 0, 0, 0])):
            context = patch.object(audit, target, side_effect=replacement)
            context.start()
            self.addCleanup(context.stop)
        backend = patch.object(audit, "numerics", side_effect=AssertionError("No backend in unit tests"))
        backend.start()
        self.addCleanup(backend.stop)

    def assertFailure(self, run, fragment, *, before_oracle=False):
        if before_oracle:
            with patch.object(audit, "oracle_for", side_effect=AssertionError("Unexpected oracle call")):
                result = audit.audit_run(run)
        else:
            result = audit.audit_run(run)
        self.assertEqual(result["status"], "fail", result)
        self.assertTrue(any(fragment in failure for failure in result["failures"]), result)
        json.dumps(result, allow_nan=False)
        return result

    def assertCliFailure(self, run, fragment):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "candidate.json"
            output = Path(directory) / "report.json"
            path.write_text(json.dumps({"runs": [run]}), encoding="utf-8")
            digest = hashlib.sha256(path.read_bytes()).hexdigest()
            completed = subprocess.run(
                [sys.executable, "-S", str(SCRIPT), "--candidate", str(path), "--output", str(output)],
                text=True, capture_output=True, timeout=15)
            type(self).cli_calls += 1
            self.assertEqual(completed.returncode, 1, completed.stderr)
            self.assertNotIn("Traceback", completed.stderr)
            report = json.loads(output.read_text(encoding="utf-8"))
            self.assertEqual(report["status"], "fail", report)
            self.assertIn(fragment, json.dumps(report))
            self.assertEqual(json.loads(completed.stdout)["status"], "fail")
            self.assertEqual(hashlib.sha256(path.read_bytes()).hexdigest(), digest)

    def test_legacy_components_require_actual_finite_numbers(self):
        for field in ("q0_m", "q1_m", "v0_m_per_s", "v1_m_per_s"):
            for value in (True, False, 10**400, -(10**400), "0", math.inf, math.nan):
                run = legacy(self.run)
                row = run["samples"][0]
                row[field] = value
                with self.subTest(field=field, value=str(value)[:20]):
                    with self.assertRaisesRegex(ValueError, "Legacy component " + field):
                        audit.normalize_sample(row)

    def test_finite_legacy_aliases_normalize_without_mutating_caller(self):
        original = legacy(self.run)
        before = copy.deepcopy(original)
        for actual, expected in zip(original["samples"], self.run["samples"]):
            normalized = audit.normalize_sample(actual)
            self.assertEqual(normalized["q_m"], expected["q_m"])
            self.assertEqual(normalized["v_m_per_s"], expected["v_m_per_s"])
        self.assertEqual(original, before)

    def test_legacy_overflow_and_boolean_cli_failures_are_structured(self):
        for field in ("q0_m", "q1_m", "v0_m_per_s", "v1_m_per_s"):
            for value in (False, 10**400):
                run = legacy(self.run)
                run["samples"][0][field] = value
                self.assertCliFailure(run, "Legacy component " + field)

    def test_nonzero_run_codes_fail_with_any_success_flag_or_coverage(self):
        for flag in (True, False, None):
            for samples in (self.run["samples"], []):
                run = copy.deepcopy(self.run)
                run.update(failure_code=5, samples=copy.deepcopy(samples))
                if flag is None:
                    run.pop("success")
                else:
                    run["success"] = flag
                self.assertFailure(run, "nonzero failure_code", before_oracle=True)
                refinement = audit.refinement_gates([run])
                self.assertEqual(refinement["status"], "fail", refinement)

    def test_valid_empty_coverage_with_zero_code_remains_incomplete(self):
        run = copy.deepcopy(self.run)
        run["samples"] = []
        result = audit.audit_run(run)
        self.assertEqual(result["status"], "incomplete", result)
        self.assertEqual(result["failures"], [])
        self.assertIn("trajectory samples", result["missing"])

    def test_failed_snapshot_flags_and_codes_cannot_be_ignored(self):
        for field, value in (("success", False), ("failure_code", 5)):
            run = copy.deepcopy(self.run)
            run["relaunch_snapshots"][0][field] = value
            self.assertFailure(run, "action failure" if field == "success" else "failure_code",
                               before_oracle=True)

    def test_snapshot_failure_code_domain_is_explicit(self):
        for value in (True, False, -1, 0.5, "0", None):
            run = copy.deepcopy(self.run)
            run["relaunch_snapshots"][0]["failure_code"] = value
            self.assertFailure(run, "failure_code must be a nonnegative integer", before_oracle=True)

    def test_absent_snapshot_is_incomplete_but_explicit_wrong_count_fails(self):
        run = copy.deepcopy(self.run)
        del run["relaunch_snapshots"]
        result = audit.audit_run(run)
        self.assertEqual(result["status"], "incomplete", result)
        self.assertEqual(result["failures"], [])
        self.assertIn("before/after relaunch snapshots for exact state preservation", result["missing"])
        for count in (0, 2, 3):
            run = copy.deepcopy(self.run)
            run["relaunch_snapshots"] *= count
            self.assertFailure(run, "snapshot count", before_oracle=True)

    def test_unscheduled_snapshot_is_rejected(self):
        run = copy.deepcopy(self.run)
        run["fixture"] = "conservative_impact"
        self.assertFailure(run, "snapshot count", before_oracle=True)

    def test_missing_snapshot_metadata_remains_explicitly_incomplete(self):
        for field in ("time_s", "requested_velocity_m_per_s", "success", "failure_code", "work_j"):
            run = copy.deepcopy(self.run)
            del run["relaunch_snapshots"][0][field]
            result = audit.audit_run(run)
            self.assertEqual(result["status"], "incomplete", result)
            self.assertEqual(result["failures"], [], result)
            self.assertIn("relaunch_snapshots[0]." + field, result["missing"])
        for side in ("before", "after"):
            run = copy.deepcopy(self.run)
            del run["relaunch_snapshots"][0][side]["time_s"]
            result = audit.audit_run(run)
            self.assertEqual(result["status"], "incomplete", result)
            self.assertEqual(result["failures"], [])
            self.assertIn("relaunch_snapshots[0]." + side + ".time_s", result["missing"])

    def test_snapshot_times_and_requested_velocity_follow_frozen_action(self):
        for field, value, fragment in (("time_s", -1, "outside the frozen duration"),
                                       ("time_s", SEPARATION+1e-5, "frozen action"),
                                       ("requested_velocity_m_per_s", 999, "requested velocity")):
            run = copy.deepcopy(self.run)
            run["relaunch_snapshots"][0][field] = value
            self.assertFailure(run, fragment, before_oracle=True)
        for side in ("before", "after"):
            run = copy.deepcopy(self.run)
            run["relaunch_snapshots"][0][side]["time_s"] += 1e-10
            self.assertFailure(run, "instantaneous action time", before_oracle=True)

    def test_final_hammer_velocity_is_assigned_exactly(self):
        run = copy.deepcopy(self.run)
        run["relaunch_snapshots"][0]["after"]["hammer_velocity_m_per_s"] = math.nextafter(1.0, 2.0)
        self.assertFailure(run, "does not equal the requested value", before_oracle=True)

    def test_relaunch_preserves_positions_modes_and_unrelated_ledgers(self):
        for key in ("q_m", "v_m_per_s", "hammer_position_m", "modal_loss_j", "hammer_loss_j",
                    "damper_loss_j", "actuator_work_j", "hammer_impulse_ns", "damper_impulse_ns"):
            run = copy.deepcopy(self.run)
            after = run["relaunch_snapshots"][0]["after"]
            if isinstance(after[key], list):
                after[key][0] += 1e-8
            else:
                after[key] += 1e-8
            self.assertFailure(run, "changed preserved " + key, before_oracle=True)

    def test_work_energy_and_launch_ledger_must_match_kinetic_change(self):
        for field in ("work_j", "energy_j", "launch_work_j"):
            run = copy.deepcopy(self.run)
            action = run["relaunch_snapshots"][0]
            target = action if field == "work_j" else action["after"]
            target[field] += 0.001
            self.assertFailure(run, "disagrees with hammer kinetic work", before_oracle=True)
        run = copy.deepcopy(self.run)
        action = run["relaunch_snapshots"][0]
        action["work_j"] = 1.0
        action["after"].update(energy_j=1.004, launch_work_j=1.0)
        self.assertFailure(run, "disagrees with hammer kinetic work", before_oracle=True)

    def test_self_consistent_forged_snapshot_fails_independent_oracle(self):
        run = copy.deepcopy(self.run)
        action = run["relaunch_snapshots"][0]
        action["before"].update(hammer_velocity_m_per_s=0.5,
                                energy_j=0.00356, launch_work_j=-0.00044)
        action["work_j"] = 0.003
        self.assertEqual(audit.validate_action_evidence(run), ([], []))
        self.assertFailure(run, "relaunch_snapshots[0].before: hammer_velocity")

    def test_equal_snapshot_displacements_cannot_forge_trajectory(self):
        run = copy.deepcopy(self.run)
        for side in ("before", "after"):
            sample_value = run["relaunch_snapshots"][0][side]
            sample_value["q_m"][0] += 1e-4
            sample_value["hammer_position_m"] += 1e-4
        self.assertEqual(audit.validate_action_evidence(run), ([], []))
        self.assertFailure(run, "relaunch_snapshots[0].before: q_m[0]")

    def test_independent_action_work_uses_stricter_existing_event_budget(self):
        run = copy.deepcopy(self.run)
        action = run["relaunch_snapshots"][0]
        velocity = 0.60001  # Within the ordinary trajectory velocity budget.
        changed_energy = 0.008 * (velocity**2 - 0.6**2) / 2
        action["before"].update(hammer_velocity_m_per_s=velocity,
                                energy_j=0.004+changed_energy)
        action["work_j"] = WORK-changed_energy
        action["after"]["launch_work_j"] = action["work_j"]
        self.assertEqual(audit.validate_action_evidence(run), ([], []))
        self.assertFailure(run, "differs from independent action work")

    def test_derived_action_overflows_produce_finite_failure_reports(self):
        for key, value in (("hammer_velocity_m_per_s", 1e308), ("energy_j", -1e308)):
            run = copy.deepcopy(self.run)
            action = run["relaunch_snapshots"][0]
            action["before"][key] = value
            if key == "energy_j":
                action["after"][key] = 1e308
            self.assertFailure(run, "kinetic work", before_oracle=True)

    def test_actual_cli_rejects_action_contradictions_without_optional_backend(self):
        cases = []
        run = copy.deepcopy(self.run)
        run["failure_code"] = 5
        cases.append((run, "failure_code"))
        for key, value in (("success", False), ("failure_code", 5), ("time_s", -1),
                           ("requested_velocity_m_per_s", 999), ("work_j", 1.0)):
            run = copy.deepcopy(self.run)
            run["relaunch_snapshots"][0][key] = value
            cases.append((run, "relaunch_snapshots"))
        for count in (0, 2):
            run = copy.deepcopy(self.run)
            run["relaunch_snapshots"] *= count
            cases.append((run, "snapshot count"))
        for run, fragment in cases:
            self.assertCliFailure(run, fragment)

    def test_valid_action_remains_incomplete_only_for_unimplemented_extrema(self):
        before = copy.deepcopy(self.run)
        result = audit.audit_run(self.run)
        self.assertEqual(result["status"], "incomplete", result)
        self.assertEqual(result["failures"], [])
        self.assertEqual(result["missing"], [
            "independently located peak compression and force (extrema audit not yet implemented)"])
        self.assertEqual(self.run, before)

    @classmethod
    def tearDownClass(cls):
        print(f"Action-auditor suite executed {cls.cli_calls} actual CLI calls")


if __name__ == "__main__":
    unittest.main()
