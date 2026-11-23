"""A041/A044 cross-document regressions; all acquisitions here are invented fixtures."""
from __future__ import annotations

from contextlib import redirect_stderr, redirect_stdout
from copy import deepcopy
import io
import json
from pathlib import Path
import re
import tempfile
import unittest

from test_project import ROOT, project, write_measurement_fixture


class EvidenceBindingTests(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory()
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name)
        (self.root / "project").mkdir()
        self.binding = write_measurement_fixture(self.root)
        self.state = {
            "schema_version": 1, "project": "Binding test", "goal": "Check declared metadata",
            "target": "Invented metadata fixture only", "fidelity_status": "unvalidated",
            "revision": 0, "updated_at": "2026-01-01T00:00:00Z", "history": [],
            "tasks": [{"id": "FND-001", "title": "Fixture", "milestone": "Test", "status": "todo",
                       "priority": 0, "dependencies": [], "acceptance": ["Test only"],
                       "required_evidence": ["documentary"], "evidence": [], "owner": None,
                       "claim": None, "blockers": [], "paths": []}],
        }
        self.registry = {"schema_version": 1, "records": []}
        self.save()

    def save(self):
        for path, value in (("project/state.json", self.state), ("project/evidence.json", self.registry)):
            (self.root / path).write_text(project.encode(value), encoding="utf-8")

    def cli(self, *arguments):
        output, errors = io.StringIO(), io.StringIO()
        with redirect_stdout(output), redirect_stderr(errors):
            status = project.main(["--root", str(self.root), *arguments])
        return status, output.getvalue(), errors.getvalue()

    def record(self, identifier="measurement", kind="measurement", binding=None, **overrides):
        path = f"docs/{identifier}.txt"
        (self.root / path).write_text("Invented test-only metadata, not a hardware result.\n", encoding="utf-8")
        record = {"id": identifier, "kind": kind, "result": "pass", "summary": "Invented metadata fixture",
                  "command": None, "created_at": "2026-01-01T00:00:00Z", "source_commit": None,
                  "artifacts": [], "specimen_id": None, "profile_path": None, "session_paths": [],
                  "independent": False, "reference_evidence": [],
                  "limitations": ["No physical specimen or acquisition; validator exercise only"]}
        if kind in project.SPECIMEN_KINDS:
            record.update(deepcopy(self.binding if binding is None else binding))
        if kind == "specimen_identity":
            record["session_paths"] = []
            record["artifacts"] = [item for item in record["artifacts"] if item["path"] == record["profile_path"]]
        if kind == "hardware_validation":
            record.update(independent=True, reference_evidence=["measurement"])
        record["artifacts"].append({"path": path, "sha256": project.sha256(self.root / path)})
        record.update(overrides)
        self.registry["records"].append(record)
        return record

    def rewrite(self, path, value):
        """Adjust an isolated fixture and its hashes to exercise semantic checks."""
        (self.root / path).write_text(project.encode(value), encoding="utf-8")
        for record in [self.binding, *self.registry["records"]]:
            for artifact in record["artifacts"]:
                if artifact["path"] == path:
                    artifact["sha256"] = project.sha256(self.root / path)

    def validate(self):
        project.validate(self.root, self.state, self.registry)

    def assert_invalid(self, message):
        with self.assertRaisesRegex(project.StateError, message):
            self.validate()

    def test_a041_unchanged_synthetic_manifest_rejected_for_observations(self):
        synthetic = project.data_contracts.strict_load(ROOT / "data/examples/synthetic-session.json")
        self.binding = write_measurement_fixture(self.root, specimen_id=synthetic["specimen_profile"])
        self.rewrite(self.binding["session_paths"][0], synthetic)
        profile = project.data_contracts.strict_load(self.root / self.binding["profile_path"])
        # The metadata contract permits a synthetic development session. The
        # evidence contract must not classify that same manifest as acquisition.
        project.data_contracts.validate_session(synthetic, {profile["profile_id"]: profile})
        self.record()
        self.record("heldout", "hardware_validation")
        self.assert_invalid("must declare kind measured")
        # Exercise the hardware record's own link check even when its referenced
        # measurement has a different, contract-valid acquired-session fixture.
        self.registry["records"] = []
        measured = write_measurement_fixture(self.root, specimen_id=synthetic["specimen_profile"],
                                             session_id="test-only-acquired")
        self.record(binding=measured)
        self.record("heldout", "hardware_validation")
        self.assert_invalid("must declare kind measured")

    def test_unidentified_profile_rejected_for_all_specimen_evidence_kinds(self):
        profile = project.data_contracts.strict_load(ROOT / "profiles/mark-i-stage-73/specimen.json")
        profile["profile_id"] = self.binding["specimen_id"]
        self.rewrite(self.binding["profile_path"], profile)
        for kind in sorted(project.SPECIMEN_KINDS):
            with self.subTest(kind=kind):
                self.registry["records"] = []
                if kind == "hardware_validation":
                    self.record("supporting", "documentary")
                self.record("candidate", kind,
                            reference_evidence=["supporting"] if kind == "hardware_validation" else [])
                self.assert_invalid("requires an identified specimen")

    def test_specimen_id_must_match_profile_id(self):
        self.record(specimen_id="ANOTHER-TEST-ONLY-SPECIMEN")
        self.assert_invalid("specimen_id must match")

    def test_session_must_reference_the_linked_profile(self):
        self.record()
        path = self.binding["session_paths"][0]
        session = project.data_contracts.strict_load(self.root / path)
        session["specimen_profile"] = "ANOTHER-TEST-ONLY-SPECIMEN"
        self.rewrite(path, session)
        self.assert_invalid("unknown specimen profile")

    def test_typed_files_must_be_in_the_immutable_artifact_list(self):
        for path in (self.binding["profile_path"], *self.binding["session_paths"]):
            with self.subTest(path=path):
                self.registry["records"] = []
                record = self.record()
                record["artifacts"] = [item for item in record["artifacts"] if item["path"] != path]
                self.assert_invalid("typed linked file must also be an immutable artifact")

    def test_profile_and_acquisition_links_are_required(self):
        for field, value, error in (("profile_path", None, "profile_path"),
                                    ("session_paths", [], "requires session_paths")):
            with self.subTest(field=field):
                self.registry["records"] = []
                self.record(**{field: value})
                self.assert_invalid(error)

    def test_identity_can_complete_its_own_kind_without_acquisition(self):
        identity = self.record("identity", "specimen_identity", source_commit="0" * 40)
        self.state["tasks"][0].update(status="done", required_evidence=["specimen_identity"], evidence=["identity"])
        self.validate()
        self.assertEqual(identity["session_paths"], [])
        self.assertEqual(self.state["fidelity_status"], "unvalidated")
        # Git object existence remains outside this validator's declared scope.
        self.assertFalse((self.root / ".git").exists())
        identity["session_paths"] = list(self.binding["session_paths"])
        identity["artifacts"].append(deepcopy(self.binding["artifacts"][1]))
        self.assert_invalid("without acquisition sessions")

    def test_hardware_validation_cannot_use_fit_sessions(self):
        self.binding = write_measurement_fixture(self.root, split="fit")
        self.record()
        self.validate()
        self.record("heldout", "hardware_validation")
        self.assert_invalid("requires validation/test sessions")

    def test_valid_holdout_chain_requires_measurement_coverage_for_every_session(self):
        second = write_measurement_fixture(self.root, session_id="test-only-final", split="test", raw_hash="b" * 64)
        self.record()
        self.record("measurement-2", binding=second)
        combined = deepcopy(self.binding)
        combined["session_paths"] += second["session_paths"]
        combined["artifacts"] += [second["artifacts"][1]]
        heldout = self.record("heldout", "hardware_validation", binding=combined,
                              reference_evidence=["measurement", "measurement-2"])
        self.validate()
        heldout["reference_evidence"] = ["measurement"]
        self.assert_invalid("covering every observed session")

    def test_failed_measurement_cannot_cover_hardware_validation(self):
        self.record(result="fail")
        self.record("heldout", "hardware_validation")
        self.assert_invalid("requires passing measurement references")

    def test_hardware_references_require_the_same_profile_snapshot(self):
        self.record()
        other = deepcopy(self.binding)
        other["profile_path"] = "profiles/test-only-revised-profile.json"
        profile = project.data_contracts.strict_load(self.root / self.binding["profile_path"])
        profile["notes"] = "Different immutable test-only profile version."
        self.rewrite(other["profile_path"], profile)
        other["artifacts"][0] = {"path": other["profile_path"], "sha256": project.sha256(self.root / other["profile_path"])}
        self.record("heldout", "hardware_validation", binding=other)
        self.assert_invalid("same specimen and profile")

    def test_linked_documents_still_need_identity_and_calibration_contracts(self):
        profile_path, session_path = self.binding["profile_path"], self.binding["session_paths"][0]
        for path, field, value, message in ((profile_path, "instrument_evidence", [], "missing identity"),
                                            (session_path, "calibration", [], "unknown calibration")):
            with self.subTest(field=field):
                self.registry["records"] = []
                original = project.data_contracts.strict_load(self.root / path)
                changed = deepcopy(original)
                changed[field] = value
                self.rewrite(path, changed)
                self.record()
                self.assert_invalid(message)
                self.rewrite(path, original)

    def test_split_leakage_is_checked_across_distinct_evidence_records(self):
        self.record()
        second = write_measurement_fixture(self.root, session_id="test-only-fit", split="fit")
        self.record("fit", binding=second)
        self.assert_invalid("identical artifact is relabeled")
        session_path = second["session_paths"][0]
        session = project.data_contracts.strict_load(self.root / session_path)
        session["artifacts"][0]["sha256"] = "b" * 64
        session["condition_id"] = "test-only-heldout-condition"
        self.rewrite(session_path, session)
        self.assert_invalid("condition leaks")

    def test_sources_are_loaded_strictly_and_profile_sources_must_resolve(self):
        self.record()
        source_path = "docs/research/sources.json"
        sources = project.data_contracts.strict_load(self.root / source_path)
        duplicate = deepcopy(sources)
        duplicate["sources"].append(deepcopy(duplicate["sources"][0]))
        self.rewrite(source_path, duplicate)
        self.assert_invalid("duplicate source ID")
        self.rewrite(source_path, sources)
        profile_path = self.binding["profile_path"]
        profile = project.data_contracts.strict_load(self.root / profile_path)
        profile["key_mapping_source"] = "ABSENT-SOURCE"
        self.rewrite(profile_path, profile)
        self.assert_invalid("unknown source")
        (self.root / source_path).write_text('{"sources": [], "sources": []}', encoding="utf-8")
        self.assert_invalid("duplicate JSON key")

    def test_cli_persists_profile_and_repeatable_session_paths(self):
        second = write_measurement_fixture(self.root, session_id="test-only-final", split="test", raw_hash="b" * 64)
        paths = [self.binding["profile_path"], *self.binding["session_paths"], *second["session_paths"]]
        arguments = ["evidence-add", "measured", "--kind", "measurement", "--result", "pass",
                     "--summary", "Invented test-only metadata", "--specimen", self.binding["specimen_id"],
                     "--profile", paths[0]]
        for path in paths:
            arguments += ["--artifact", path]
        for path in paths[1:]:
            arguments += ["--session", path]
        status, _, error = self.cli(*arguments)
        self.assertEqual((status, error), (0, ""))
        state, registry = project.read_project(self.root)
        self.assertEqual(registry["records"][0]["profile_path"], paths[0])
        self.assertEqual(registry["records"][0]["session_paths"], paths[1:])
        self.assertEqual(state["revision"], 0)

    def test_no_arbitrary_json_artifact_guessing_or_canonical_state_reload(self):
        path = "docs/not-a-metadata-contract.json"
        (self.root / path).write_text("This is an opaque report, not JSON.", encoding="utf-8")
        self.record("report", "documentary", artifacts=[{"path": path, "sha256": project.sha256(self.root / path)}])
        self.record()
        (self.root / "project/state.json").unlink()
        self.validate()

    def test_schema_identifiers_digests_and_timestamps_reject_trailing_newline(self):
        schema = json.loads((ROOT / "schemas/project-state.schema.json").read_text())
        definitions = schema["$defs"]
        evidence = definitions["evidence_record"]["properties"]
        stamp = "2026-01-01T00:00:00Z"
        examples = [
            (schema["properties"]["updated_at"], stamp),
            (definitions["claim"]["properties"]["claimed_at"], stamp),
            (definitions["claim"]["properties"]["expires_at"], stamp),
            (definitions["task"]["properties"]["id"], "FND-001"),
            (definitions["task"]["properties"]["dependencies"]["items"], "FND-001"),
            (definitions["event"]["properties"]["task"], "FND-001"),
            (definitions["event"]["properties"]["at"], stamp),
            (evidence["id"], "test-only-evidence"),
            (evidence["created_at"], stamp),
            (evidence["source_commit"]["anyOf"][0], "a" * 40),
            (evidence["source_commit"]["anyOf"][0], "a" * 64),
            (evidence["artifacts"]["items"]["properties"]["sha256"], "a" * 64),
        ]
        # JSON Schema applies search semantics, so '$' alone accepts a final LF.
        for field, value in examples:
            with self.subTest(pattern=field["pattern"], value=value):
                self.assertIsNotNone(re.search(field["pattern"], value))
                self.assertIsNone(re.search(field["pattern"], value + "\n"))


if __name__ == "__main__":
    unittest.main()
