"""Supported-impact input regressions; constructed records are not physics evidence.

The wire records and 13-slot declaration below are independent of the parser's
fixture/observation helpers. They need neither a native build nor NumPy/SciPy.
The repeated endpoint state is deliberately not a simulated trajectory: parsing
and complete slot coverage must never become numerical acceptance.
"""
from __future__ import annotations

import copy
from dataclasses import FrozenInstanceError, replace
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
    import contact_peak_schema as legacy
    import contact_supported_schema as schema


FIXTURES = ("supported_impact", "supported_impact_scaled")
SETTING_FIELDS = (
    "maximum_step_s", "event_tolerance_s", "output_step_s",
    "relative_tolerance", "absolute_tolerance", "velocity_absolute_tolerance",
    "energy_absolute_tolerance", "impulse_absolute_tolerance",
)
# Frozen experiment declaration, including its original eight slots. This is
# not obtained from schema.EXPECTED_SETTINGS or refinement_slots(). The local
# controls use the producer's documented binary-float scaling operation.
SLOT_AXES = (
    (20e-6, 2.5e-9, 1.0), (10e-6, 2.5e-9, 1.0),
    (5e-6, 2.5e-9, 1.0), (2.5e-6, 2.5e-9, 1.0),
    (2.5e-6, 10e-9, 1.0), (2.5e-6, 5e-9, 1.0),
    (2.5e-6, 2.5e-9, 0.1), (2.5e-6, 2.5e-9, 0.01),
    (1.25e-6, 2.5e-9, 1.0), (1.25e-6, 10e-9, 1.0),
    (1.25e-6, 5e-9, 1.0), (1.25e-6, 2.5e-9, 0.1),
    (1.25e-6, 2.5e-9, 0.01),
)
PEAK_UNITS = {"hammer_compression": "m", "hammer_force": "n",
              "damper_compression": "m", "damper_force": "n"}


def settings(axes=SLOT_AXES[0]):
    step, event_tolerance, scale = axes
    return dict(zip(SETTING_FIELDS, (
        step, event_tolerance, 20e-6,
        *(value * scale for value in (1e-8, 1e-12, 1e-9, 1e-13, 1e-13)),
    )))


def sample(time=0.0):
    """Published rest state: 8 g hammer at -0.2 mm, moving at 1 m/s."""
    return {
        "time_s": time, "q_m": [0.0, 0.0], "v_m_per_s": [0.0, 0.0],
        "hammer_position_m": -0.0002, "hammer_velocity_m_per_s": 1.0,
        "hammer_force_n": 0.0, "damper_force_n": 0.0,
        "hammer_gap_m": -0.0002, "damper_gap_m": 0.0,
        "hammer_relative_velocity_m_per_s": 1.0,
        "damper_relative_velocity_m_per_s": 0.0,
        "hammer_impulse_ns": 0.0, "damper_impulse_ns": 0.0,
        "modal_loss_j": 0.0, "hammer_loss_j": 0.0, "damper_loss_j": 0.0,
        "hammer_clipped_loss_j": 0.0, "damper_clipped_loss_j": 0.0,
        "actuator_work_j": 0.0, "launch_work_j": 0.0, "energy_j": 0.004,
        "modal_acceleration_m_per_s2": [0.0, 0.0],
        "hammer_acceleration_m_per_s2": 0.0,
        "instantaneous_power_residual_w": 0.0, "actuator_power_w": 0.0,
        "pickup_voltage_v": 0.0,
    }


def payload(name=FIXTURES[0], axes=SLOT_AXES[0], *, peaks=True):
    sampled = {"scope": "accepted endpoints and checked interior samples; not continuous-time extrema"}
    located = {
        "coverage": "uncertified_sign_change_search",
        "evaluation_method": "fixed_interval_anchor_replay_two_rk4_half_steps_or_certified_free_motion",
    }
    for peak, unit in PEAK_UNITS.items():
        sampled[peak + "_" + unit] = 0.0
        sampled[peak + "_time_s"] = 0.0
        located[peak] = {
            "value_" + unit: 0.0, "time_s": 0.0,
            "bracket_lo_s": 0.0, "bracket_hi_s": 0.0,
            "origin": "disabled" if peak.startswith("damper_") else "sample",
        }
    diagnostics = {
        "max_abs_cumulative_residual_j": 0.0, "sum_abs_interval_residual_j": 0.0,
        "maximum_power_residual_w": 0.0, "initial_energy_j": 0.004,
        "minimum_accepted_step_s": axes[0], "maximum_accepted_step_s": axes[0],
        "accepted_steps": 1, "rejected_steps": 0, "rhs_evaluations": 12,
        "analytic_free_evaluations": 0, "event_subdivisions": 0,
        "root_iterations": 0, "grazing_candidates": 0, "sampled_maxima": sampled,
    }
    if peaks:
        diagnostics["located_peak_candidates"] = located
    return {"schema_version": 1, "model": "synthetic-contact-p1", "runs": [{
        "fixture": name, "success": True, "failure_code": 0, **settings(axes),
        "samples": [sample(), sample(0.020)], "events": [],
        "relaunch_snapshots": [], "diagnostics": diagnostics,
    }]}


def encode(document):
    return json.dumps(document, separators=(",", ":")).encode("utf-8")


def event(**overrides):
    value = {"contact": "hammer", "kind": "onset", "time_s": 0.0,
             "bracket_lo_s": 0.0, "bracket_hi_s": 0.0,
             "gap_uncertainty_m": 0.0, "work_j": 0.0}
    value.update(overrides)
    return value


class ContactSupportedSchemaTests(unittest.TestCase):
    def parse(self, document):
        return schema.parse_candidate(encode(document))

    def reject(self, document, message=None):
        with self.assertRaises(schema.CandidateFormatError) as caught:
            self.parse(document)
        self.assertLessEqual(len(str(caught.exception)), 384)
        if message is not None:
            self.assertRegex(str(caught.exception), message)

    def matrix(self):
        return [self.parse(payload(name, axes)) for name in FIXTURES for axes in SLOT_AXES]

    def test_independent_26_slot_declaration_and_frozen_identity(self):
        records = self.matrix()
        expected = tuple(sorted(tuple(settings(axes).values()) for axes in SLOT_AXES))
        self.assertEqual(schema.SUPPORTED_FIXTURES, frozenset(FIXTURES))
        self.assertEqual(schema.RUN_SETTING_FIELDS, SETTING_FIELDS)
        self.assertEqual(schema.EXPECTED_SETTINGS, expected)
        result = schema.validate_collection(records)
        self.assertEqual(result["matrix_id"], "contact-refinement-20261123-extra-1")
        self.assertEqual(result["settings_sha256"],
                         "802f018edbad9396df0af0099cd1d0bf6b0d0a9621b0a5ae95688a283d31e17b")
        self.assertEqual((result["expected_count"], result["actual_count"],
                          result["distinct_expected_slots"]), (26, 26, 26))
        self.assertTrue(result["matrix_complete"])
        self.assertEqual(result["diagnostics"], [])
        self.assertEqual(result["numerical_acceptance"], "not_evaluated")
        self.assertEqual(result, schema.validate_collection(reversed(records)))
        self.assertEqual(json.loads(json.dumps(result)), result)

    def test_complete_coverage_keeps_failure_and_unavailable_peaks(self):
        records = self.matrix()
        failed = payload()
        failed["runs"][0].update(success=False, failure_code=21)
        failed["runs"][0]["samples"][-1]["time_s"] = 0.001
        records[0] = self.parse(failed)
        records[1] = self.parse(payload(axes=SLOT_AXES[1], peaks=False))
        result = schema.validate_collection(records)
        self.assertTrue(result["matrix_complete"])
        self.assertEqual(result["numerical_acceptance"], "not_evaluated")
        self.assertEqual(len(result["failed_runs"]), 1)
        self.assertEqual(result["failed_runs"][0]["failure_code"], 21)
        self.assertEqual(result["failed_runs"][0]["raw_sha256"], records[0].raw_sha256)
        self.assertEqual(len(result["unavailable_peaks"]), 1)
        self.assertEqual(result["unavailable_peaks"][0]["raw_sha256"], records[1].raw_sha256)
        self.assertEqual(len(result["diagnostics"]), 2)

    def test_peak_availability_is_independent_of_producer_status(self):
        for name in FIXTURES:
            for success in (False, True):
                for peaks in (False, True):
                    with self.subTest(name=name, success=success, peaks=peaks):
                        document = payload(name, peaks=peaks)
                        document["runs"][0].update(success=success, failure_code=0 if success else 9)
                        parsed = self.parse(document)
                        self.assertIs(parsed.run["success"], success)
                        self.assertEqual(parsed.availability, "available" if peaks else "not_available")
                        self.assertEqual(parsed.peaks is not None, peaks)
                        result = schema.validate_collection([parsed])
                        self.assertEqual(len(result["failed_runs"]), int(not success))
                        self.assertEqual(len(result["unavailable_peaks"]), int(not peaks))

    def test_immediate_failure_preserves_zero_work_and_absent_peaks(self):
        document = payload(peaks=False)
        run = document["runs"][0]
        run.update(success=False, failure_code=15)
        run["samples"] = run["samples"][:1]
        run["diagnostics"].update(accepted_steps=0, minimum_accepted_step_s=0,
                                  maximum_accepted_step_s=0)
        parsed = self.parse(document)
        self.assertFalse(parsed.run["success"])
        self.assertEqual(parsed.run["failure_code"], 15)
        self.assertEqual(len(parsed.run["samples"]), 1)
        self.assertEqual(parsed.availability, "not_available")
        run["diagnostics"]["maximum_accepted_step_s"] = 1e-9
        self.reject(document, "zero accepted work")

    def test_failure_status_requires_literal_types_consistent_known_codes(self):
        for success, code in ((1, 0), (True, True), (True, 0.0), (True, 1),
                              (False, 0), (False, -1), (False, 22), (False, 2**64)):
            with self.subTest(success=success, code=code):
                document = payload()
                document["runs"][0].update(success=success, failure_code=code)
                self.reject(document)

    def test_initial_physics_is_exact_for_both_coordinate_systems(self):
        for name in FIXTURES:
            for field, value in sample().items():
                for component in range(len(value)) if isinstance(value, list) else (None,):
                    with self.subTest(name=name, field=field, component=component):
                        document = payload(name)
                        initial = document["runs"][0]["samples"][0]
                        if component is None:
                            initial[field] = math.nextafter(value, math.inf)
                        else:
                            initial[field][component] = math.nextafter(value[component], math.inf)
                        self.reject(document)
            document = payload(name)
            document["runs"][0]["diagnostics"]["initial_energy_j"] = math.nextafter(0.004, math.inf)
            self.reject(document, "initial_energy_j")

    def test_exact_duration_rejects_one_ulp_short_or_long_success(self):
        for name in FIXTURES:
            for time in (math.nextafter(0.020, 0.0), math.nextafter(0.020, math.inf)):
                with self.subTest(name=name, time=time):
                    document = payload(name)
                    document["runs"][0]["samples"][-1]["time_s"] = time
                    self.reject(document, "duration")
            document = payload(name)
            document["runs"][0]["samples"].pop()
            self.reject(document, "duration")

    def test_failed_trajectories_still_obey_duration_and_time_order(self):
        for times in ((0.0, 0.0), (0.001, 0.0), (0.0, 0.0200001)):
            with self.subTest(times=times):
                document = payload()
                run = document["runs"][0]
                run.update(success=False, failure_code=9)
                run["samples"] = [sample(time) for time in times]
                self.reject(document)

    def test_each_complete_setting_is_exact_and_required(self):
        for axes in (SLOT_AXES[0], SLOT_AXES[-1]):
            for field in SETTING_FIELDS:
                for replacement in ("ulp", "missing", True, "1e-8", None, 0.0, -1.0):
                    with self.subTest(axes=axes, field=field, replacement=replacement):
                        document = payload(axes=axes)
                        run = document["runs"][0]
                        if replacement == "ulp":
                            run[field] = math.nextafter(run[field], math.inf)
                        elif replacement == "missing":
                            del run[field]
                        else:
                            run[field] = replacement
                        self.reject(document)

    def test_cross_axis_and_depth_two_settings_cannot_enter_depth_one(self):
        for axes in ((20e-6, 5e-9, 1.0), (5e-6, 2.5e-9, 0.1),
                     (1.25e-6, 5e-9, 0.1), (0.625e-6, 2.5e-9, 1.0)):
            with self.subTest(axes=axes):
                self.reject(payload(axes=axes), "frozen depth-one slot")
        for name in ("conservative_impact", "damped_impact", "moving_damper",
                     "supported_conservative", "supported_impact_scaled_scaled", "future_fixture"):
            with self.subTest(fixture=name):
                self.reject(payload(name))

    def test_action_free_and_disabled_ledgers_hold_through_final_sample(self):
        zero_fields = ("damper_force_n", "damper_impulse_ns", "damper_loss_j",
                       "damper_clipped_loss_j", "hammer_loss_j", "hammer_clipped_loss_j",
                       "actuator_work_j", "actuator_power_w", "launch_work_j")
        for name in FIXTURES:
            for field in zero_fields:
                for index in (1, 2):
                    with self.subTest(name=name, field=field, index=index):
                        document = payload(name)
                        run = document["runs"][0]
                        run["samples"].insert(1, sample(0.010))
                        run["samples"][index][field] = 1e-30
                        self.reject(document, field)

    def test_disabled_damper_preserves_signed_geometric_observations(self):
        for sign in (-1, 1):
            document = payload()
            final = document["runs"][0]["samples"][-1]
            final.update(damper_gap_m=sign * 0.001,
                         damper_relative_velocity_m_per_s=sign * 0.2)
            parsed = self.parse(document)
            self.assertEqual(parsed.run["samples"][-1]["damper_gap_m"], sign * 0.001)
            self.assertEqual(parsed.peaks["damper_force"]["origin"], "disabled")

    def test_relaunch_snapshot_is_rejected_even_when_structurally_valid(self):
        document = payload()
        document["runs"][0]["relaunch_snapshots"] = [{
            "time_s": 0.0, "requested_velocity_m_per_s": 1.0,
            "success": True, "failure_code": 0, "work_j": 0.0,
            "before": sample(), "after": sample(),
        }]
        self.reject(document, "action snapshots")

    def test_events_allow_hammer_contacts_but_reject_actions_clamps_and_work(self):
        document = payload()
        document["runs"][0]["events"] = [event(kind=kind) for kind in ("onset", "separation", "grazing")]
        self.assertEqual(len(self.parse(document).run["events"]), 3)
        for update in ({"contact": "damper"}, {"kind": "relaunch"}, {"kind": "motion_change"},
                       {"kind": "clamp_on"}, {"kind": "clamp_off"}, {"work_j": 1e-30}):
            with self.subTest(update=update):
                document["runs"][0]["events"] = [event(**update)]
                self.reject(document)

    def test_event_and_peak_brackets_respect_declared_tolerance(self):
        for target in ("event", "peak"):
            with self.subTest(target=target):
                document = payload()
                run = document["runs"][0]
                eta = run["event_tolerance_s"]
                bracket = {"time_s": eta / 2, "bracket_lo_s": 0.0, "bracket_hi_s": eta}
                if target == "event":
                    run["events"] = [event(**bracket)]
                    record = run["events"][0]
                else:
                    record = run["diagnostics"]["located_peak_candidates"]["hammer_force"]
                    record.update(origin="stationary_candidate", **bracket)
                self.parse(document)
                record["bracket_hi_s"] = eta * 1.000001
                self.reject(document, "bracket exceeds event tolerance")
                record["bracket_hi_s"] = eta / 4
                self.reject(document, "enclose")

    def test_supplied_peaks_cannot_be_malformed_or_claim_certified_coverage(self):
        for value in (None, {}, [], "not_available"):
            document = payload()
            document["runs"][0]["diagnostics"]["located_peak_candidates"] = value
            self.reject(document)
        for name in PEAK_UNITS:
            for origin in ("action_before", "action_after"):
                with self.subTest(name=name, origin=origin):
                    document = payload()
                    document["runs"][0]["diagnostics"]["located_peak_candidates"][name]["origin"] = origin
                    self.reject(document, "action absent")
        for field in ("coverage", "evaluation_method"):
            document = payload()
            document["runs"][0]["diagnostics"]["located_peak_candidates"][field] = "certified"
            self.reject(document, "metadata")
        document = payload()
        peak = document["runs"][0]["diagnostics"]["located_peak_candidates"]["hammer_force"]
        peak["value_m"] = peak.pop("value_n")
        self.reject(document, "missing or unknown")

    def test_disabled_peak_and_sampled_maxima_semantics_are_exact(self):
        for name, unit in PEAK_UNITS.items():
            document = payload()
            peak = document["runs"][0]["diagnostics"]["located_peak_candidates"][name]
            peak["origin"] = "sample" if name.startswith("damper_") else "disabled"
            self.reject(document, "disabled semantics")
            if name.startswith("damper_"):
                for field in ("value_" + unit, "time_s", "bracket_lo_s", "bracket_hi_s"):
                    document = payload()
                    document["runs"][0]["diagnostics"]["located_peak_candidates"][name][field] = 1e-30
                    self.reject(document)
                for field in (name + "_" + unit, name + "_time_s"):
                    document = payload()
                    document["runs"][0]["diagnostics"]["sampled_maxima"][field] = 1e-30
                    self.reject(document, "disabled damper sampled maxima")

    def test_unknown_and_missing_fields_at_every_supplied_object_level(self):
        paths = ((), ("runs", 0), ("runs", 0, "samples", 1),
                 ("runs", 0, "events", 0), ("runs", 0, "diagnostics"),
                 ("runs", 0, "diagnostics", "sampled_maxima"),
                 ("runs", 0, "diagnostics", "located_peak_candidates"),
                 ("runs", 0, "diagnostics", "located_peak_candidates", "hammer_force"))
        for path in paths:
            for missing in (False, True):
                with self.subTest(path=path, missing=missing):
                    document = payload()
                    document["runs"][0]["events"] = [event()]
                    target = document
                    for part in path:
                        target = target[part]
                    if missing:
                        del target[next(iter(target))]
                    else:
                        target["unrecognized_physics"] = 1
                    self.reject(document, "missing or unknown")

    def test_literal_numeric_types_and_final_state_shape(self):
        paths = (("schema_version",), ("runs", 0, "samples", 1, "energy_j"),
                 ("runs", 0, "samples", 1, "q_m", 1),
                 ("runs", 0, "diagnostics", "accepted_steps"),
                 ("runs", 0, "diagnostics", "located_peak_candidates", "hammer_force", "value_n"))
        for path in paths:
            for replacement in (True, "0", None):
                with self.subTest(path=path, replacement=replacement):
                    document = payload()
                    target = document
                    for part in path[:-1]:
                        target = target[part]
                    target[path[-1]] = replacement
                    self.reject(document)
        for field in ("q_m", "v_m_per_s", "modal_acceleration_m_per_s2"):
            for replacement in ([], [0], [0, 0, 0], {"0": 0, "1": 0}):
                document = payload()
                document["runs"][0]["samples"][-1][field] = replacement
                self.reject(document, "exactly two")

    def test_nonfinite_numbers_and_duplicate_keys_are_rejected_before_promotion(self):
        raw = encode(payload())
        mutations = [raw.replace(b'"schema_version":1', b'"schema_version":1,"schema_version":1'),
                     raw.replace(b'"success":true', b'"success":true,"success":true'),
                     raw.replace(b'"energy_j":0.004', b'"energy_j":0.004,"energy_j":0.004', 1),
                     raw.replace(b'"value_n":0.0', b'"value_n":0.0,"value_n":0.0', 1)]
        for token in (b"NaN", b"Infinity", b"-Infinity", b"1e999"):
            mutations.append(raw.replace(b'"energy_j":0.004', b'"energy_j":' + token, 1))
        for bad in mutations:
            with self.subTest(raw=bad[:100]):
                self.assertNotEqual(bad, raw)
                with self.assertRaises(schema.CandidateFormatError):
                    schema.parse_candidate(bad)

    def test_input_envelope_encoding_and_depth_are_bounded(self):
        raw = encode(payload())
        for bad in (None, raw.decode(), bytearray(raw), b"\xff", b"\xef\xbb\xbf" + raw,
                    b"", b"null", b"[]", b"{", b"[" * 65 + b"0" + b"]" * 65):
            with self.subTest(type=type(bad), prefix=str(bad)[:30]):
                with self.assertRaises(schema.CandidateFormatError) as caught:
                    schema.parse_candidate(bad)
                self.assertLessEqual(len(str(caught.exception)), 384)
        for replacement in ([], {}, [payload()["runs"][0]] * 2):
            document = payload()
            document["runs"] = replacement
            self.reject(document, "exactly one run")

    def test_missing_and_duplicate_slots_do_not_cancel_at_expected_count(self):
        records = self.matrix()
        records[-1] = records[0]
        result = schema.validate_collection(records)
        self.assertEqual((result["actual_count"], result["distinct_expected_slots"]), (26, 25))
        self.assertFalse(result["matrix_complete"])
        self.assertEqual(len(result["missing_slots"]), 1)
        self.assertEqual(len(result["duplicate_slots"]), 1)
        duplicate = result["duplicate_slots"][0]
        self.assertEqual(duplicate["count"], 2)
        self.assertTrue(duplicate["byte_identical"])
        self.assertEqual(duplicate["raw_sha256"], [records[0].raw_sha256] * 2)
        self.assertEqual(result, schema.validate_collection(reversed(records)))

    def test_distinct_raw_duplicates_retain_every_failure_and_missing_peak(self):
        document = payload(peaks=False)
        document["runs"][0].update(success=False, failure_code=9)
        raw = encode(document)
        records = [schema.parse_candidate(raw), schema.parse_candidate(raw + b"\n")]
        result = schema.validate_collection(records)
        duplicate = result["duplicate_slots"][0]
        self.assertFalse(duplicate["byte_identical"])
        self.assertEqual(duplicate["raw_sha256"], sorted(record.raw_sha256 for record in records))
        self.assertEqual(len(result["failed_runs"]), 2)
        self.assertEqual(len(result["unavailable_peaks"]), 2)
        self.assertEqual(result, schema.validate_collection(reversed(records)))

    def test_empty_collection_and_record_limit_remain_explicit(self):
        result = schema.validate_collection(iter(()))
        self.assertFalse(result["matrix_complete"])
        self.assertEqual(len(result["missing_slots"]), 26)
        self.assertEqual(result["actual_count"], 0)
        self.assertEqual((schema.MAX_FILES, schema.MAX_FILE_BYTES, schema.MAX_COLLECTION_BYTES),
                         (64, 32 * 1024**2, 128 * 1024**2))
        parsed = self.parse(payload())
        self.assertEqual(schema.validate_collection([parsed] * 64)["actual_count"], 64)

        def bounded_source():
            yield from (parsed for _ in range(65))
            self.fail("collection exhausted an iterable beyond its first over-limit record")

        with self.assertRaisesRegex(schema.CandidateFormatError, "64-file"):
            schema.validate_collection(bounded_source())

    def test_collection_rejects_unpromoted_and_inconsistent_records(self):
        parsed = self.parse(payload())
        for record in (None, {}, legacy.parse_candidate(encode(payload())),
                       replace(parsed, fixture=FIXTURES[1]),
                       replace(parsed, settings=tuple(reversed(parsed.settings))),
                       replace(parsed, availability="unsupported_fixture")):
            with self.subTest(record=type(record), availability=getattr(record, "availability", None)):
                with self.assertRaises(schema.CandidateFormatError):
                    schema.validate_collection([record])
        changed = list(parsed.settings)
        changed[0] = (changed[0][0], math.nextafter(changed[0][1], math.inf))
        with self.assertRaisesRegex(schema.CandidateFormatError, "metadata"):
            schema.validate_collection([replace(parsed, settings=tuple(changed))])

    def test_defensive_unexpected_slots_are_retained_in_deterministic_order(self):
        records = []
        for axes in ((0.625e-6, 2.5e-9, 1.0), (20e-6, 5e-9, 1.0)):
            # Only the legacy parser can produce these off-grid records. They
            # exercise collection diagnostics, not promotion by the new parser.
            records.append(legacy.parse_candidate(encode(payload(axes=axes))))
        result = schema.validate_collection(records)
        self.assertFalse(result["matrix_complete"])
        self.assertEqual(len(result["unexpected_slots"]), 2)
        self.assertEqual(len(result["missing_slots"]), 26)
        self.assertEqual(result["distinct_expected_slots"], 0)
        self.assertEqual(result, schema.validate_collection(reversed(records)))

    def test_raw_identity_and_immutability_do_not_expand_legacy_fixture_scope(self):
        document = payload()
        raw = encode(document)
        parsed = schema.parse_candidate(raw)
        self.assertEqual(parsed.raw_sha256, hashlib.sha256(raw).hexdigest())
        self.assertEqual(json.loads(json.dumps(parsed.run)), document["runs"][0])
        self.assertIs(parsed.peaks, parsed.run["diagnostics"]["located_peak_candidates"])
        self.assertEqual(legacy.SUPPORTED_FIXTURES, frozenset({
            "conservative_impact", "damped_impact", "forced_clamp", "preload_equilibrium"}))
        self.assertEqual(legacy.parse_candidate(raw).availability, "unsupported_fixture")
        with self.assertRaises(FrozenInstanceError):
            parsed.availability = "accepted"
        for mutate in (lambda: parsed.run.update(success=False),
                       lambda: parsed.run["samples"][0].clear(),
                       lambda: parsed.peaks["hammer_force"].update(value_n=1.0)):
            with self.assertRaises(TypeError):
                mutate()
        self.assertEqual(copy.deepcopy(parsed.run), parsed.run)

    def test_cold_import_and_parsing_do_not_load_optional_numerical_libraries(self):
        code = """import builtins, sys
sys.path.insert(0, sys.argv[1])
original = builtins.__import__
def guarded(name, *args, **kwargs):
    if name.split('.')[0] in {'numpy', 'scipy'}:
        raise AssertionError('optional backend imported: ' + name)
    return original(name, *args, **kwargs)
builtins.__import__ = guarded
import contact_supported_schema
record = contact_supported_schema.parse_candidate(sys.stdin.buffer.read())
assert record.availability == 'available'
result = contact_supported_schema.validate_collection([record])
assert result['numerical_acceptance'] == 'not_evaluated'
"""
        completed = subprocess.run(
            [sys.executable, "-S", "-c", code, str(ROOT / "tools")],
            input=encode(payload()), capture_output=True, timeout=10,
        )
        self.assertEqual(completed.returncode, 0, completed.stderr.decode(errors="replace"))


if __name__ == "__main__":
    unittest.main()
