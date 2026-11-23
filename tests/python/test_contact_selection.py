"""Frozen contact evidence selection regressions; no numerical acceptance claim.

The comparator is mocked to isolate routing and duplicate rejection. The real
input preflight and frozen gate decisions run without NumPy or SciPy. Archived
manifest metadata bind the full 208-run selection case to the original sweep.
"""
from __future__ import annotations

import copy
import csv
import importlib.util
import itertools
import json
import math
from pathlib import Path
import random
import sys
import tempfile
import unittest
from unittest.mock import patch


ROOT = Path(__file__).resolve().parents[2]
SPEC = importlib.util.spec_from_file_location(
    "contact_selection_target", ROOT / "tools/validate_contact_reference.py")
if SPEC is None or SPEC.loader is None:
    raise RuntimeError("Cannot load contact auditor")
audit = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = audit
SPEC.loader.exec_module(audit)

LOCAL_DEFAULTS = {"relative_tolerance": 1e-8, "absolute_tolerance": 1e-12,
                  "velocity_absolute_tolerance": 1e-9,
                  "energy_absolute_tolerance": 1e-13,
                  "impulse_absolute_tolerance": 1e-13}
ORIGINAL_SLOTS = [(20e-6, 2.5e-9, 1.0), (10e-6, 2.5e-9, 1.0),
                  (5e-6, 2.5e-9, 1.0), (2.5e-6, 2.5e-9, 1.0),
                  (2.5e-6, 10e-9, 1.0), (2.5e-6, 5e-9, 1.0),
                  (2.5e-6, 2.5e-9, 0.1), (2.5e-6, 2.5e-9, 0.01)]


def candidate(step=20e-6, event=2.5e-9, scale=1.0, name="conservative_impact"):
    sample = {"time_s": 0.0, "q_m": [0.0, 0.0], "v_m_per_s": [0.0, 0.0],
              "hammer_position_m": 0.0, "hammer_velocity_m_per_s": 1.0,
              "hammer_force_n": 0.0, "damper_force_n": 0.0,
              "hammer_impulse_ns": 0.0, "damper_impulse_ns": 0.0,
              "modal_loss_j": 0.0, "hammer_loss_j": 0.0, "damper_loss_j": 0.0,
              "actuator_work_j": 0.0, "launch_work_j": 0.0, "energy_j": 0.004}
    return {"fixture": name, "maximum_step_s": step, "event_tolerance_s": event,
            "output_step_s": 20e-6, "success": True, "failure_code": 0,
            **{key: value * scale for key, value in LOCAL_DEFAULTS.items()},
            "samples": [sample], "events": [], "diagnostics": {}}


def fixture_runs(name="conservative_impact"):
    return [candidate(*slot, name=name) for slot in ORIGINAL_SLOTS]


def settings(run):
    return tuple(run.get(key) for key in audit.RUN_SETTING_FIELDS)


class ContactSelectionTests(unittest.TestCase):
    def setUp(self):
        backend = patch.object(audit, "numerics", side_effect=AssertionError(
            "Selection must not construct an independent numerical backend"))
        backend.start()
        self.addCleanup(backend.stop)

    def select(self, runs):
        with patch.object(audit, "compare_runs", return_value=({"q_m[0]": 0.0}, [])) as compare:
            result = audit.refinement_gates(runs)
        calls = [(a[0]["fixture"], settings(a[0]), a[1]["fixture"], settings(a[1]), kwargs)
                 for a, kwargs in compare.call_args_list]
        return result, calls

    def test_all_permutations_reject_conflicting_duplicate_before_comparison(self):
        runs = fixture_runs()[:4]
        conflict = copy.deepcopy(runs[2])
        conflict["samples"][0]["q_m"][0] = 1e-6
        runs.append(conflict)
        expected = None
        for order in itertools.permutations(runs):
            result, calls = self.select(list(order))
            self.assertEqual(result["status"], "fail")
            self.assertIn("conflicting duplicate", result["failures"][0])
            self.assertEqual(calls, [])
            if expected is None:
                expected = result
            self.assertEqual(result, expected)

    def test_all_permutations_keep_tighter_local_run_off_step_axis(self):
        runs = fixture_runs()[:4] + [fixture_runs()[6]]
        expected = None
        for order in itertools.permutations(runs):
            result, calls = self.select(list(order))
            self.assertEqual(result["status"], "incomplete")
            self.assertIn("conservative_impact:step", result["comparisons"])
            self.assertEqual(len(calls), 3)
            self.assertTrue(all(a[3] == b[3] == 1e-8 for _, a, _, b, _ in calls))
            if expected is None:
                expected = result, calls
            self.assertEqual((result, calls), expected)

    def test_exact_repeats_deduplicate_and_retain_sorted_sources(self):
        runs = fixture_runs()
        runs[2]["source"] = "z.json"
        repeat = copy.deepcopy(runs[2])
        repeat["source"] = "a.json"
        baseline, baseline_calls = self.select(runs)
        duplicate, calls = self.select([repeat, *reversed(runs)])
        self.assertEqual(calls, baseline_calls)
        self.assertEqual(duplicate["comparisons"], baseline["comparisons"])
        self.assertEqual(duplicate, self.select([*runs, repeat])[0])
        records = [entry for entry in duplicate["selection"]
                   if entry["variants"][0]["copies"] == 2]
        self.assertEqual(len(records), 1)
        self.assertTrue(records[0]["selected"])
        self.assertEqual(records[0]["variants"][0]["sources"], ["a.json", "z.json"])
        self.assertEqual(len(records[0]["variants"][0]["scientific_content_sha256"]), 64)

    def test_dictionary_order_does_not_create_false_duplicate_conflict(self):
        run = candidate()
        repeat = dict(reversed(list(run.items())))
        repeat["samples"] = [dict(reversed(list(run["samples"][0].items())))]
        result, _ = self.select([run, repeat])
        self.assertEqual(result["failures"], [])
        self.assertEqual(result["selection"][0]["variants"][0]["copies"], 2)

    def test_conflicting_diagnostics_and_extensions_are_scientific_content(self):
        for field, value in (("diagnostics", {"accepted_steps": 1}),
                             ("unrecognized_solver_metadata", "changed")):
            with self.subTest(field=field):
                run = candidate()
                conflict = copy.deepcopy(run)
                conflict[field] = value
                result, calls = self.select([run, conflict])
                self.assertEqual(result["status"], "fail")
                self.assertIn("conflicting duplicate", result["failures"][0])
                self.assertEqual(calls, [])

    def test_event_and_local_axes_use_exact_frozen_slots_in_any_order(self):
        runs = fixture_runs()
        expected = self.select(runs)
        for seed in range(12):
            random.Random(seed).shuffle(runs)
            self.assertEqual(self.select(runs), expected)
        result, calls = expected
        self.assertEqual(len(calls), 7)
        self.assertEqual(result["failures"], [])
        self.assertFalse(any(item.startswith("conservative_impact:") for item in result["missing"]))
        self.assertEqual([call[1][1] for call in calls[3:5]], [10e-9, 5e-9])
        self.assertTrue(all(call[1][3] == call[3][3] == 1e-8 for call in calls[3:5]))
        self.assertEqual([call[1][3] for call in calls[5:]], [1e-8, 1e-9])
        self.assertEqual([call[3][3] for call in calls[5:]], [1e-9, 1e-10])
        self.assertTrue(all(call[4] == {"factor": 0.1} for call in calls[3:]))

    def test_each_of_original_eight_slots_is_required(self):
        for index in range(8):
            with self.subTest(index=index):
                runs = fixture_runs()
                runs.pop(index)
                result, _ = self.select(runs)
                self.assertEqual(result["status"], "incomplete")
                axis = "step" if index < 4 else "event" if index < 6 else "local"
                self.assertNotIn("conservative_impact:" + axis, result["comparisons"])
                self.assertEqual(len(result["selection"]), 7)

    def test_arbitrary_local_scales_cannot_satisfy_frozen_axis(self):
        for scales in ((0.8, 0.6), (0.25, 0.0625), (0.01, 0.001)):
            with self.subTest(scales=scales):
                runs = fixture_runs()[:6] + [candidate(2.5e-6, 2.5e-9, scale) for scale in scales]
                result, calls = self.select(runs)
                self.assertEqual(result["status"], "fail")
                self.assertTrue(any("undeclared refinement settings" in item for item in result["failures"]))
                self.assertNotIn("conservative_impact:local", result["comparisons"])
                self.assertEqual(calls, [])

    def test_every_emitted_control_participates_in_slot_selection(self):
        for key in audit.RUN_SETTING_FIELDS:
            with self.subTest(key=key):
                runs = fixture_runs()
                runs[0][key] *= 0.8
                result, calls = self.select(runs)
                self.assertEqual(result["status"], "fail")
                self.assertIn("undeclared refinement settings", result["failures"][0])
                self.assertEqual(calls, [])

    def test_missing_optional_controls_remain_incomplete_and_never_inferred(self):
        for key in ("output_step_s", *LOCAL_DEFAULTS):
            with self.subTest(key=key):
                runs = fixture_runs()
                del runs[0][key]
                result, _ = self.select(runs)
                self.assertEqual(result["status"], "incomplete")
                self.assertTrue(any("missing " + key in item for item in result["missing"]))
                self.assertNotIn("conservative_impact:step", result["comparisons"])

    def test_nonfinite_and_invalid_settings_fail_before_comparison(self):
        for key in audit.RUN_SETTING_FIELDS:
            for value in (math.nan, math.inf, -1.0, 0.0, True, "1e-8"):
                with self.subTest(key=key, value=value):
                    runs = fixture_runs()
                    runs[0][key] = value
                    result, calls = self.select(runs)
                    self.assertEqual(result["status"], "fail")
                    self.assertEqual(calls, [])
                    json.dumps(result, allow_nan=False)

    def test_malformed_and_explicit_failure_reports_are_order_independent(self):
        for field, value in (("relative_tolerance", math.nan), ("failure_code", 3)):
            runs = fixture_runs()
            runs[0][field] = value
            self.assertEqual(self.select(runs), self.select(list(reversed(runs))))

    def test_undeclared_finer_steps_require_a_separate_explicit_policy(self):
        result, calls = self.select(fixture_runs() + [candidate(1.25e-6)])
        self.assertEqual(result["status"], "fail")
        self.assertIn("undeclared refinement settings", result["failures"][0])
        self.assertEqual(calls, [])

    def test_settings_matching_is_tight_and_same_slot_near_duplicates_conflict(self):
        run = candidate()
        close = copy.deepcopy(run)
        close["relative_tolerance"] *= 1 + 5e-13
        self.assertEqual(self.select([close])[0]["failures"], [])
        self.assertIn("conflicting duplicate", self.select([run, close])[0]["failures"][0])
        close["relative_tolerance"] = run["relative_tolerance"] * (1 + 1e-10)
        self.assertIn("undeclared refinement settings", self.select([close])[0]["failures"][0])

    def test_coordinate_pairs_require_matching_baseline_local_settings(self):
        runs = fixture_runs("supported_impact") + fixture_runs("supported_impact_scaled")
        result, calls = self.select(list(reversed(runs)))
        structural = [call for call in calls if call[4].get("structural")]
        self.assertEqual(len(structural), 1)
        self.assertEqual(structural[0][1], structural[0][3])
        self.assertEqual(structural[0][1][3], 1e-8)
        self.assertEqual(result, self.select(runs)[0])
        runs.pop(8 + 3)
        result, calls = self.select(runs)
        self.assertNotIn("supported_impact:scaling", result["comparisons"])
        self.assertIn("supported_impact: signed coordinate rescaling", result["missing"])
        self.assertFalse(any(call[4].get("structural") for call in calls))

    def test_original_208_manifest_satisfies_all_frozen_selection_slots(self):
        manifest = json.loads((ROOT / "docs/validation/contact-generation-manifest-20261123.json").read_text())
        metadata = manifest["metadata"]
        self.assertEqual(metadata["local_tolerance_scales"], [1.0, 0.1, 0.01])
        self.assertEqual(metadata["maximum_step_levels_s"], [20e-6, 10e-6, 5e-6, 2.5e-6])
        self.assertEqual(metadata["event_levels_s"], [10e-9, 5e-9, 2.5e-9])
        runs = [candidate(item["maximum_step_s"], item["event_tolerance_s"],
                          item["tolerance_scale"], item["fixture"]) for item in metadata["runs"]]
        self.assertEqual(len(runs), 208)
        expected = self.select(runs)
        result, calls = expected
        self.assertEqual(result["status"], "pass")  # Selection only; comparisons are mocked.
        self.assertEqual(result["missing"], [])
        self.assertEqual(result["failures"], [])
        self.assertEqual(len(result["selection"]), 208)
        self.assertEqual(len(calls), 26 * 7 + 3)
        for seed in range(3):
            random.Random(seed).shuffle(runs)
            self.assertEqual(self.select(runs), expected)

    def test_fixture_subset_cannot_establish_complete_selection(self):
        result, _ = self.select(fixture_runs())
        absent = [item for item in result["missing"] if item.startswith("absent fixture: ")]
        self.assertEqual(result["status"], "incomplete")
        self.assertEqual(len(absent), 25)


class ContactSelectionCSVTests(unittest.TestCase):
    def read_csv(self, rows):
        with tempfile.TemporaryDirectory() as temporary:
            path = Path(temporary) / "candidate.csv"
            with path.open("w", newline="", encoding="utf-8") as handle:
                writer = csv.DictWriter(handle, fieldnames=list(rows[0]))
                writer.writeheader()
                writer.writerows(rows)
            return audit.read_runs(path)

    @staticmethod
    def row(run, time):
        sample = copy.deepcopy(run["samples"][0])
        sample["time_s"] = time
        sample["q0_m"], sample["q1_m"] = sample.pop("q_m")
        sample["v0_m_per_s"], sample["v1_m_per_s"] = sample.pop("v_m_per_s")
        return {"fixture": run["fixture"], **{key: run[key] for key in audit.RUN_SETTING_FIELDS}, **sample}

    def test_local_settings_are_run_metadata_and_never_merge_distinct_axes(self):
        runs = [candidate(2.5e-6, 2.5e-9, scale) for scale in (1.0, 0.1, 0.01)]
        rows = [self.row(run, time) for time in (0.0, 20e-6) for run in runs]
        actual = self.read_csv(rows)
        self.assertEqual(len(actual), 3)
        self.assertEqual([settings(run) for run in actual], [settings(run) for run in runs])
        self.assertEqual([len(run["samples"]) for run in actual], [2, 2, 2])
        self.assertTrue(all(not set(audit.RUN_SETTING_FIELDS).intersection(run["samples"][0])
                            for run in actual))

    def test_each_extra_control_splits_csv_groups(self):
        for key in ("output_step_s", *LOCAL_DEFAULTS):
            with self.subTest(key=key):
                run = candidate()
                other = copy.deepcopy(run)
                other[key] *= 0.5
                actual = self.read_csv([self.row(run, 0.0), self.row(other, 0.0)])
                self.assertEqual(len(actual), 2)
                self.assertNotEqual(actual[0][key], actual[1][key])

    def test_equivalent_numeric_text_preserves_one_csv_run(self):
        rows = [self.row(candidate(), time) for time in (0.0, 20e-6)]
        rows[0]["relative_tolerance"] = "1e-8"
        rows[1]["relative_tolerance"] = "0.00000001"
        actual = self.read_csv(rows)
        self.assertEqual(len(actual), 1)
        self.assertEqual(len(actual[0]["samples"]), 2)

    def test_legacy_csv_does_not_invent_missing_local_metadata(self):
        row = self.row(candidate(), 0.0)
        for key in ("output_step_s", *LOCAL_DEFAULTS):
            del row[key]
        actual = self.read_csv([row])
        self.assertNotIn("relative_tolerance", actual[0])
        result = audit.refinement_gates(actual)
        self.assertEqual(result["status"], "incomplete")
        self.assertEqual(result["selection"], [])


if __name__ == "__main__":
    unittest.main()
