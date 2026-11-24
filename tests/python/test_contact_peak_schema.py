"""Strict peak-schema regressions; constructed payloads are not physics evidence."""
from __future__ import annotations

import copy
from dataclasses import FrozenInstanceError
import hashlib
import json
import math
from pathlib import Path
import subprocess
import sys
import unittest
from unittest.mock import patch


ROOT = Path(__file__).resolve().parents[2]
with patch.object(sys, "path", [str(ROOT / "tools"), *sys.path]):
    import contact_peak_schema as schema
    from validate_contact_reference import fixtures, RUN_SETTING_FIELDS


def payload(name="conservative_impact", *, peaks=True):
    """Exact published schema, with mechanical initial state and synthetic zeros."""
    fixture = fixtures()[name]

    def sample(time):
        return {
            "time_s": time, "q_m": list(fixture.q), "v_m_per_s": list(fixture.v),
            "hammer_position_m": fixture.x, "hammer_velocity_m_per_s": fixture.vh,
            "hammer_force_n": 0.0, "damper_force_n": 0.0,
            "hammer_gap_m": 0.0, "damper_gap_m": 0.0,
            "hammer_relative_velocity_m_per_s": 0.0,
            "damper_relative_velocity_m_per_s": 0.0,
            "hammer_impulse_ns": 0.0, "damper_impulse_ns": 0.0,
            "modal_loss_j": 0.0, "hammer_loss_j": 0.0, "damper_loss_j": 0.0,
            "hammer_clipped_loss_j": 0.0, "damper_clipped_loss_j": 0.0,
            "actuator_work_j": 0.0, "launch_work_j": 0.0, "energy_j": 0.0,
            "modal_acceleration_m_per_s2": [0.0, 0.0],
            "hammer_acceleration_m_per_s2": 0.0,
            "instantaneous_power_residual_w": 0.0,
            "actuator_power_w": 0.0, "pickup_voltage_v": 0.0,
        }

    sampled = {"scope": "accepted endpoints and checked interior samples; not continuous-time extrema"}
    located = {
        "coverage": "uncertified_sign_change_search",
        "evaluation_method": "fixed_interval_anchor_replay_two_rk4_half_steps_or_certified_free_motion",
    }
    for contact in ("hammer", "damper"):
        for quantity, unit in (("compression", "m"), ("force", "n")):
            key = contact + "_" + quantity
            sampled[key + "_" + unit] = 0.0
            sampled[key + "_time_s"] = 0.0
            located[key] = {
                "value_" + unit: 0.0, "time_s": 0.0,
                "bracket_lo_s": 0.0, "bracket_hi_s": 0.0,
                "origin": "disabled" if contact == "damper" and name != "preload_equilibrium" else "sample",
            }
    diagnostics = {
        "max_abs_cumulative_residual_j": 0.0, "sum_abs_interval_residual_j": 0.0,
        "maximum_power_residual_w": 0.0, "initial_energy_j": 0.0,
        "minimum_accepted_step_s": fixture.duration,
        "maximum_accepted_step_s": fixture.duration,
        "accepted_steps": 1, "rejected_steps": 0, "rhs_evaluations": 12,
        "analytic_free_evaluations": 0, "event_subdivisions": 0,
        "root_iterations": 0, "grazing_candidates": 0, "sampled_maxima": sampled,
    }
    if peaks:
        diagnostics["located_peak_candidates"] = located
    return {"schema_version": 1, "model": "synthetic-contact-p1", "runs": [{
        "fixture": name, "success": True, "failure_code": 0,
        "maximum_step_s": 20e-6, "event_tolerance_s": 1e-8, "output_step_s": 20e-6,
        "relative_tolerance": 1e-8, "absolute_tolerance": 1e-12,
        "velocity_absolute_tolerance": 1e-9, "energy_absolute_tolerance": 1e-13,
        "impulse_absolute_tolerance": 1e-13,
        "samples": [sample(0.0), sample(fixture.duration)],
        "events": [], "relaunch_snapshots": [], "diagnostics": diagnostics,
    }]}


def encode(document):
    return json.dumps(document, separators=(",", ":")).encode("utf-8")


def event(**overrides):
    value = {"contact": "hammer", "kind": "onset", "time_s": 0.0,
             "bracket_lo_s": 0.0, "bracket_hi_s": 0.0,
             "gap_uncertainty_m": 0.0, "work_j": 0.0}
    value.update(overrides)
    return value


class ContactPeakSchemaTests(unittest.TestCase):
    def parse(self, document):
        return schema.parse_candidate(encode(document))

    def malformed(self, document):
        with self.assertRaises(schema.CandidateFormatError) as caught:
            self.parse(document)
        self.assertLessEqual(len(str(caught.exception)), 384)

    def test_all_supported_identities_and_optional_availability(self):
        for name in ("conservative_impact", "damped_impact", "forced_clamp", "preload_equilibrium"):
            for present in (False, True):
                with self.subTest(fixture=name, peaks=present):
                    parsed = self.parse(payload(name, peaks=present))
                    self.assertEqual(parsed.fixture, name)
                    self.assertEqual(parsed.availability, "available" if present else "not_available")
                    self.assertEqual(parsed.peaks is not None, present)
                    self.assertEqual(len(parsed.run["samples"][0]), 26)

    def test_hash_settings_normalization_and_immutability(self):
        document = payload()
        raw = encode(document)
        parsed = schema.parse_candidate(raw)
        self.assertEqual(parsed.raw_sha256, hashlib.sha256(raw).hexdigest())
        self.assertEqual(tuple(name for name, value in parsed.settings), RUN_SETTING_FIELDS)
        self.assertTrue(all(type(value) is float for name, value in parsed.settings))
        self.assertEqual(json.loads(json.dumps(parsed.run)), document["runs"][0])
        self.assertIs(parsed.peaks, parsed.run["diagnostics"]["located_peak_candidates"])
        with self.assertRaises(FrozenInstanceError):
            parsed.fixture = "different"
        for operation in (
            lambda: parsed.run.__setitem__("success", False),
            lambda: parsed.run["samples"][0].clear(),
            lambda: parsed.peaks["hammer_force"].update(value_n=10),
            lambda: parsed.run["samples"].__setitem__(0, {}),
        ):
            with self.assertRaises((TypeError, AttributeError)):
                operation()
        self.assertEqual(copy.deepcopy(parsed.run), parsed.run)

    def test_import_needs_no_optional_backend(self):
        code = """import builtins, sys
sys.path.insert(0, sys.argv[1])
original = builtins.__import__
def guarded(name, *args, **kwargs):
    if name.split('.')[0] in {'numpy', 'scipy'}:
        raise AssertionError('optional backend imported')
    return original(name, *args, **kwargs)
builtins.__import__ = guarded
import contact_peak_schema
assert callable(contact_peak_schema.parse_candidate)
"""
        completed = subprocess.run([sys.executable, "-S", "-c", code, str(ROOT / "tools")],
                                   capture_output=True, text=True, timeout=10)
        self.assertEqual(completed.returncode, 0, completed.stderr)

    def test_initial_state_uses_independent_values_and_zero_exactness(self):
        for name in ("conservative_impact", "damped_impact", "forced_clamp", "preload_equilibrium"):
            for field in ("q_m", "v_m_per_s", "hammer_position_m", "hammer_velocity_m_per_s"):
                document = payload(name)
                initial = document["runs"][0]["samples"][0]
                if isinstance(initial[field], list):
                    initial[field][0] += 1e-6
                else:
                    initial[field] += 1e-6
                with self.subTest(fixture=name, field=field):
                    self.malformed(document)
        document = payload()
        document["runs"][0]["samples"][0]["q_m"][1] = math.ulp(0.0)
        self.malformed(document)

    def test_nonzero_initial_state_accepts_exact_64_ulp_boundary(self):
        document = payload("preload_equilibrium")
        initial = document["runs"][0]["samples"][0]
        expected = initial["q_m"][0]
        initial["q_m"][0] = expected + 64 * math.ulp(expected)
        self.parse(document)
        initial["q_m"][0] = expected + 65 * math.ulp(expected)
        self.malformed(document)

    def test_successful_truncation_rejected_but_partial_failure_preserved(self):
        document = payload()
        run = document["runs"][0]
        run["samples"][-1]["time_s"] = 0.001
        self.malformed(document)
        run.update(success=False, failure_code=9)
        parsed = self.parse(document)
        self.assertFalse(parsed.run["success"])
        self.assertEqual(parsed.run["failure_code"], 9)
        self.assertEqual(parsed.run["samples"][-1]["time_s"], 0.001)
        self.assertEqual(parsed.availability, "available")

    def test_immediate_failure_keeps_zero_step_diagnostics(self):
        document = payload()
        run = document["runs"][0]
        run.update(success=False, failure_code=15)
        run["samples"] = run["samples"][:1]
        run["diagnostics"].update(accepted_steps=0, minimum_accepted_step_s=0,
                                  maximum_accepted_step_s=0)
        self.assertEqual(len(self.parse(document).run["samples"]), 1)
        run["diagnostics"]["maximum_accepted_step_s"] = 1e-6
        self.malformed(document)

    def test_status_fields_and_counters_reject_ambiguous_types(self):
        for field, value in (("success", 1), ("success", False),
                             ("failure_code", True), ("failure_code", 1),
                             ("failure_code", -1), ("failure_code", 0.0)):
            document = payload()
            document["runs"][0][field] = value
            with self.subTest(field=field, value=value):
                self.malformed(document)
        for value in (True, -1, 0.5):
            document = payload()
            document["runs"][0]["diagnostics"]["accepted_steps"] = value
            self.malformed(document)

    def test_diagnostic_step_bounds_and_integer_limit(self):
        for updates in ({"minimum_accepted_step_s": 0},
                        {"minimum_accepted_step_s": 0.01},
                        {"accepted_steps": 0},
                        {"accepted_steps": 0, "analytic_free_evaluations": 9,
                         "minimum_accepted_step_s": 0, "maximum_accepted_step_s": 0}):
            document = payload()
            document["runs"][0]["diagnostics"].update(updates)
            self.malformed(document)
        document = payload()
        document["runs"][0]["diagnostics"]["rhs_evaluations"] = 2**64 - 1
        self.parse(document)
        document["runs"][0]["diagnostics"]["rhs_evaluations"] = 2**64
        self.malformed(document)

    def test_settings_are_complete_positive_finite_numbers(self):
        for field in RUN_SETTING_FIELDS:
            for value in (0, -1, True, "1e-8", None, float("inf")):
                document = payload()
                document["runs"][0][field] = value
                with self.subTest(field=field, value=value):
                    self.malformed(document)
            document = payload()
            del document["runs"][0][field]
            self.malformed(document)

    def test_unknown_and_missing_nested_fields_are_rejected(self):
        paths = [(), ("runs", 0), ("runs", 0, "samples", 0),
                 ("runs", 0, "diagnostics"),
                 ("runs", 0, "diagnostics", "sampled_maxima"),
                 ("runs", 0, "diagnostics", "located_peak_candidates"),
                 ("runs", 0, "diagnostics", "located_peak_candidates", "hammer_force")]
        for path in paths:
            for missing in (False, True):
                document = payload()
                target = document
                for part in path:
                    target = target[part]
                if missing:
                    del target[next(iter(target))]
                else:
                    target["unexpected"] = 0
                with self.subTest(path=path, missing=missing):
                    self.malformed(document)

    def test_present_null_partial_and_wrong_unit_peak_records_fail(self):
        for replacement in (None, {}, [], "not_available"):
            document = payload()
            document["runs"][0]["diagnostics"]["located_peak_candidates"] = replacement
            self.malformed(document)
        document = payload()
        peak = document["runs"][0]["diagnostics"]["located_peak_candidates"]["hammer_force"]
        peak["value_m"] = peak.pop("value_n")
        self.malformed(document)

    def test_literal_metadata_is_required(self):
        for target, field in (("sampled_maxima", "scope"),
                              ("located_peak_candidates", "coverage"),
                              ("located_peak_candidates", "evaluation_method")):
            document = payload()
            document["runs"][0]["diagnostics"][target][field] = "certified"
            self.malformed(document)

    def test_sample_order_shape_finiteness_and_duration(self):
        for edit in ("duplicate", "reversed", "nonzero_start", "over_duration",
                     "short_vector", "long_vector", "bool", "string", "nonfinite"):
            document = payload()
            run = document["runs"][0]
            sample = run["samples"][-1]
            if edit == "duplicate": sample["time_s"] = 0
            elif edit == "reversed": run["samples"].reverse()
            elif edit == "nonzero_start": run["samples"][0]["time_s"] = 1e-9
            elif edit == "over_duration": sample["time_s"] += 2e-12
            elif edit == "short_vector": sample["q_m"] = [0]
            elif edit == "long_vector": sample["v_m_per_s"] = [0, 0, 0]
            elif edit == "bool": sample["energy_j"] = True
            elif edit == "string": sample["pickup_voltage_v"] = "0"
            else: sample["hammer_acceleration_m_per_s2"] = float("nan")
            with self.subTest(edit=edit):
                self.malformed(document)

    def test_peak_brackets_and_localization_width(self):
        document = payload()
        peak = document["runs"][0]["diagnostics"]["located_peak_candidates"]["hammer_force"]
        peak.update(origin="stationary_candidate", time_s=0.5e-8,
                    bracket_lo_s=0, bracket_hi_s=1e-8)
        self.parse(document)
        peak["bracket_hi_s"] = 1.00001e-8
        self.malformed(document)
        for updates in ({"origin": "sample", "bracket_hi_s": 1e-8},
                        {"time_s": -1e-8}, {"time_s": 0.003},
                        {"bracket_lo_s": 0.0001}, {"bracket_hi_s": 0},
                        {"value_n": -1}, {"origin": "action_before"}):
            document = payload()
            candidate = document["runs"][0]["diagnostics"]["located_peak_candidates"]["hammer_force"]
            candidate.update(time_s=0.0005, bracket_lo_s=0.0005, bracket_hi_s=0.0005)
            candidate.update(updates)
            with self.subTest(updates=updates):
                self.malformed(document)

    def test_disabled_and_enabled_zero_records_remain_distinct(self):
        document = payload("forced_clamp")
        parsed = self.parse(document)
        self.assertEqual(parsed.peaks["hammer_force"]["origin"], "sample")
        self.assertEqual(parsed.peaks["damper_force"]["origin"], "disabled")
        for field, value in (("origin", "sample"), ("value_n", 1e-15),
                             ("time_s", 1e-9), ("bracket_hi_s", 1e-9)):
            document = payload()
            document["runs"][0]["diagnostics"]["located_peak_candidates"]["damper_force"][field] = value
            self.malformed(document)
        for name, contact in (("forced_clamp", "hammer"), ("preload_equilibrium", "damper")):
            document = payload(name)
            document["runs"][0]["diagnostics"]["located_peak_candidates"][contact + "_force"]["origin"] = "disabled"
            self.malformed(document)

    def test_sampled_maxima_fields_and_disabled_semantics(self):
        for field, value in (("hammer_force_n", -1), ("hammer_force_time_s", 0.003),
                             ("damper_compression_m", 1e-20),
                             ("damper_force_time_s", 1e-20)):
            document = payload()
            document["runs"][0]["diagnostics"]["sampled_maxima"][field] = value
            self.malformed(document)

    def test_events_validate_exact_shape_enums_and_chronology(self):
        document = payload()
        document["runs"][0]["events"] = [event()]
        self.parse(document)
        for updates in ({"contact": "other"}, {"kind": "relaunch"},
                        {"time_s": -1}, {"time_s": 0.003},
                        {"bracket_hi_s": -1}, {"gap_uncertainty_m": -1},
                        {"work_j": False}, {"unexpected": 0}):
            document["runs"][0]["events"] = [event(**updates)]
            with self.subTest(updates=updates):
                self.malformed(document)
        document["runs"][0]["events"] = [
            event(time_s=0.001, bracket_lo_s=0.001, bracket_hi_s=0.001), event()]
        self.malformed(document)

    def test_unsupported_actions_are_preserved_after_structural_validation(self):
        document = payload()
        run = document["runs"][0]
        run["fixture"] = "future_action_fixture"
        sample = copy.deepcopy(run["samples"][0])
        run["relaunch_snapshots"] = [{
            "time_s": 0, "requested_velocity_m_per_s": 1, "success": True,
            "failure_code": 0, "work_j": 0, "before": sample, "after": copy.deepcopy(sample)}]
        run["events"] = [event(kind="relaunch")]
        run["diagnostics"]["located_peak_candidates"]["hammer_force"]["origin"] = "action_after"
        parsed = self.parse(document)
        self.assertEqual(parsed.availability, "unsupported_fixture")
        self.assertEqual(len(parsed.run["relaunch_snapshots"]), 1)
        self.assertEqual(parsed.peaks["hammer_force"]["origin"], "action_after")
        malformed = copy.deepcopy(document)
        malformed["runs"][0]["relaunch_snapshots"][0]["after"]["energy_j"] = True
        self.malformed(malformed)
        malformed = copy.deepcopy(document)
        malformed["runs"][0]["relaunch_snapshots"][0]["before"]["time_s"] = 1e-6
        self.malformed(malformed)
        malformed = copy.deepcopy(document)
        malformed["runs"][0]["events"][0]["kind"] = "arbitrary"
        self.malformed(malformed)
        run["fixture"] = "conservative_impact"
        self.malformed(document)

    def test_duplicate_utf8_depth_nonfinite_and_integer_lexemes_are_bounded(self):
        raw = encode(payload())
        bad_inputs = [
            b"\xff", b"\xef\xbb\xbf" + raw, b"", b"null", b"[]", b"{",
            raw.replace(b'"schema_version":1', b'"schema_version":1,"schema_version":1'),
            raw.replace(b'"value_n":0.0', b'"value_n":0.0,"value_n":0.0', 1),
            raw.replace(b'"hammer_force_n":0.0', b'"hammer_force_n":1e999', 1),
            raw.replace(b'"hammer_force_n":0.0', b'"hammer_force_n":NaN', 1),
            raw.replace(b'"hammer_force_n":0.0', b'"hammer_force_n":-Infinity', 1),
            raw.replace(b'"hammer_force_n":0.0', b'"hammer_force_n":-18446744073709551616', 1),
            b"[" * 65 + b"0" + b"]" * 65,
            raw.replace(b'"schema_version":1', b'"schema_version":' + b"9" * 1000),
        ]
        for candidate in bad_inputs:
            with self.subTest(prefix=candidate[:55]):
                with self.assertRaises(schema.CandidateFormatError) as caught:
                    schema.parse_candidate(candidate)
                self.assertLessEqual(len(str(caught.exception)), 384)
        document = payload()
        document["runs"][0]["fixture"] = '["\\' * 100
        self.assertEqual(self.parse(document).availability, "unsupported_fixture")

    def test_envelope_types_and_number_of_runs(self):
        for field, replacement in (("schema_version", True), ("schema_version", 1.0),
                                   ("model", "other"), ("runs", []),
                                   ("runs", [payload()["runs"][0]] * 2),
                                   ("runs", {})):
            document = payload()
            document[field] = replacement
            self.malformed(document)

    def test_size_and_row_caps_are_checked_at_boundaries(self):
        self.assertEqual((schema.MAX_FILE_BYTES, schema.MAX_COLLECTION_BYTES, schema.MAX_FILES),
                         (32 * 1024**2, 128 * 1024**2, 64))
        self.assertEqual((schema.MAX_SAMPLES, schema.MAX_EVENTS, schema.MAX_DEPTH), (30000, 100000, 64))
        raw = encode(payload())
        with patch.object(schema, "MAX_FILE_BYTES", len(raw)):
            schema.parse_candidate(raw)
            with self.assertRaises(schema.CandidateFormatError):
                schema.parse_candidate(raw + b" ")
        with patch.object(schema, "MAX_SAMPLES", 1):
            self.malformed(payload())
        document = payload()
        document["runs"][0]["events"] = [event(), event()]
        with patch.object(schema, "MAX_EVENTS", 1):
            self.malformed(document)
        for value in (bytearray(raw), raw.decode(), None):
            with self.assertRaises(schema.CandidateFormatError):
                schema.parse_candidate(value)


if __name__ == "__main__":
    unittest.main()
