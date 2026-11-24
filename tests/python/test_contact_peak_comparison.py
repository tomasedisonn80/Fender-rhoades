"""Comparator contract tests using synthetic schema examples, not physics evidence.

The injected references are deliberately simple values assembled in this file.
They test comparison policy independently of the analytic oracle calculations.
"""
from __future__ import annotations

import hashlib
import math
from pathlib import Path
import sys
import unittest
from unittest.mock import patch


ROOT = Path(__file__).resolve().parents[2]
with patch.object(sys, "path", [str(ROOT / "tools"), str(ROOT / "tests/python"), *sys.path]):
    from contact_peak_schema import parse_candidate
    from test_contact_peak_schema import encode, payload
    import validate_contact_peaks as comparison


PEAK_NAMES = ("hammer_compression", "hammer_force", "damper_compression", "damper_force")


def example_parameters(name):
    """Fixed examples and their frozen value budgets; no implementation helper."""
    return (0.001, 1.1e-7, 1e-4) if name.endswith("compression") else (1.0, 3e-4, 2e-4)


def value_key(name):
    return "value_m" if name.endswith("compression") else "value_n"


def set_time(peak, time):
    peak.update(time_s=time, bracket_lo_s=time, bracket_hi_s=time)


def example(name="preload_equilibrium"):
    """Return a valid native envelope and a separate, synthetic reference mapping."""
    document = payload(name)
    run = document["runs"][0]
    duration = run["samples"][-1]["time_s"]
    reference = {
        "fixture": name, "duration_s": duration,
        "definition": {"synthetic_schema_example": True},
        "definition_sha256": hashlib.sha256(b'{"synthetic_schema_example":true}').hexdigest(),
        "reference_settings": {"damped_time_requested": False}, "peaks": {},
    }
    for peak_name in PEAK_NAMES:
        candidate = run["diagnostics"]["located_peak_candidates"][peak_name]
        disabled = candidate["origin"] == "disabled"
        value = 0.0 if disabled else example_parameters(peak_name)[0]
        time = 0.0 if disabled else duration / 2
        candidate[value_key(peak_name)] = value
        set_time(candidate, time)
        reference["peaks"][peak_name] = {
            "kind": "disabled" if disabled else "isolated",
            "unit": "m" if peak_name.endswith("compression") else "N",
            "value_interval": [value, value], "value_status": "analytic",
            "maximizer_intervals_s": [[0.0, duration]] if disabled else [[time, time]],
            "time_status": "disabled" if disabled else "analytic",
            "coverage": "synthetic_comparator_example", "details": {},
        }
    return document, reference


class ContactPeakComparisonTests(unittest.TestCase):
    def compare(self, document, reference):
        result = comparison.compare_candidate(parse_candidate(encode(document)), reference)
        self.assertEqual(result["phy002_acceptance"], "incomplete")
        return result

    def test_complete_result_preserves_identities_and_separate_coverage(self):
        document, reference = example()
        result = self.compare(document, reference)
        self.assertEqual(result["exit_code"], 0)
        self.assertEqual(result["validity"], "pass")
        self.assertEqual(result["availability"], "available")
        self.assertEqual(result["candidate_sha256"], hashlib.sha256(encode(document)).hexdigest())
        self.assertEqual(result["definition_sha256"], reference["definition_sha256"])
        self.assertEqual(result["reference_settings"], reference["reference_settings"])
        for name in PEAK_NAMES:
            with self.subTest(peak=name):
                row = result["peaks"][name]
                self.assertEqual((row["value_status"], row["time_status"],
                                  row["observation_consistency_status"]), ("pass", "pass", "pass"))
                self.assertEqual(row["candidate"], document["runs"][0]["diagnostics"]["located_peak_candidates"][name])
                self.assertEqual(row["reference"], reference["peaks"][name])
                self.assertEqual(row["coverage"], {
                    "candidate": "uncertified_sign_change_search",
                    "reference": "synthetic_comparator_example",
                    "general_continuous_extrema": "incomplete"})

    def test_value_interval_pass_fail_and_inconclusive_for_every_observable(self):
        for name in PEAK_NAMES:
            center, frozen_budget, relative = example_parameters(name)
            for status, radius, offset, exit_code in (
                ("pass", 0.25, 0.0, 0),
                ("fail", 0.25, 4.0, 1),
                ("inconclusive_reference", 2.0, 0.0, 2),
            ):
                with self.subTest(peak=name, status=status):
                    document, reference = example()
                    half_width = radius * frozen_budget
                    reference["peaks"][name].update(kind="enclosure", value_status="verified_enclosure",
                        value_interval=[center - half_width, center + half_width])
                    document["runs"][0]["diagnostics"]["located_peak_candidates"][name][value_key(name)] += offset * frozen_budget
                    result = self.compare(document, reference)
                    row = result["peaks"][name]
                    self.assertEqual(row["value_status"], status)
                    self.assertEqual(row["time_status"], "pass")
                    self.assertEqual(row["observation_consistency_status"], "pass")
                    self.assertEqual(result["exit_code"], exit_code)
                    bounds = row["value_comparison"]
                    for field, expected in (
                        ("sufficient_pass_budget", frozen_budget - relative * half_width),
                        ("definite_failure_budget", frozen_budget + relative * half_width),
                    ):
                        self.assertAlmostEqual(bounds[field], expected, delta=4 * math.ulp(expected))
                    if status == "inconclusive_reference":
                        self.assertEqual(bounds["distance_to_interval"], 0.0)
                        self.assertGreater(bounds["maximum_endpoint_error"], bounds["sufficient_pass_budget"])

    def test_zero_reference_accepts_exact_budget_and_rejects_next_float(self):
        for name in PEAK_NAMES:
            absolute_budget = 1e-8 if name.endswith("compression") else 1e-4
            for candidate_value, expected in ((absolute_budget, "pass"),
                    (math.nextafter(absolute_budget, math.inf), "fail")):
                with self.subTest(peak=name, expected=expected):
                    document, reference = example()
                    reference["peaks"][name]["value_interval"] = [0.0, 0.0]
                    document["runs"][0]["diagnostics"]["located_peak_candidates"][name][value_key(name)] = candidate_value
                    result = self.compare(document, reference)
                    self.assertEqual(result["peaks"][name]["value_status"], expected)
                    self.assertEqual(result["exit_code"], 0 if expected == "pass" else 1)

    def test_isolated_time_intervals_have_three_outcomes_for_every_observable(self):
        for name in PEAK_NAMES:
            for status, radius, offset, exit_code in (
                ("pass", 0.25, 0.0, 0),
                ("fail", 0.25, 4.0, 1),
                ("inconclusive_reference", 2.0, 0.0, 2),
            ):
                with self.subTest(peak=name, status=status):
                    document, reference = example()
                    peak = document["runs"][0]["diagnostics"]["located_peak_candidates"][name]
                    center = peak["time_s"]
                    time_budget = 1e-7 + 1e-4 * center
                    reference["peaks"][name]["maximizer_intervals_s"] = [
                        [center - radius * time_budget, center + radius * time_budget]]
                    set_time(peak, center + offset * time_budget)
                    result = self.compare(document, reference)
                    row = result["peaks"][name]
                    self.assertEqual(row["value_status"], "pass")
                    self.assertEqual(row["time_status"], status)
                    self.assertEqual(row["observation_consistency_status"], "pass")
                    self.assertTrue(row["time_comparison_required"])
                    self.assertEqual(result["exit_code"], exit_code)

    def test_observations_check_exported_and_native_maxima_with_frozen_budget(self):
        for name in PEAK_NAMES:
            contact, quantity = name.split("_")
            center, frozen_budget, _ = example_parameters(name)
            for source in ("exported", "native_sampled"):
                for offset, expected in ((0.5, "pass"), (4.0, "fail")):
                    with self.subTest(peak=name, source=source, expected=expected):
                        document, reference = example()
                        run = document["runs"][0]
                        witness = center + offset * frozen_budget
                        if source == "exported":
                            field = contact + ("_gap_m" if quantity == "compression" else "_force_n")
                            run["samples"][-1][field] = witness
                        else:
                            field = name + ("_m" if quantity == "compression" else "_n")
                            run["diagnostics"]["sampled_maxima"][field] = witness
                        result = self.compare(document, reference)
                        row = result["peaks"][name]
                        self.assertEqual(row["value_status"], "pass")
                        self.assertEqual(row["time_status"], "pass")
                        self.assertEqual(row["observation_consistency_status"], expected)
                        self.assertEqual(result["exit_code"], 0 if expected == "pass" else 1)
                        retained = "maximum_exported_observation" if source == "exported" else "maximum_native_sampled_diagnostic"
                        self.assertEqual(row["observation_comparison"][retained], witness)

    def test_negative_exported_force_fails_even_without_a_peak_deficit(self):
        for contact in ("hammer", "damper"):
            with self.subTest(contact=contact):
                document, reference = example()
                document["runs"][0]["samples"][-1][contact + "_force_n"] = -1e-12
                result = self.compare(document, reference)
                row = result["peaks"][contact + "_force"]
                self.assertEqual(row["value_status"], "pass")
                self.assertEqual(row["time_status"], "pass")
                self.assertEqual(row["observation_consistency_status"], "fail")
                self.assertEqual(row["observation_comparison"]["peak_deficit"], 0.0)
                self.assertTrue(row["observation_comparison"]["negative_exported_force"])
                self.assertEqual(result["exit_code"], 1)

    def test_disabled_and_enabled_zero_plateaus_keep_distinct_semantics(self):
        document, reference = example("forced_clamp")
        run = document["runs"][0]
        force = run["diagnostics"]["located_peak_candidates"]["hammer_force"]
        force["value_n"] = 0.0
        set_time(force, reference["duration_s"])
        reference["peaks"]["hammer_force"].update(kind="plateau", value_interval=[0.0, 0.0],
            maximizer_intervals_s=[[0.0, reference["duration_s"]]])
        run["samples"][-1]["damper_gap_m"] = 0.001
        result = self.compare(document, reference)
        self.assertEqual(result["exit_code"], 0)
        enabled = result["peaks"]["hammer_force"]
        disabled = result["peaks"]["damper_compression"]
        self.assertEqual((enabled["candidate"]["origin"], enabled["reference"]["kind"]), ("sample", "plateau"))
        self.assertEqual((disabled["candidate"]["origin"], disabled["reference"]["kind"]), ("disabled", "disabled"))
        self.assertEqual(disabled["observation_comparison"]["maximum_exported_observation"], 0.0)
        self.assertEqual(enabled["time_status"], "pass")

        document, reference = example()
        document["runs"][0]["diagnostics"]["located_peak_candidates"]["hammer_compression"]["value_m"] = 0.0
        reference["peaks"]["hammer_compression"].update(kind="plateau", value_interval=[0.0, 0.0],
            maximizer_intervals_s=[[0.0, reference["duration_s"]]])
        document["runs"][0]["samples"][-1]["hammer_gap_m"] = 0.001
        result = self.compare(document, reference)
        self.assertEqual(result["peaks"]["hammer_compression"]["observation_consistency_status"], "fail")
        self.assertEqual(result["exit_code"], 1)

    def test_plateau_membership_does_not_borrow_the_isolated_time_tolerance(self):
        document, reference = example("forced_clamp")
        peak = document["runs"][0]["diagnostics"]["located_peak_candidates"]["hammer_force"]
        peak["value_n"] = 0.0
        boundary = peak["time_s"]
        reference["peaks"]["hammer_force"].update(kind="plateau", value_interval=[0.0, 0.0],
            maximizer_intervals_s=[[0.0, boundary]])
        self.assertEqual(self.compare(document, reference)["exit_code"], 0)
        set_time(peak, math.nextafter(boundary, math.inf))
        result = self.compare(document, reference)
        row = result["peaks"]["hammer_force"]
        self.assertEqual(row["value_status"], "pass")
        self.assertEqual(row["time_status"], "fail")
        self.assertGreater(row["time_comparison"]["distance_to_maximizers_s"], 0.0)
        self.assertEqual(result["exit_code"], 1)

    def test_default_damped_and_preload_incomplete_times_do_not_block_valid_values(self):
        for fixture, contact, time_status, value_status in (
            ("damped_impact", "hammer", "not_available", "resolved_under_declared_refinement"),
            ("preload_equilibrium", "damper", "unresolved_near_equilibrium", "verified_enclosure"),
        ):
            with self.subTest(fixture=fixture):
                document, reference = example(fixture)
                for quantity in ("compression", "force"):
                    reference["peaks"][contact + "_" + quantity].update(
                        value_status=value_status, time_status=time_status, maximizer_intervals_s=[],
                        kind="enclosure" if fixture == "preload_equilibrium" else "isolated")
                result = self.compare(document, reference)
                self.assertEqual(result["exit_code"], 0)
                for quantity in ("compression", "force"):
                    row = result["peaks"][contact + "_" + quantity]
                    self.assertEqual(row["value_status"], "pass")
                    self.assertEqual(row["time_status"], time_status)
                    self.assertFalse(row["time_comparison_required"])

    def test_requested_damped_time_missing_or_unresolved_is_exit_two(self):
        for time_status in ("not_available", "inconclusive_reference"):
            with self.subTest(time_status=time_status):
                document, reference = example("damped_impact")
                reference["reference_settings"]["damped_time_requested"] = True
                for name in ("hammer_compression", "hammer_force"):
                    reference["peaks"][name].update(time_status=time_status, maximizer_intervals_s=[])
                result = self.compare(document, reference)
                self.assertEqual(result["exit_code"], 2)
                for name in ("hammer_compression", "hammer_force"):
                    row = result["peaks"][name]
                    self.assertEqual(row["value_status"], "pass")
                    self.assertEqual(row["time_status"], time_status)
                    self.assertEqual(row["observation_consistency_status"], "pass")
                    self.assertTrue(row["time_comparison_required"])

    def test_independent_time_may_lie_outside_candidate_replay_bracket(self):
        for name in PEAK_NAMES:
            with self.subTest(peak=name):
                document, reference = example()
                run = document["runs"][0]
                peak = run["diagnostics"]["located_peak_candidates"][name]
                time, tolerance = peak["time_s"], run["event_tolerance_s"]
                peak.update(origin="stationary_candidate", bracket_lo_s=time - tolerance / 4,
                            bracket_hi_s=time + tolerance / 4)
                independent_time = time + 2 * tolerance
                reference["peaks"][name]["maximizer_intervals_s"] = [[independent_time, independent_time]]
                self.assertGreater(independent_time, peak["bracket_hi_s"])
                result = self.compare(document, reference)
                self.assertEqual(result["peaks"][name]["time_status"], "pass")
                self.assertEqual(result["exit_code"], 0)

    def test_definite_failure_precedes_unresolved_value_and_other_peak_timing(self):
        document, reference = example("damped_impact")
        reference["reference_settings"]["damped_time_requested"] = True
        for name in ("hammer_compression", "hammer_force"):
            reference["peaks"][name].update(time_status="not_available", maximizer_intervals_s=[])
        reference["peaks"]["hammer_force"].update(value_status="inconclusive_reference", value_interval=None)
        document["runs"][0]["samples"][-1]["hammer_force_n"] = 2.0
        result = self.compare(document, reference)
        self.assertEqual(result["peaks"]["hammer_compression"]["exit_code"], 2)
        failed = result["peaks"]["hammer_force"]
        self.assertEqual(failed["value_status"], "inconclusive_reference")
        self.assertEqual(failed["time_status"], "not_available")
        self.assertEqual(failed["observation_consistency_status"], "fail")
        self.assertEqual(failed["exit_code"], 1)
        self.assertEqual(result["exit_code"], 1)

    def test_missing_unsupported_and_absent_or_mismatched_reference_are_incomplete(self):
        for availability in ("not_available", "unsupported_fixture"):
            with self.subTest(availability=availability):
                document = payload(peaks=availability != "not_available")
                if availability == "unsupported_fixture":
                    document["runs"][0]["fixture"] = "synthetic_outside_four_fixture_scope"
                result = self.compare(document, None)
                self.assertEqual(result["availability"], availability)
                self.assertEqual(result["exit_code"], 2)
                self.assertEqual(result["peaks"], {})
        document, reference = example()
        reference["fixture"] = "conservative_impact"
        for missing_reference in (None, reference):
            with self.subTest(reference=missing_reference):
                result = self.compare(document, missing_reference)
                self.assertEqual(result["validity"], "inconclusive_reference")
                self.assertEqual(result["exit_code"], 2)
                self.assertEqual(result["peaks"], {})

    def test_producer_failure_precedes_available_missing_or_unsupported_evidence(self):
        for availability in ("available", "not_available", "unsupported_fixture"):
            with self.subTest(availability=availability):
                document = payload(peaks=availability != "not_available")
                document["runs"][0].update(success=False, failure_code=9)
                if availability == "unsupported_fixture":
                    document["runs"][0]["fixture"] = "synthetic_outside_four_fixture_scope"
                result = self.compare(document, None)
                self.assertEqual(result["availability"], availability)
                self.assertEqual(result["validity"], "producer_failure")
                self.assertEqual(result["failure_code"], 9)
                self.assertEqual(result["exit_code"], 1)
                self.assertEqual(result["peaks"], {})

    def test_collection_exit_precedence_keeps_operational_failure_highest(self):
        for codes, expected in (((0,), 0), ((0, 2), 2), ((0, 2, 1), 1), ((0, 2, 1, 3), 3)):
            with self.subTest(codes=codes):
                self.assertEqual(comparison.aggregate_exit(codes), expected)
                self.assertEqual(comparison.aggregate_exit(reversed(codes)), expected)


if __name__ == "__main__":
    unittest.main()
