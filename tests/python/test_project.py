"""Adversarial state transitions and independent governance invariants."""
from __future__ import annotations

from contextlib import redirect_stdout, redirect_stderr
from copy import deepcopy
import importlib.util
import io
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
SPEC = importlib.util.spec_from_file_location("rhodes_project", ROOT / "tools/project.py")
assert SPEC and SPEC.loader
project = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(project)


class ProjectTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        (self.root / "project").mkdir()
        (self.root / "docs").mkdir()
        seed = json.loads((ROOT / "project/state.json").read_text())
        self.state = deepcopy(seed)
        self.state.update(revision=0, history=[], fidelity_status="unvalidated", updated_at="2026-01-01T00:00:00Z")
        for task in self.state["tasks"]:
            task.update(status="todo", claim=None, owner=None, evidence=[], blockers=[])
        self.registry = {"schema_version": 1, "records": []}
        self.save()

    def save(self):
        (self.root / "project/state.json").write_text(project.encode(self.state))
        (self.root / "project/evidence.json").write_text(project.encode(self.registry))

    def reload(self):
        self.state, self.registry = project.read_project(self.root)

    def cli(self, *args):
        output, errors = io.StringIO(), io.StringIO()
        with redirect_stdout(output), redirect_stderr(errors):
            status = project.main(["--root", str(self.root), *args])
        return status, output.getvalue(), errors.getvalue()

    def assertValid(self):
        project.validate(self.root, self.state, self.registry)

    def record(self, identifier="check-1", kind="numerical", **overrides):
        path = self.root / f"docs/{identifier}.txt"
        path.write_text("Observed test output, fixture only.\n")
        record = {"id": identifier, "kind": kind, "result": "pass", "summary": "Unit test fixture", "command": None,
                  "created_at": "2026-01-01T00:00:00Z", "source_commit": None,
                  "artifacts": [{"path": f"docs/{identifier}.txt", "sha256": project.sha256(path)}],
                  "specimen_id": None, "independent": False, "reference_evidence": [], "limitations": ["Synthetic test fixture"]}
        record.update(overrides)
        self.registry["records"].append(record)
        return record

    def test_seed_and_deterministic_generation(self):
        self.assertValid()
        first = project.render_state(self.state)
        self.assertEqual(first, project.render_state(self.state))
        self.assertEqual(self.cli("sync")[0], 0)
        self.assertEqual(self.cli("sync", "--check")[0], 0)
        (self.root / "docs/PROJECT_STATE.md").write_text("stale")
        status, _, error = self.cli("sync", "--check")
        self.assertEqual(status, 2)
        self.assertIn("stale", error)

    def test_unknown_schema_fields_status_and_types(self):
        invalid = [("schema_version", True), ("revision", -1), ("fidelity_status", []), ("updated_at", "2026-02-30T00:00:00Z")]
        for field, value in invalid:
            with self.subTest(field=field):
                state = deepcopy(self.state)
                state[field] = value
                with self.assertRaises(project.StateError):
                    project.validate(self.root, state, self.registry)
        self.state["extra"] = "unknown"
        with self.assertRaisesRegex(project.StateError, "unknown"):
            self.assertValid()

    def test_unknown_dependency_and_cycle(self):
        self.state["tasks"][0]["dependencies"] = ["NO-000"]
        with self.assertRaisesRegex(project.StateError, "unknown dependency"):
            self.assertValid()
        self.state["tasks"][0]["dependencies"] = ["PHY-001"]
        with self.assertRaisesRegex(project.StateError, "cycle"):
            self.assertValid()

    def test_duplicate_ids_and_invalid_status(self):
        self.state["tasks"].append(deepcopy(self.state["tasks"][0]))
        with self.assertRaisesRegex(project.StateError, "duplicate task"):
            self.assertValid()
        self.state["tasks"].pop()
        self.state["tasks"][0]["status"] = []
        with self.assertRaisesRegex(project.StateError, "unknown status"):
            self.assertValid()

    def test_done_requires_passing_evidence_of_every_required_kind(self):
        task = self.state["tasks"][0]
        task["status"] = "done"
        with self.assertRaisesRegex(project.StateError, "requires evidence"):
            self.assertValid()
        record = self.record()
        task["evidence"] = [record["id"]]
        with self.assertRaisesRegex(project.StateError, "missing evidence kinds"):
            self.assertValid()
        documentary = self.record("doc-1", "documentary")
        task["evidence"].append(documentary["id"])
        self.assertValid()
        record["result"] = "fail"
        with self.assertRaisesRegex(project.StateError, "must pass"):
            self.assertValid()

    def test_done_requires_completed_dependencies(self):
        task = project.task_by_id(self.state, "PHY-001")
        task.update(status="done", evidence=[self.record()["id"]])
        with self.assertRaisesRegex(project.StateError, "dependency FND-001 is not done"):
            self.assertValid()

    def test_artifact_hash_detects_changes_and_missing_files(self):
        self.record()
        self.assertValid()
        path = self.root / "docs/check-1.txt"
        path.write_text("changed")
        with self.assertRaisesRegex(project.StateError, "hash changed"):
            self.assertValid()
        path.unlink()
        with self.assertRaisesRegex(project.StateError, "artifact missing"):
            self.assertValid()

    def test_path_traversal_absolute_and_git_paths_rejected(self):
        for value in ["../secret", "/etc/passwd", "C:/secret", ".git/config", "docs/../../secret", "docs\\secret"]:
            with self.subTest(value=value), self.assertRaises(project.StateError):
                project.relative_path(self.root, value, "test")

    def test_symlink_escape_rejected(self):
        with tempfile.TemporaryDirectory() as outside:
            try:
                (self.root / "escape").symlink_to(outside, target_is_directory=True)
            except (OSError, NotImplementedError):
                self.skipTest("Host does not permit symlinks")
            with self.assertRaisesRegex(project.StateError, "escapes repository"):
                project.relative_path(self.root, "escape/file.txt", "test")

    def test_hardware_validation_cannot_use_synthetic_reference(self):
        synthetic = self.record()
        validation = self.record("heldout", "hardware_validation", specimen_id="SPECIMEN-TEST", independent=True, reference_evidence=[synthetic["id"]])
        with self.assertRaisesRegex(project.StateError, "same specimen"):
            self.assertValid()
        measurement = self.record("measurement", "measurement", specimen_id="SPECIMEN-TEST")
        validation["reference_evidence"] = [measurement["id"]]
        self.assertValid()
        validation["independent"] = False
        with self.assertRaisesRegex(project.StateError, "independent"):
            self.assertValid()

    def test_numerical_evidence_cannot_claim_specimen(self):
        self.record(specimen_id="NOT-MEASURED")
        with self.assertRaisesRegex(project.StateError, "cannot claim a measured specimen"):
            self.assertValid()

    def test_fidelity_promotion_requires_hardware_gate(self):
        self.state["fidelity_status"] = "measured_one_note"
        with self.assertRaisesRegex(project.StateError, "requires completed VAL-001"):
            self.assertValid()

    def test_cli_claim_complete_and_generated_status(self):
        self.record("numerical", "numerical")
        self.record("documentary", "documentary")
        self.save()
        status, output, _ = self.cli("claim", "FND-001", "--owner", "worker")
        self.assertEqual(status, 0)
        token = json.loads(output)["token"]
        self.assertEqual(self.cli("complete", "FND-001", "--owner", "other", "--token", token, "--evidence", "numerical")[0], 2)
        self.assertEqual(self.cli("complete", "FND-001", "--owner", "worker", "--token", token, "--evidence", "numerical")[0], 2)
        self.reload()
        self.assertEqual(self.state["revision"], 1)
        status, _, error = self.cli("complete", "FND-001", "--owner", "worker", "--token", token, "--evidence", "numerical", "--evidence", "documentary")
        self.assertEqual((status, error), (0, ""))
        self.reload()
        self.assertEqual(self.state["tasks"][0]["status"], "done")
        self.assertEqual(self.state["revision"], 2)
        self.assertEqual(self.cli("sync", "--check")[0], 0)
        self.assertEqual(self.cli("claim", "FND-001", "--owner", "worker")[0], 2)

    def test_dependency_claim_rejected(self):
        status, _, error = self.cli("claim", "PHY-001", "--owner", "worker")
        self.assertEqual(status, 2)
        self.assertIn("unfinished dependencies", error)

    def test_release_block_unblock_and_renew_preserve_history(self):
        status, output, _ = self.cli("claim", "FND-001", "--owner", "worker", "--minutes", "1")
        self.assertEqual(status, 0)
        token = json.loads(output)["token"]
        self.assertEqual(self.cli("renew", "FND-001", "--owner", "worker", "--token", token, "--minutes", "5")[0], 0)
        self.assertEqual(self.cli("block", "FND-001", "--owner", "worker", "--token", token, "--reason", "Await artifact")[0], 0)
        self.assertEqual(self.cli("claim", "FND-001", "--owner", "worker")[0], 2)
        self.assertEqual(self.cli("unblock", "FND-001", "--owner", "integrator", "--reason", "Artifact supplied")[0], 0)
        _, output, _ = self.cli("claim", "FND-001", "--owner", "next-worker")
        token = json.loads(output)["token"]
        self.assertEqual(self.cli("release", "FND-001", "--owner", "next-worker", "--token", token, "--reason", "Handoff: code ready; tests pending")[0], 0)
        self.reload()
        self.assertEqual(len(self.state["history"]), 6)
        self.assertEqual(self.state["tasks"][0]["status"], "todo")

    def test_expired_claim_can_be_replaced_but_not_completed(self):
        task = self.state["tasks"][0]
        task.update(status="in_progress", owner="old", claim={"owner": "old", "token": "old-token", "claimed_at": "2020-01-01T00:00:00Z", "expires_at": "2020-01-01T01:00:00Z"})
        self.save()
        self.assertEqual(self.cli("complete", "FND-001", "--owner", "old", "--token", "old-token", "--evidence", "absent")[0], 2)
        self.assertEqual(self.cli("claim", "FND-001", "--owner", "new")[0], 0)
        self.reload()
        self.assertEqual(task["owner"], "old")
        self.assertEqual(self.state["tasks"][0]["owner"], "new")

    def test_competing_processes_only_one_claim_succeeds(self):
        command = [sys.executable, str(ROOT / "tools/project.py"), "--root", str(self.root), "claim", "FND-001", "--owner"]
        processes = [subprocess.Popen(command + [owner], stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True) for owner in ("one", "two")]
        results = [process.communicate(timeout=15) for process in processes]
        self.assertEqual(sorted(process.returncode for process in processes), [0, 2], results)
        self.reload()
        self.assertEqual(self.state["revision"], 1)
        self.assertFalse((self.root / "project/.project.lock").exists())

    def test_evidence_add_does_not_execute_command_and_rejects_duplicate(self):
        (self.root / "docs/result.txt").write_text("Already-observed output\n")
        arguments = ("evidence-add", "observed", "--kind", "numerical", "--result", "pass", "--summary", "Observed fixture", "--artifact", "docs/result.txt", "--command", "THIS_IS_NOT_EXECUTED")
        self.assertEqual(self.cli(*arguments)[0], 0)
        self.assertEqual(self.cli(*arguments)[0], 2)
        self.reload()
        self.assertEqual(self.registry["records"][0]["command"], "THIS_IS_NOT_EXECUTED")
        self.assertEqual(self.state["revision"], 0)

    def test_duplicate_json_keys_and_nonfinite_values_rejected(self):
        path = self.root / "project/invalid.json"
        for content in ('{"id":1,"id":2}', '{"value":NaN}', '{"value":Infinity}'):
            path.write_text(content)
            with self.assertRaises(project.StateError):
                project.load_json(path)

    def test_history_revision_and_timestamp_consistency(self):
        self.state["revision"] = 1
        with self.assertRaisesRegex(project.StateError, "one event per revision"):
            self.assertValid()
        self.state["history"] = [{"revision":1,"at":"2027-01-01T00:00:00Z","action":"claim","task":"FND-001","owner":"test","detail":"test"}]
        with self.assertRaisesRegex(project.StateError, "exceeds updated_at"):
            self.assertValid()

    def test_reopen_requires_dependents_first_and_keeps_evidence(self):
        numerical = self.record("numerical")
        documentary = self.record("documentary", "documentary")
        self.state["tasks"][0].update(status="done", evidence=[numerical["id"], documentary["id"]])
        project.task_by_id(self.state, "PHY-001").update(status="done", evidence=[numerical["id"]])
        self.save()
        self.assertEqual(self.cli("reopen", "FND-001", "--owner", "reviewer", "--reason", "Regression")[0], 2)
        self.assertEqual(self.cli("reopen", "PHY-001", "--owner", "reviewer", "--reason", "Regression")[0], 0)
        self.assertEqual(self.cli("reopen", "FND-001", "--owner", "reviewer", "--reason", "Regression")[0], 0)
        self.reload()
        self.assertEqual(len(self.registry["records"]), 2)
        self.assertEqual(self.state["tasks"][0]["evidence"], ["numerical", "documentary"])

    def test_generated_document_cannot_escape_through_symlink(self):
        with tempfile.TemporaryDirectory() as outside:
            (self.root / "docs").rmdir()
            try:
                (self.root / "docs").symlink_to(outside, target_is_directory=True)
            except (OSError, NotImplementedError):
                self.skipTest("Host does not permit symlinks")
            self.assertEqual(self.cli("sync")[0], 2)
            self.assertFalse((Path(outside) / "PROJECT_STATE.md").exists())


if __name__ == "__main__":
    unittest.main()
