"""Metadata validation tests use invented test-only records, never hardware evidence."""
from __future__ import annotations

import copy
import importlib.util
from pathlib import Path
import shutil
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
SPEC = importlib.util.spec_from_file_location("rhodes_data_validator", ROOT / "tools/validate_data.py")
assert SPEC is not None and SPEC.loader is not None
data = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(data)


class DataContractsTests(unittest.TestCase):
    def setUp(self):
        self.profile = data.strict_load(ROOT / "profiles/mark-i-stage-73/specimen.json")
        self.session = data.strict_load(ROOT / "data/examples/synthetic-session.json")
        self.inventory = data.strict_load(ROOT / "profiles/mark-i-stage-73/components.json")
        self.sources = {s["id"] for s in data.strict_load(ROOT / "docs/research/sources.json")["sources"]}
        self.tasks = {t["id"] for t in data.strict_load(ROOT / "project/state.json")["tasks"]}

    def measured_pair(self):
        """Structurally complete, explicitly test-only fixture for validator testing."""
        profile = copy.deepcopy(self.profile)
        profile.update(identity_status="identified", serial_number="TEST-ONLY-NOT-HARDWARE",
                       action_revision="test-only", tine_revision="test-only",
                       pickup_revision="test-only", wiring_verified=True,
                       instrument_evidence=["test-only-identity-record"],
                       load={"resistance_ohm":1000000, "capacitance_farad":100e-12})
        session = copy.deepcopy(self.session)
        session.update(session_id="test-only-metadata-session", kind="measured", split="fit",
                       condition_id="test-only-condition", acquired_at="2026-01-01T00:00:00Z",
                       operator="test-only-fixture", temperature_celsius=20.0,
                       pickup_geometry_m={"gap":0.002,"vertical_offset":0.0},
                       calibration=[{"id":"test-cal", "method":"test only", "reference":"test-only-record",
                                     "uncertainty":0.001, "unit":"V"}],
                       artifacts=[{"path":"data/raw/test-only.wav", "sha256":"a"*64, "bytes":64}],
                       rights={"status":"owned", "permission_reference":"test-only-rights"})
        session["excitation"].update(type="native_hammer", hammer_velocity_m_per_s=0.1)
        session["channels"][0]["calibration_id"] = "test-cal"
        return profile, session

    def validate_session(self, session, profile=None):
        profile = self.profile if profile is None else profile
        data.validate_session(session, {profile["profile_id"]:profile})

    def validate_inventory(self, inventory, profile=None, sessions=None):
        profile = self.profile if profile is None else profile
        data.validate_components(inventory, {profile["profile_id"]:profile}, self.sources,
                                 self.tasks, {} if sessions is None else sessions)

    def test_current_repository_contracts(self):
        profiles, sessions = data.validate_repository(ROOT)
        self.assertGreaterEqual(profiles, 1)
        self.assertGreaterEqual(sessions, 1)

    def test_structurally_complete_test_only_measurement(self):
        profile, session = self.measured_pair()
        data.validate_profile(profile, self.sources)
        self.validate_session(session, profile)

    def test_synthetic_relabel_does_not_supply_identity(self):
        _, session = self.measured_pair()
        with self.assertRaisesRegex(data.DataError, "identified specimen"):
            self.validate_session(session)

    def test_synthetic_cannot_claim_acquisition_or_validation(self):
        for field, value in (("split", "validation"), ("acquired_at", "2026-01-01T00:00:00Z"),
                             ("operator", "purported operator")):
            with self.subTest(field=field):
                session = copy.deepcopy(self.session)
                session[field] = value
                with self.assertRaises(data.DataError):
                    self.validate_session(session)

    def test_synthetic_calibration_claim_rejected(self):
        _, calibrated = self.measured_pair()
        self.session["calibration"] = calibrated["calibration"]
        self.session["channels"] = calibrated["channels"]
        with self.assertRaisesRegex(data.DataError, "cannot claim acquisition calibration"):
            self.validate_session(self.session)

    def test_profile_target_and_boolean_fields(self):
        for field, value in (("key_count", 88), ("midi_low", 40), ("midi_high", 112),
                             ("midi_low", True), ("wiring_verified", 0),
                             ("model_family", "janus"), ("output_boundary", "cabinet")):
            with self.subTest(field=field, value=value):
                profile = copy.deepcopy(self.profile)
                profile[field] = value
                with self.assertRaises(data.DataError):
                    data.validate_profile(profile, self.sources)

    def test_unidentified_profile_cannot_claim_wiring_verified(self):
        self.profile["wiring_verified"] = True
        with self.assertRaisesRegex(data.DataError, "unidentified profile"):
            data.validate_profile(self.profile, self.sources)

    def test_profile_source_and_identity_evidence(self):
        bad_source = copy.deepcopy(self.profile)
        bad_source["key_mapping_source"] = "MISSING"
        with self.assertRaisesRegex(data.DataError, "unknown source"):
            data.validate_profile(bad_source, self.sources)
        profile, _ = self.measured_pair()
        profile["instrument_evidence"] = []
        with self.assertRaisesRegex(data.DataError, "missing identity"):
            data.validate_profile(profile, self.sources)

    def test_json_duplicate_nonfinite_and_overflow(self):
        with tempfile.TemporaryDirectory() as temporary:
            path = Path(temporary) / "bad.json"
            for content in ('{"x":1,"x":2}', '{"x":NaN}', '{"x":Infinity}',
                            '{"nested":{"x":1e999}}'):
                with self.subTest(content=content):
                    path.write_text(content, encoding="utf-8")
                    with self.assertRaises(data.DataError):
                        data.strict_load(path)

    def test_nonfinite_and_boolean_numbers_rejected(self):
        for value in (float("nan"), float("inf"), True, 10**1000):
            with self.subTest(value=str(value)[:30]):
                session = copy.deepcopy(self.session)
                session["sample_rate_hz"] = value
                with self.assertRaises(data.DataError):
                    self.validate_session(session)

    def test_unsafe_or_noncanonical_artifact_paths(self):
        for path in ("../secret", "/etc/passwd", "C:/private", "data\\raw.wav", "a/../b",
                     "./raw.wav", ".", "a//b", "raw/", ".git/config", "a\x00b", "a\nb"):
            with self.subTest(path=path):
                with self.assertRaises(data.DataError):
                    data.safe_relative(path)
        data.safe_relative("a")
        data.safe_relative("data/raw/pickup.wav")

    def test_missing_acquisition_fields_and_timezone(self):
        for field, value in (("artifacts", []), ("calibration", []), ("pickup_geometry_m", None),
                             ("operator", ""), ("acquired_at", "2026-01-01T00:00:00"),
                             ("acquired_at", "2026-02-31T00:00:00Z"), ("temperature_celsius", None)):
            with self.subTest(field=field, value=value):
                profile, session = self.measured_pair()
                session[field] = value
                with self.assertRaises(data.DataError):
                    self.validate_session(session, profile)

    def test_calibration_reference_and_unit_consistency(self):
        for mutate in (lambda s: s["channels"][0].update(calibration_id="missing"),
                       lambda s: s["calibration"][0].update(unit="m"),
                       lambda s: s["calibration"].append(copy.deepcopy(s["calibration"][0])),
                       lambda s: s["channels"].append(copy.deepcopy(s["channels"][0]))):
            profile, session = self.measured_pair()
            mutate(session)
            with self.assertRaises(data.DataError):
                self.validate_session(session, profile)

    def test_missing_rights_and_hammer_velocity(self):
        for mutate in (lambda s: s["rights"].update(permission_reference=None),
                       lambda s: s["rights"].update(status="synthetic_original"),
                       lambda s: s["excitation"].update(hammer_velocity_m_per_s=None),
                       lambda s: s["excitation"].update(hammer_velocity_m_per_s=0)):
            profile, session = self.measured_pair()
            mutate(session)
            with self.assertRaises(data.DataError):
                self.validate_session(session, profile)

    def test_artifact_metadata_must_be_valid_and_unique(self):
        for mutate in (lambda s: s["artifacts"][0].update(sha256="not-a-hash"),
                       lambda s: s["artifacts"][0].update(bytes=True),
                       lambda s: s["artifacts"].append(copy.deepcopy(s["artifacts"][0]))):
            profile, session = self.measured_pair()
            mutate(session)
            with self.assertRaises(data.DataError):
                self.validate_session(session, profile)

    def test_condition_and_identical_artifact_split_leakage(self):
        _, fit = self.measured_pair()
        heldout = copy.deepcopy(fit)
        heldout.update(session_id="heldout-test-only", split="validation")
        heldout["artifacts"][0]["sha256"] = "b"*64
        with self.assertRaisesRegex(data.DataError, "condition leaks"):
            data.validate_splits([fit, heldout])
        heldout["condition_id"] = "different-condition"
        heldout["artifacts"][0]["sha256"] = "a"*64
        with self.assertRaisesRegex(data.DataError, "identical artifact"):
            data.validate_splits([fit, heldout])
        heldout["artifacts"][0]["sha256"] = "b"*64
        data.validate_splits([fit, heldout])

    def test_component_unknown_values_stay_unknown(self):
        for field, value in (("value", 1.0), ("uncertainty", 0.1), ("measurement_ids", ["test-session"])):
            with self.subTest(field=field):
                inventory = copy.deepcopy(self.inventory)
                inventory["components"][0][field] = value
                with self.assertRaises(data.DataError):
                    self.validate_inventory(inventory)

    def test_component_references_and_identity_are_checked(self):
        for field, value in (("source_ids", ["MISSING"]), ("task_id", "NOPE-001"),
                             ("unit", "furlongs"), ("implementation_status", "hardware_validated")):
            with self.subTest(field=field):
                inventory = copy.deepcopy(self.inventory)
                inventory["components"][0][field] = value
                with self.assertRaises(data.DataError):
                    self.validate_inventory(inventory)
        self.inventory["components"].append(copy.deepcopy(self.inventory["components"][0]))
        with self.assertRaisesRegex(data.DataError, "duplicate component"):
            self.validate_inventory(self.inventory)

    def test_component_scalar_requires_exact_unit(self):
        component = self.inventory["components"][0]
        component.update(provenance="synthetic", value=1.0, unit="mixed_SI")
        with self.assertRaisesRegex(data.DataError, "mixed_SI"):
            self.validate_inventory(self.inventory)

    def test_measured_component_cannot_use_synthetic_or_heldout_data(self):
        profile, session = self.measured_pair()
        component = self.inventory["components"][0]
        component.update(provenance="measured", value=0.01, uncertainty=0.001,
                         measurement_ids=[session["session_id"]])
        sessions = {session["session_id"]:session}
        self.validate_inventory(self.inventory, profile, sessions)
        session["split"] = "validation"
        with self.assertRaisesRegex(data.DataError, "held-out"):
            self.validate_inventory(self.inventory, profile, sessions)
        session["split"] = "fit"
        session["kind"] = "synthetic"
        with self.assertRaisesRegex(data.DataError, "same specimen"):
            self.validate_inventory(self.inventory, profile, sessions)

    def test_measured_component_missing_uncertainty_or_foreign_specimen(self):
        profile, session = self.measured_pair()
        component = self.inventory["components"][0]
        component.update(provenance="fitted", value=0.01, measurement_ids=[session["session_id"]])
        sessions = {session["session_id"]:session}
        with self.assertRaisesRegex(data.DataError, "uncertainty"):
            self.validate_inventory(self.inventory, profile, sessions)
        component["uncertainty"] = 0.001
        session["specimen_profile"] = "foreign-specimen"
        with self.assertRaisesRegex(data.DataError, "same specimen"):
            self.validate_inventory(self.inventory, profile, sessions)

    def test_repository_rejects_missing_component_inventory(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            for name in ("profiles", "data", "project", "docs/research"):
                shutil.copytree(ROOT/name, root/name)
            (root / "profiles/mark-i-stage-73/components.json").unlink()
            with self.assertRaisesRegex(data.DataError, "requires a component inventory"):
                data.validate_repository(root)

    def test_schema_documents_have_exact_field_contracts(self):
        for filename, fixture in (("specimen.schema.json", self.profile),
                                  ("measurement-session.schema.json", self.session),
                                  ("components.schema.json", self.inventory)):
            with self.subTest(filename=filename):
                schema = data.strict_load(ROOT / "schemas" / filename)
                self.assertEqual(schema["$schema"], "https://json-schema.org/draft/2020-12/schema")
                self.assertEqual(set(schema["required"]), set(fixture))
                self.assertEqual(set(schema["properties"]), set(fixture))
                self.assertFalse(schema["additionalProperties"])


if __name__ == "__main__":
    unittest.main()
