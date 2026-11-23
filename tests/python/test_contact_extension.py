"""Bounded contact policy regressions; mocked comparisons are not acceptance.

Real input preflight, complete-settings selection, matrix requirements and
decrease checks run here without constructing any independent numerical oracle.
"""
from __future__ import annotations

from contextlib import redirect_stderr, redirect_stdout
import copy
import hashlib
import importlib.util
import io
import json
import math
from pathlib import Path
import random
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch


ROOT = Path(__file__).resolve().parents[2]


def load_tool(name, filename):
    spec = importlib.util.spec_from_file_location(name, ROOT / "tools" / filename)
    if spec is None or spec.loader is None:
        raise RuntimeError("Cannot load contact tool: " + filename)
    module = importlib.util.module_from_spec(spec)
    sys.modules[name] = module
    spec.loader.exec_module(module)
    return module


audit = load_tool("contact_extension_audit", "validate_contact_reference.py")
with patch.dict(sys.modules, {"validate_contact_reference": audit}):
    sweep = load_tool("contact_extension_sweep", "run_contact_sweep.py")

LOCAL_DEFAULTS = {"relative_tolerance": 1e-8, "absolute_tolerance": 1e-12,
                  "velocity_absolute_tolerance": 1e-9,
                  "energy_absolute_tolerance": 1e-13,
                  "impulse_absolute_tolerance": 1e-13}
ORIGINAL_SLOTS = [(20e-6, 2.5e-9, 1.0), (10e-6, 2.5e-9, 1.0),
                  (5e-6, 2.5e-9, 1.0), (2.5e-6, 2.5e-9, 1.0),
                  (2.5e-6, 10e-9, 1.0), (2.5e-6, 5e-9, 1.0),
                  (2.5e-6, 2.5e-9, 0.1), (2.5e-6, 2.5e-9, 0.01)]
CORE_RESULT_FIELDS = ("status", "failures", "missing", "comparisons", "selection",
                      "nonlinear_solve_refinement")


def expected_slots(depth):
    slots = list(ORIGINAL_SLOTS)
    if depth:
        slots.append((1.25e-6, 2.5e-9, 1.0))
        if depth == 2:
            slots.append((0.625e-6, 2.5e-9, 1.0))
        finest = 1.25e-6 if depth == 1 else 0.625e-6
        slots.extend([(finest, 10e-9, 1.0), (finest, 5e-9, 1.0),
                      (finest, 2.5e-9, 0.1), (finest, 2.5e-9, 0.01)])
    return slots


def candidate(step, event, scale, name="conservative_impact"):
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


def fixture_runs(depth, name="conservative_impact"):
    return [candidate(*slot, name=name) for slot in expected_slots(depth)]


def full_runs(depth):
    return [run for name in sorted(audit.fixtures()) for run in fixture_runs(depth, name)]


def controls(run):
    return tuple(run[key] for key in audit.RUN_SETTING_FIELDS)


class ContactExtensionTests(unittest.TestCase):
    def setUp(self):
        backend = patch.object(audit, "numerics", side_effect=AssertionError(
            "A matrix/policy regression must not construct a numerical oracle"))
        backend.start()
        self.addCleanup(backend.stop)

    def evaluate(self, runs, depth=0, comparator=None):
        kwargs = {"side_effect": comparator} if comparator else {"return_value": ({"q_m[0]": 0.0}, [])}
        with patch.object(audit, "compare_runs", **kwargs) as compare:
            result = audit.refinement_gates(runs, depth)
        calls = [(args[0]["fixture"], controls(args[0]), args[1]["fixture"],
                  controls(args[1]), options) for args, options in compare.call_args_list]
        return result, calls

    def cli(self, module, *arguments):
        output, errors = io.StringIO(), io.StringIO()
        with patch.object(sys, "argv", [module.__file__, *arguments]), \
                redirect_stdout(output), redirect_stderr(errors):
            status = module.main()
        return status, output.getvalue(), errors.getvalue()

    def test_all_policy_entry_points_reject_booleans_nonintegers_and_range(self):
        for value in (True, False, -1, 3, 20, 0.0, 1.0, 2.0, "1", None, [], {}):
            for function, arguments in (
                    (sweep.plan, (["conservative_impact"],)),
                    (audit.refinement_gates, ([],)),
                    (audit.select_refinement_runs, ([],)),
                    (audit.refinement_step_levels, ()),
                    (audit.refinement_slots, ()),
                    (audit.refinement_matrix, ()),
                    (audit.refinement_policy, ())):
                with self.subTest(value=value, function=function.__name__):
                    with self.assertRaisesRegex(ValueError, "integer choice of 0, 1, or 2"):
                        function(*arguments, value)

    def test_both_clis_reject_invalid_policy_before_work(self):
        for module, command in ((sweep, "--plan"), (audit, "--list-fixtures")):
            for value in ("true", "false", "1.0", "-1", "3", "nan", "inf", ""):
                with self.subTest(module=module.__name__, value=value), \
                        patch.object(sweep.subprocess, "run") as execute, \
                        patch.object(audit, "read_runs") as read:
                    with self.assertRaises(SystemExit) as failure:
                        self.cli(module, command, "--extra-step-levels", value)
                    self.assertEqual(failure.exception.code, 2)
                    execute.assert_not_called()
                    read.assert_not_called()

    def test_all_full_plans_match_uniform_complete_settings_and_declared_counts(self):
        names = sorted(audit.fixtures())
        self.assertEqual(len(names), 26)
        for depth, count in ((0, 208), (1, 338), (2, 364)):
            with self.subTest(depth=depth):
                items = sweep.plan(names, depth)
                self.assertEqual(items, sweep.plan(list(reversed(names)) + names, depth))
                self.assertEqual(len(items), count)
                self.assertEqual(len({item["id"] for item in items}), count)
                expected = expected_slots(depth)
                for name in names:
                    group = [item for item in items if item["fixture"] == name]
                    observed = [(item["maximum_step_s"], item["event_tolerance_s"],
                                 item["tolerance_scale"]) for item in group]
                    self.assertEqual(observed, expected)
                    self.assertEqual(len(set(observed)), count // 26)
                    self.assertEqual(observed[:8], ORIGINAL_SLOTS)
                    self.assertTrue(all(item["output_step_s"] == 20e-6 for item in group))
                    self.assertEqual({controls(candidate(*slot)) for slot in observed},
                                     set(audit.refinement_slots(depth)))
                result, _ = self.evaluate(full_runs(depth), depth)
                self.assertEqual(result["status"], "pass")
                self.assertEqual(result["missing"], [])
                self.assertEqual(len(result["selection"]), count)
                if depth:
                    self.assertEqual(result["matrix"]["run_count"], count)
                    self.assertEqual(result["original_frozen_result"]["matrix"]["run_count"], 208)

    def test_default_plan_and_results_keep_the_archived_208_matrix(self):
        manifest = json.loads((ROOT / "docs/validation/contact-generation-manifest-20261123.json").read_text())
        original = sweep.plan(sorted(audit.fixtures()))
        self.assertEqual(original, manifest["metadata"]["runs"])
        self.assertEqual(original, sweep.plan(sorted(audit.fixtures()), 0))
        runs = full_runs(0)
        with patch.object(audit, "compare_runs", return_value=({"q_m[0]": 0.0}, [])):
            default = audit.refinement_gates(runs)
            explicit = audit.refinement_gates(runs, 0)
        self.assertEqual(default, explicit)
        self.assertNotIn("extended_result", default)
        self.assertEqual(len(default["selection"]), 208)

    def test_matrix_hash_binds_every_required_fixture_and_setting(self):
        hashes = set()
        for depth in (0, 1, 2):
            manifest = audit.refinement_matrix(depth)
            settings = [{"fixture": name, "settings": dict(zip(audit.RUN_SETTING_FIELDS, slot))}
                        for name in sorted(audit.fixtures())
                        for slot in sorted(controls(candidate(*item)) for item in expected_slots(depth))]
            encoded = json.dumps(settings, sort_keys=True, separators=(",", ":"), allow_nan=False).encode()
            self.assertEqual(manifest["settings_sha256"], hashlib.sha256(encoded).hexdigest())
            self.assertEqual(manifest["slots_per_fixture"], (8, 13, 14)[depth])
            hashes.add(manifest["settings_sha256"])
            subset = audit.refinement_matrix(depth, ["conservative_impact"])
            self.assertNotEqual(subset["settings_sha256"], manifest["settings_sha256"])
            self.assertEqual(subset["run_count"], (8, 13, 14)[depth])
        self.assertEqual(len(hashes), 3)

    def test_plan_cli_declares_selected_and_required_matrices_without_writing(self):
        with tempfile.TemporaryDirectory() as temporary:
            output = Path(temporary) / "absent"
            for depth, count in ((0, 208), (1, 338), (2, 364)):
                with self.subTest(depth=depth):
                    status, stdout, stderr = self.cli(sweep, "--plan", "--output-dir", str(output),
                                                       "--extra-step-levels", str(depth))
                    plan = json.loads(stdout)
                    self.assertEqual((status, stderr), (0, ""))
                    self.assertEqual(len(plan["runs"]), count)
                    self.assertEqual(plan["refinement_policy"], audit.refinement_policy(depth))
                    self.assertEqual(plan["matrix"], plan["required_matrix"])
                    self.assertEqual(plan["original_frozen_matrix"]["run_count"], 208)
                    self.assertFalse(output.exists())
            _, stdout, _ = self.cli(sweep, "--plan", "--fixture", "conservative_impact",
                                    "--extra-step-levels", "1")
            subset = json.loads(stdout)
            self.assertEqual(subset["matrix"]["run_count"], 13)
            self.assertEqual(subset["required_matrix"]["run_count"], 338)

    def test_policy_is_never_inferred_from_available_steps(self):
        for depth in (1, 2):
            runs = fixture_runs(depth)
            result, calls = self.evaluate(runs)
            self.assertEqual(result["status"], "fail")
            self.assertTrue(any("undeclared refinement settings" in item for item in result["failures"]))
            self.assertEqual(calls, [])
        for declared, supplied in ((1, 2), (2, 1)):
            result, calls = self.evaluate(fixture_runs(supplied), declared)
            self.assertEqual(result["status"], "fail")
            self.assertEqual(result["comparisons"], {})
            self.assertTrue(all(a[0] >= 2.5e-6 and b[0] >= 2.5e-6 for _, a, _, b, _ in calls))

    def test_depth_two_requires_no_intermediate_event_or_local_axes(self):
        result, _ = self.evaluate(full_runs(2), 2)
        self.assertEqual(result["status"], "pass")
        at_intermediate = [entry for entry in result["selection"]
                           if entry["settings"]["maximum_step_s"] == 1.25e-6]
        self.assertEqual(len(at_intermediate), 26)
        self.assertTrue(all(entry["settings"]["event_tolerance_s"] == 2.5e-9 and
                            entry["settings"]["relative_tolerance"] == 1e-8
                            for entry in at_intermediate))

    def test_partial_fixture_sets_remain_incomplete_at_both_depths(self):
        for depth in (1, 2):
            result, _ = self.evaluate(fixture_runs(depth), depth)
            self.assertEqual(result["status"], "incomplete")
            self.assertEqual(len([item for item in result["missing"] if item.startswith("absent fixture:")]), 25)
            self.assertEqual(len(result["missing_slots"]), 25 * (13 if depth == 1 else 14))
            self.assertEqual(result["original_frozen_result"]["status"], "incomplete")

    def test_each_finest_axis_slot_is_required_and_not_replaced_by_old_axes(self):
        for depth in (1, 2):
            finest = 1.25e-6 if depth == 1 else 0.625e-6
            for event, scale in ((2.5e-9, 1.0), (10e-9, 1.0), (5e-9, 1.0),
                                 (2.5e-9, 0.1), (2.5e-9, 0.01)):
                with self.subTest(depth=depth, event=event, scale=scale):
                    omitted = controls(candidate(finest, event, scale))
                    runs = [run for run in full_runs(depth) if not (
                        run["fixture"] == "conservative_impact" and controls(run) == omitted)]
                    result, _ = self.evaluate(runs, depth)
                    self.assertEqual(result["status"], "incomplete")
                    self.assertEqual(result["original_frozen_result"]["status"], "pass")
                    self.assertEqual(len(result["missing_slots"]), 1)
                    self.assertEqual(result["missing_slots"][0]["settings"], dict(zip(audit.RUN_SETTING_FIELDS, omitted)))

    def test_retained_original_axes_cannot_be_omitted_from_an_extension(self):
        for slot in ORIGINAL_SLOTS[4:]:
            omitted = controls(candidate(*slot))
            runs = [run for run in full_runs(1) if not (
                run["fixture"] == "conservative_impact" and controls(run) == omitted)]
            result, _ = self.evaluate(runs, 1)
            self.assertEqual(result["status"], "incomplete")
            self.assertEqual(result["original_frozen_result"]["status"], "incomplete")
            self.assertEqual(len(result["missing_slots"]), 1)
            self.assertIn("conservative_impact:event", result["comparisons"])
            self.assertIn("conservative_impact:local", result["comparisons"])

    def test_original_only_evidence_cannot_satisfy_declared_extension(self):
        for depth, absent in ((1, 130), (2, 156)):
            result, _ = self.evaluate(full_runs(0), depth)
            self.assertEqual(result["status"], "incomplete")
            self.assertEqual(result["original_frozen_result"]["status"], "pass")
            self.assertEqual(len(result["missing_slots"]), absent)

    def test_complete_controls_remain_fixed_along_steps_and_coordinate_pairs(self):
        for depth in (1, 2):
            result, calls = self.evaluate(full_runs(depth), depth)
            self.assertEqual(result["status"], "pass")
            step_calls = [call for call in calls if not call[4]]
            self.assertTrue(all(a[1:] == b[1:] for _, a, _, b, _ in step_calls))
            self.assertTrue(all(a[3] == b[3] == 1e-8 for _, a, _, b, _ in step_calls))
            structural = [call for call in calls if call[4].get("structural")]
            self.assertEqual(len(structural), 6)  # Three original and three extended pairs.
            self.assertTrue(all(a == b for _, a, _, b, _ in structural))
            self.assertEqual({a[0] for _, a, _, _, _ in structural},
                             {2.5e-6, 1.25e-6 if depth == 1 else 0.625e-6})
            tightening = [call for call in calls if call[4].get("factor")]
            self.assertTrue(all(call[4] == {"factor": 0.1} for call in tightening))

    def test_every_emitted_control_still_participates_in_extended_selection(self):
        for key in audit.RUN_SETTING_FIELDS:
            runs = fixture_runs(1)
            runs[-1][key] *= 0.8
            result, calls = self.evaluate(runs, 1)
            self.assertEqual(result["status"], "fail")
            self.assertTrue(any("undeclared refinement settings" in item for item in result["failures"]))
            self.assertEqual(result["comparisons"], {})
            self.assertTrue(all(a[0] >= 2.5e-6 and b[0] >= 2.5e-6 for _, a, _, b, _ in calls))

    def test_substituted_local_scales_never_satisfy_finest_axis(self):
        for depth in (1, 2):
            for scale in (0.25, 0.0625, 0.8, 0.6, 0.001):
                runs = fixture_runs(depth)
                for key, value in LOCAL_DEFAULTS.items():
                    runs[-1][key] = value * scale
                result, calls = self.evaluate(runs, depth)
                self.assertEqual(result["status"], "fail")
                self.assertEqual(result["comparisons"], {})
                self.assertTrue(all(a[0] >= 2.5e-6 and b[0] >= 2.5e-6 for _, a, _, b, _ in calls))

    def test_missing_optional_controls_are_incomplete_and_never_guessed(self):
        for key in ("output_step_s", *LOCAL_DEFAULTS):
            runs = full_runs(1)
            del runs[-1][key]
            result, _ = self.evaluate(runs, 1)
            self.assertEqual(result["status"], "incomplete")
            self.assertTrue(any("missing " + key in item for item in result["missing"]))
            self.assertEqual(len(result["missing_slots"]), 1)
            self.assertEqual(result["original_frozen_result"]["status"], "pass")

    def test_conflicting_duplicates_fail_before_the_affected_grid_compares_in_both_orders(self):
        for depth in (1, 2):
            for index in (3, -1):
                runs = fixture_runs(depth)
                conflict = copy.deepcopy(runs[index])
                conflict["samples"][0]["q_m"][0] = 1e-6
                forward, calls = self.evaluate([*runs, conflict], depth)
                backward, reverse_calls = self.evaluate([conflict, *reversed(runs)], depth)
                self.assertEqual(forward, backward)
                self.assertEqual(forward["status"], "fail")
                self.assertTrue(any("conflicting duplicate" in item for item in forward["failures"]))
                self.assertEqual(forward["comparisons"], {})
                self.assertEqual(calls, reverse_calls)
                if index == 3:
                    self.assertEqual(calls, [])
                    self.assertEqual(forward["original_frozen_result"]["status"], "fail")
                else:
                    self.assertEqual(forward["original_frozen_result"]["failures"], [])
                    self.assertTrue(all(a[0] >= 2.5e-6 and b[0] >= 2.5e-6 for _, a, _, b, _ in calls))

    def test_invalid_finer_evidence_cannot_relabel_the_original_result(self):
        original, _ = self.evaluate(full_runs(0))
        for field, value in (("failure_code", 3), ("relative_tolerance", True),
                             ("samples", "invalid"), ("event_tolerance_s", math.inf)):
            runs = full_runs(1)
            runs[-1][field] = value
            result, calls = self.evaluate(runs, 1)
            self.assertEqual(result["status"], "fail")
            self.assertEqual(result["comparisons"], {})
            self.assertEqual({key: result["original_frozen_result"][key] for key in CORE_RESULT_FIELDS}, original)
            self.assertTrue(all(a[0] >= 2.5e-6 and b[0] >= 2.5e-6 for _, a, _, b, _ in calls))
            json.dumps(result, allow_nan=False)

    def test_incomplete_original_result_remains_separate_and_unchanged(self):
        original_runs = fixture_runs(0)
        original_runs.pop(4)
        original, _ = self.evaluate(original_runs)
        result, _ = self.evaluate(original_runs + fixture_runs(1)[8:], 1)
        self.assertEqual({key: result["original_frozen_result"][key] for key in CORE_RESULT_FIELDS}, original)
        self.assertEqual(result["status"], "incomplete")
        empty, _ = self.evaluate([])
        only_extension, _ = self.evaluate(fixture_runs(1)[8:], 1)
        self.assertEqual({key: only_extension["original_frozen_result"][key] for key in CORE_RESULT_FIELDS}, empty)

    def test_exact_repeats_deduplicate_with_all_provenance_in_both_orders(self):
        runs = fixture_runs(1)
        runs[-1]["source"] = "z.json"
        repeat = copy.deepcopy(runs[-1])
        repeat["source"] = "a.json"
        baseline, baseline_calls = self.evaluate(runs, 1)
        result, calls = self.evaluate([repeat, *reversed(runs)], 1)
        self.assertEqual(calls, baseline_calls)
        self.assertEqual(result["comparisons"], baseline["comparisons"])
        self.assertEqual(result, self.evaluate([*runs, repeat], 1)[0])
        record = next(entry for entry in result["selection"] if entry["variants"][0]["copies"] == 2)
        self.assertTrue(record["selected"])
        self.assertEqual(record["variants"][0]["sources"], ["a.json", "z.json"])
        self.assertEqual(len(result["selection"]), 13)

    def test_full_results_are_order_independent_at_both_depths(self):
        for depth in (1, 2):
            runs = full_runs(depth)
            expected = self.evaluate(runs, depth)
            for seed in (29, 47):
                random.Random(seed).shuffle(runs)
                self.assertEqual(self.evaluate(runs, depth), expected)

    def test_preserved_original_failure_can_coexist_with_passing_extension(self):
        def differences(coarse, fine, **options):
            if coarse["fixture"] == "conservative_impact" and not options:
                ratios = {20e-6: 0.4, 10e-6: 0.2, 5e-6: 0.19,
                          2.5e-6: 0.1, 1.25e-6: 0.05}
                return {"q_m[0]": ratios[coarse["maximum_step_s"]]}, []
            return {"q_m[0]": 0.0}, []
        original, _ = self.evaluate(full_runs(0), comparator=differences)
        self.assertEqual(original["status"], "fail")
        for depth in (1, 2):
            result, _ = self.evaluate(full_runs(depth), depth, differences)
            self.assertEqual(result["status"], "pass")
            self.assertEqual(result["extended_result"]["status"], "pass")
            frozen = result["original_frozen_result"]
            self.assertEqual({key: frozen[key] for key in CORE_RESULT_FIELDS}, original)
            self.assertEqual(frozen["status"], "fail")
            self.assertIn("not decreasing", frozen["failures"][0])
            diffs = result["comparisons"]["conservative_impact:step"]["differences_as_budget_fractions"]
            self.assertEqual(len(diffs), 3 + depth)
            self.assertEqual(diffs[:3], original["comparisons"]["conservative_impact:step"]["differences_as_budget_fractions"])

    def test_budget_checks_apply_to_both_final_pairs_and_retain_earlier_differences(self):
        def differences(coarse, fine, **options):
            step = coarse["maximum_step_s"]
            if options or coarse["fixture"] != "conservative_impact":
                return {"q_m[0]": 0.0}, []
            values = {20e-6: 8.0, 10e-6: 4.0, 5e-6: 2.0, 2.5e-6: 0.4, 1.25e-6: 0.2}
            value = values[step]
            return {"q_m[0]": value}, ["position exceeds frozen budget"] if value > 1 else []
        one, _ = self.evaluate(full_runs(1), 1, differences)
        two, _ = self.evaluate(full_runs(2), 2, differences)
        self.assertEqual(one["status"], "fail")  # Its penultimate comparison still exceeds budget.
        self.assertEqual(two["status"], "pass")
        self.assertEqual(two["original_frozen_result"]["status"], "fail")
        self.assertEqual([item["q_m[0]"] for item in two["comparisons"]["conservative_impact:step"]["differences_as_budget_fractions"]],
                         [8.0, 4.0, 2.0, 0.4, 0.2])

    def test_unchanged_point_eight_decrease_threshold_and_roundoff_rule(self):
        for last, expected in ((0.4, "pass"), (math.nextafter(0.4, math.inf), "fail")):
            def differences(coarse, fine, **options):
                if options:
                    return {"q_m[0]": 0.0}, []
                value = last if coarse["maximum_step_s"] == 2.5e-6 else 0.5
                return {"q_m[0]": value}, []
            result, _ = self.evaluate(full_runs(1), 1, differences)
            self.assertEqual(result["status"], expected)
        resolution = 128 * math.ulp(1e-3) / 1e-8
        for fraction, expected in ((resolution, "pass"), (math.nextafter(resolution, math.inf), "fail")):
            def plateau(coarse, fine, **options):
                return {"q_m[0]": 0.0 if options else fraction}, []
            result, _ = self.evaluate(full_runs(1), 1, plateau)
            self.assertEqual(result["status"], expected)
            self.assertEqual("q_m[0]" in result["comparisons"]["conservative_impact:step"]["below_resolution"],
                             expected == "pass")

    def test_cli_passes_policy_and_reports_both_results_without_numerical_oracles(self):
        with tempfile.TemporaryDirectory() as temporary:
            source = Path(temporary) / "candidates.json"
            source.write_text(json.dumps({"runs": full_runs(1)}))
            with patch.object(audit, "audit_run", return_value={"status": "pass"}), \
                    patch.object(audit, "compare_runs", return_value=({"q_m[0]": 0.0}, [])):
                status, stdout, stderr = self.cli(audit, "--candidate", str(source), "--extra-step-levels", "1")
            result = json.loads(stdout)
            self.assertEqual((status, stderr), (0, ""))
            self.assertEqual(result["refinement_policy"]["extra_step_levels"], 1)
            self.assertEqual(result["refinement"]["original_frozen_result"]["matrix"]["run_count"], 208)
            self.assertEqual(result["refinement"]["extended_result"]["matrix"]["run_count"], 338)
            with patch.object(audit, "audit_run", return_value={"status": "pass"}):
                status, stdout, _ = self.cli(audit, "--candidate", str(source))
            self.assertEqual(status, 1)
            self.assertEqual(json.loads(stdout)["refinement_policy"]["extra_step_levels"], 0)

    def test_generated_manifest_carries_declared_policy_into_audit_command(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            executable = root / "synthetic-candidate"
            executable.write_text("Synthetic test executable placeholder\n")
            def child(argv, *, stdout, stderr, **unused):
                settings = dict(zip(argv[1::2], argv[2::2]))
                run = candidate(float(settings["--max-step"]), float(settings["--event-tolerance"]),
                                float(settings["--tolerance-scale"]), settings["--fixture"])
                end = copy.deepcopy(run["samples"][0])
                end["time_s"] = 0.002
                run["samples"].append(end)
                stdout.write(json.dumps({"schema_version": 1, "model": "synthetic-contact-p1", "runs": [run]}).encode())
                return subprocess.CompletedProcess(argv, 0)
            for depth, count in ((1, 13), (2, 14)):
                output = root / str(depth)
                with patch.object(sweep.subprocess, "run", side_effect=child) as execute:
                    status, _, _ = self.cli(sweep, "--executable", str(executable), "--output-dir", str(output),
                                             "--fixture", "conservative_impact", "--extra-step-levels", str(depth))
                self.assertEqual(status, 0)
                self.assertEqual(execute.call_count, count)
                manifest = json.loads((output / "manifest.json").read_text())
                self.assertEqual(manifest["acceptance"], "not evaluated")
                self.assertEqual(manifest["audit_argv"][-2:], ["--extra-step-levels", str(depth)])
                self.assertEqual(manifest["metadata"]["refinement_policy"]["extra_step_levels"], depth)
                self.assertIn("docs/specs/CONTACT_REFINEMENT_EXTENSION_20261123.md", manifest["source_sha256"])
                self.assertEqual(len(json.loads((output / "candidates.json").read_text())["runs"]), count)


if __name__ == "__main__":
    unittest.main()
