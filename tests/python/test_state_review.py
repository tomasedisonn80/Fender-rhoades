"""Focused A007 lease, recovery, and evidence-durability regressions."""
from __future__ import annotations

from contextlib import redirect_stderr, redirect_stdout
from datetime import datetime, timedelta, timezone
import io
import json
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

from test_project import project, write_measurement_fixture


class StateReviewTests(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory()
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name)
        (self.root / "project").mkdir()
        (self.root / "docs").mkdir()
        self.state = {
            "schema_version": 1, "project": "Review fixture", "goal": "Check transitions",
            "target": "Temporary synthetic fixture", "fidelity_status": "unvalidated",
            "revision": 0, "updated_at": "2020-01-01T00:00:00Z", "history": [],
            "tasks": [{
                "id": "FND-001", "title": "Fixture task", "milestone": "Fixture",
                "status": "todo", "priority": 0, "dependencies": [],
                "acceptance": ["Fixture only"], "required_evidence": ["documentary"],
                "evidence": [], "owner": None, "claim": None, "blockers": [], "paths": [],
            }],
        }
        self.registry = {"schema_version": 1, "records": []}
        self.save()

    def save(self):
        (self.root / "project/state.json").write_text(project.encode(self.state), encoding="utf-8")
        (self.root / "project/evidence.json").write_text(project.encode(self.registry), encoding="utf-8")

    def cli(self, *arguments):
        output, errors = io.StringIO(), io.StringIO()
        with redirect_stdout(output), redirect_stderr(errors):
            status = project.main(["--root", str(self.root), *arguments])
        return status, output.getvalue(), errors.getvalue()

    def claim(self):
        status, output, error = self.cli("claim", "FND-001", "--owner", "worker")
        self.assertEqual((status, error), (0, ""))
        return json.loads(output)["token"]

    def add_record(self, identifier, kind, **fields):
        artifact = self.root / f"docs/{identifier}.txt"
        artifact.write_text("Synthetic audit fixture; no hardware observation.\n", encoding="utf-8")
        record = {
            "id": identifier, "kind": kind, "result": "pass", "summary": "Synthetic audit fixture",
            "command": None, "created_at": "2020-01-01T00:00:00Z", "source_commit": None,
            "artifacts": [{"path": f"docs/{identifier}.txt", "sha256": project.sha256(artifact)}],
            "specimen_id": None, "profile_path": None, "session_paths": [],
            "independent": False, "reference_evidence": [],
            "limitations": ["Only a structural test fixture; not evidence of an instrument"],
        }
        record.update(fields)
        self.registry["records"].append(record)

    def test_expiry_boundary_is_exclusive(self):
        task = {"claim": {"expires_at": "2026-01-01T00:00:00Z"}}
        expiry = datetime(2026, 1, 1, tzinfo=timezone.utc)
        self.assertTrue(project.active_claim(task, expiry - timedelta(microseconds=1)))
        self.assertFalse(project.active_claim(task, expiry))
        self.assertFalse(project.active_claim(task, expiry + timedelta(microseconds=1)))

    def test_wrong_owner_or_token_cannot_mutate_active_claim(self):
        token = self.claim()
        path = self.root / "project/state.json"
        original = path.read_bytes()
        actions = {
            "complete": ("--evidence", "absent"), "release": ("--reason", "handoff"),
            "block": ("--reason", "blocked"), "renew": ("--minutes", "1"),
        }
        for action, arguments in actions.items():
            for owner, supplied_token in (("other", token), ("worker", "wrong-token")):
                with self.subTest(action=action, owner=owner, token=supplied_token):
                    status, _, error = self.cli(action, "FND-001", "--owner", owner,
                                                "--token", supplied_token, *arguments)
                    self.assertEqual(status, 2)
                    self.assertIn("owner/token mismatch", error)
                    self.assertEqual(path.read_bytes(), original)

    def test_reclaimed_lease_rejects_previous_token_even_for_same_owner(self):
        old_token = self.claim()
        self.state, self.registry = project.read_project(self.root)
        self.state["tasks"][0]["claim"].update(
            claimed_at="2020-01-01T00:00:00Z", expires_at="2020-01-01T00:01:00Z")
        self.save()
        new_token = self.claim()
        self.assertNotEqual(old_token, new_token)
        self.assertEqual(self.cli("release", "FND-001", "--owner", "worker", "--token", old_token,
                                  "--reason", "Old worker handoff")[0], 2)
        self.assertEqual(self.cli("release", "FND-001", "--owner", "worker", "--token", new_token,
                                  "--reason", "Current worker handoff")[0], 0)

    def test_sync_failure_preserves_committed_state_and_allows_recovery(self):
        self.assertEqual(self.cli("sync")[0], 0)
        with patch.object(project, "sync", side_effect=OSError("Injected document write failure")):
            self.assertEqual(self.cli("claim", "FND-001", "--owner", "worker")[0], 2)
        state, _ = project.read_project(self.root)
        self.assertEqual(state["revision"], 1)
        self.assertIsNotNone(state["tasks"][0]["claim"])
        self.assertFalse((self.root / "project/.project.lock").exists())
        original = (self.root / "project/state.json").read_bytes()
        self.assertEqual(self.cli("sync", "--check")[0], 2)
        self.assertEqual(self.cli("validate")[0], 0)
        self.assertEqual(self.cli("sync")[0], 0)
        self.assertEqual(self.cli("sync", "--check")[0], 0)
        self.assertEqual((self.root / "project/state.json").read_bytes(), original)

    def test_failed_atomic_replacement_preserves_state_and_removes_temporary_file(self):
        original = (self.root / "project/state.json").read_bytes()
        with patch.object(project.os, "replace", side_effect=OSError("Injected rename failure")):
            self.assertEqual(self.cli("claim", "FND-001", "--owner", "worker")[0], 2)
        self.assertEqual((self.root / "project/state.json").read_bytes(), original)
        self.assertEqual(list((self.root / "project").glob(".state.json.*")), [])
        self.assertFalse((self.root / "project/.project.lock").exists())

    def test_existing_lock_is_not_stolen(self):
        lock = self.root / "project/.project.lock"
        lock.mkdir()
        owner = lock / "owner.json"
        owner.write_text('{"pid": 123, "created_at": "2020-01-01T00:00:00Z"}\n', encoding="utf-8")
        original = owner.read_bytes()
        with patch.object(project.time, "monotonic", side_effect=[0.0, 3.0]):
            status, _, error = self.cli("validate")
        self.assertEqual(status, 2)
        self.assertIn("state is locked", error)
        self.assertEqual(owner.read_bytes(), original)

    def test_reopen_fidelity_gate_clears_claim_and_preserves_evidence(self):
        for identifier, fidelity in (("VAL-001", "measured_one_note"), ("KEY-001", "measured_keyboard")):
            with self.subTest(identifier=identifier):
                self.state.update(revision=0, history=[], fidelity_status=fidelity,
                                  updated_at="2020-01-01T00:00:00Z")
                task = self.state["tasks"][0]
                task.update(id=identifier, status="done", required_evidence=["hardware_validation"],
                            evidence=["heldout"], claim=None, owner="reviewer")
                self.registry["records"] = []
                binding = write_measurement_fixture(self.root)
                self.add_record("measurement", "measurement", **binding)
                self.add_record("heldout", "hardware_validation", **binding,
                                independent=True, reference_evidence=["measurement"])
                self.save()
                original_evidence = (self.root / "project/evidence.json").read_bytes()
                self.assertEqual(self.cli("reopen", identifier, "--owner", "reviewer",
                                          "--reason", "Fixture regression")[0], 0)
                state, _ = project.read_project(self.root)
                self.assertEqual(state["fidelity_status"], "unvalidated")
                self.assertEqual(state["tasks"][0]["status"], "todo")
                self.assertIsNone(state["tasks"][0]["owner"])
                self.assertEqual(state["tasks"][0]["evidence"], ["heldout"])
                self.assertEqual((self.root / "project/evidence.json").read_bytes(), original_evidence)

    def assert_mutable_artifact_rejected(self, artifact):
        self.assertEqual(self.cli("sync")[0], 0)
        original = (self.root / "project/evidence.json").read_bytes()
        status, _, _ = self.cli("evidence-add", "mutable", "--kind", "documentary", "--result", "pass",
                                "--summary", "Temporary fixture", "--artifact", artifact)
        self.assertEqual(status, 2, f"Tool-owned mutable artifact must be rejected: {artifact}")
        self.assertEqual((self.root / "project/evidence.json").read_bytes(), original)
        self.assertEqual(self.cli("validate")[0], 0)

    def test_evidence_registry_cannot_be_its_own_artifact(self):
        self.assert_mutable_artifact_rejected("project/evidence.json")

    def test_canonical_state_cannot_be_an_immutable_artifact(self):
        self.assert_mutable_artifact_rejected("project/state.json")

    def test_generated_state_cannot_be_an_immutable_artifact(self):
        self.assert_mutable_artifact_rejected("docs/PROJECT_STATE.md")

    def test_transient_lock_owner_cannot_be_an_immutable_artifact(self):
        self.assert_mutable_artifact_rejected("project/.project.lock/owner.json")

    def test_symlink_alias_of_canonical_state_cannot_be_an_artifact(self):
        try:
            (self.root / "docs/state-alias.json").symlink_to(self.root / "project/state.json")
        except (OSError, NotImplementedError):
            self.skipTest("Host does not permit symlinks")
        self.assert_mutable_artifact_rejected("docs/state-alias.json")

    def test_non_ascii_token_is_rejected_without_traceback(self):
        self.claim()
        original = (self.root / "project/state.json").read_bytes()
        status, _, error = self.cli("release", "FND-001", "--owner", "worker", "--token", "é",
                                    "--reason", "Malformed-token fixture")
        self.assertEqual(status, 2)
        self.assertIn("owner/token mismatch", error)
        self.assertEqual((self.root / "project/state.json").read_bytes(), original)


if __name__ == "__main__":
    unittest.main()
