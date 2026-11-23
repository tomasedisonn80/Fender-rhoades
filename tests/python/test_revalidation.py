"""A075: reopening requires new passing registrations for every required kind."""
from __future__ import annotations

from contextlib import redirect_stderr, redirect_stdout
from copy import deepcopy
import io
import json
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

from test_project import project


class RevalidationTests(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory()
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name)
        (self.root / "project").mkdir()
        (self.root / "docs").mkdir()
        # All registrations and events deliberately share a timestamp. Leases
        # still expire in the future relative to the actual active-claim clock.
        self.stamp = project.now()
        clock = patch.object(project, "now", return_value=self.stamp)
        clock.start()
        self.addCleanup(clock.stop)
        self.state = {
            "schema_version": 1, "project": "Revalidation fixture", "goal": "Check reopening",
            "target": "Temporary synthetic fixture", "fidelity_status": "unvalidated",
            "revision": 0, "updated_at": self.stamp, "history": [],
            "tasks": [{
                "id": "TST-001", "title": "Fixture task", "milestone": "Fixture",
                "status": "todo", "priority": 0, "dependencies": [],
                "acceptance": ["Test fixture only"],
                "required_evidence": ["documentary", "numerical"],
                "evidence": [], "owner": None, "claim": None, "blockers": [], "paths": [],
            }],
        }
        self.registry = {"schema_version": 1, "records": []}
        (self.root / "project/state.json").write_text(project.encode(self.state), encoding="utf-8")
        (self.root / "project/evidence.json").write_text(project.encode(self.registry), encoding="utf-8")

    def reload(self):
        self.state, self.registry = project.read_project(self.root)

    def cli(self, *arguments):
        output, errors = io.StringIO(), io.StringIO()
        with redirect_stdout(output), redirect_stderr(errors):
            status = project.main(["--root", str(self.root), *arguments])
        return status, output.getvalue(), errors.getvalue()

    def succeed(self, *arguments):
        status, output, error = self.cli(*arguments)
        self.assertEqual((status, error), (0, ""))
        self.reload()
        return output

    def register(self, identifier, kind, result="pass"):
        artifact = f"docs/{identifier}.txt"
        (self.root / artifact).write_text(
            f"Test-only registration {identifier}; no instrument observation.\n", encoding="utf-8")
        self.succeed("evidence-add", identifier, "--kind", kind, "--result", result,
                     "--summary", "Synthetic revalidation test fixture", "--artifact", artifact)

    def register_pair(self, prefix):
        identifiers = [f"{prefix}-doc", f"{prefix}-num"]
        self.register(identifiers[0], "documentary")
        self.register(identifiers[1], "numerical")
        return identifiers

    def claim(self):
        output = self.succeed("claim", "TST-001", "--owner", "worker")
        return json.loads(output)["token"]

    def completion_args(self, token, identifiers):
        arguments = ["complete", "TST-001", "--owner", "worker", "--token", token]
        for identifier in identifiers:
            arguments.extend(["--evidence", identifier])
        return arguments

    def complete(self, token, identifiers):
        self.succeed(*self.completion_args(token, identifiers))

    def reopen(self):
        self.succeed("reopen", "TST-001", "--owner", "reviewer", "--reason", "Fixture regression")
        return deepcopy(self.state["tasks"][0]["revalidation"])

    def completed_fixture(self):
        identifiers = self.register_pair("old")
        self.complete(self.claim(), identifiers)
        return identifiers

    def reopened_fixture(self):
        identifiers = self.completed_fixture()
        self.reopen()
        return identifiers

    def assert_completion_rejected(self, token, identifiers, message):
        paths = [self.root / name for name in
                 ("project/state.json", "project/evidence.json", "docs/PROJECT_STATE.md")]
        saved = [path.read_bytes() for path in paths]
        status, _, error = self.cli(*self.completion_args(token, identifiers))
        self.assertEqual(status, 2)
        self.assertIn(message, error)
        self.assertEqual([path.read_bytes() for path in paths], saved)
        self.reload()
        self.assertEqual(self.state["tasks"][0]["status"], "in_progress")
        self.assertEqual(self.state["tasks"][0]["claim"]["token"], token)

    def test_never_reopened_completion_keeps_existing_contract(self):
        self.completed_fixture()
        self.assertEqual(self.state["tasks"][0]["status"], "done")
        self.assertNotIn("revalidation", self.state["tasks"][0])
        project.validate(self.root, self.state, self.registry)

    def test_reopen_snapshots_whole_registry_and_preserves_old_evidence(self):
        old = self.completed_fixture()
        self.register("unattached", "documentary")
        self.register("failed-observation", "synthetic", "fail")
        original_registry = (self.root / "project/evidence.json").read_bytes()
        original_artifacts = {path: path.read_bytes() for path in (self.root / "docs").glob("*.txt")}
        gate = self.reopen()
        self.assertEqual(gate["reopen_revision"], self.state["history"][-1]["revision"])
        self.assertEqual(self.state["history"][-1]["action"], "reopen")
        self.assertEqual(gate["excluded_evidence"], sorted(old + ["unattached", "failed-observation"]))
        self.assertEqual(self.state["tasks"][0]["evidence"], old)
        self.assertEqual((self.root / "project/evidence.json").read_bytes(), original_registry)
        self.assertEqual({path: path.read_bytes() for path in original_artifacts}, original_artifacts)

    def test_old_and_unrelated_new_records_cannot_complete_or_change_saved_files(self):
        old = self.reopened_fixture()
        token = self.claim()
        self.assert_completion_rejected(token, old, "registered after reopen")
        self.register("new-performance", "performance")
        self.assert_completion_rejected(token, old + ["new-performance"], "registered after reopen")

    def test_preexisting_unattached_records_cannot_satisfy_revalidation(self):
        self.completed_fixture()
        unused = self.register_pair("unused")
        self.reopen()
        self.assert_completion_rejected(self.claim(), unused, "registered after reopen")

    def test_every_required_kind_needs_a_new_passing_record(self):
        old = self.reopened_fixture()
        gate = deepcopy(self.state["tasks"][0]["revalidation"])
        token = self.claim()
        self.register("fresh-doc", "documentary")
        self.assert_completion_rejected(token, ["fresh-doc", old[1]], "['numerical']")
        self.register("fresh-num", "numerical")
        self.complete(token, old + ["fresh-doc", "fresh-num"])
        self.assertEqual(self.state["tasks"][0]["evidence"], old + ["fresh-doc", "fresh-num"])
        self.assertEqual(self.state["tasks"][0]["revalidation"], gate)
        self.assertEqual(len(self.registry["records"]), 4)

    def test_new_failed_or_inconclusive_records_do_not_satisfy_completion(self):
        old = self.reopened_fixture()
        token = self.claim()
        self.register("fresh-doc", "documentary")
        for result in ("fail", "inconclusive"):
            with self.subTest(result=result):
                identifier = f"new-{result}"
                self.register(identifier, "numerical", result)
                self.assert_completion_rejected(token, ["fresh-doc", identifier], "must pass")
        self.assert_completion_rejected(token, ["fresh-doc", old[1]], "['numerical']")

    def test_same_second_registration_succeeds_and_gate_survives_completion(self):
        self.reopened_fixture()
        gate = deepcopy(self.state["tasks"][0]["revalidation"])
        token = self.claim()
        fresh = self.register_pair("same-second")
        self.complete(token, fresh)
        self.assertEqual({event["at"] for event in self.state["history"]}, {self.stamp})
        self.assertEqual({record["created_at"] for record in self.registry["records"]}, {self.stamp})
        self.assertEqual(self.state["tasks"][0]["status"], "done")
        self.assertEqual(self.state["tasks"][0]["revalidation"], gate)
        project.validate(self.root, self.state, self.registry)

    def test_repeated_reopen_replaces_snapshot_and_requires_another_new_pair(self):
        old = self.reopened_fixture()
        first_gate = deepcopy(self.state["tasks"][0]["revalidation"])
        first_repair = self.register_pair("first-repair")
        self.complete(self.claim(), first_repair)
        self.register("later-unattached", "synthetic")
        second_gate = self.reopen()
        self.assertGreater(second_gate["reopen_revision"], first_gate["reopen_revision"])
        self.assertEqual(second_gate["excluded_evidence"], sorted(old + first_repair + ["later-unattached"]))
        token = self.claim()
        self.assert_completion_rejected(token, first_repair, "registered after reopen")
        second_repair = self.register_pair("second-repair")
        self.complete(token, second_repair)
        self.assertEqual(self.state["tasks"][0]["revalidation"], second_gate)
        self.assertEqual(len(self.registry["records"]), 7)

    def test_direct_validation_enforces_fresh_evidence_for_done_state(self):
        self.reopened_fixture()
        stale = deepcopy(self.state)
        stale["tasks"][0]["status"] = "done"
        with self.assertRaisesRegex(project.StateError, "registered after reopen"):
            project.validate(self.root, stale, self.registry)
        fresh = self.register_pair("direct")
        direct = deepcopy(self.state)
        direct["tasks"][0].update(status="done", evidence=fresh)
        project.validate(self.root, direct, self.registry)

    def test_recorded_reopen_requires_gate_in_every_status(self):
        self.reopened_fixture()
        fresh = self.register_pair("fresh")
        for status in sorted(project.STATUSES):
            with self.subTest(status=status):
                direct = deepcopy(self.state)
                task = direct["tasks"][0]
                task.update(status=status, evidence=fresh, blockers=["Fixture blocker"] if status == "blocked" else [])
                del task["revalidation"]
                with self.assertRaisesRegex(project.StateError, "recorded reopen requires revalidation"):
                    project.validate(self.root, direct, self.registry)

    def test_malformed_revalidation_is_rejected_by_direct_validation(self):
        self.reopened_fixture()
        gate = self.state["tasks"][0]["revalidation"]
        invalid = [None, [], {}, {**gate, "extra": True},
                   {"reopen_revision": gate["reopen_revision"]},
                   {"excluded_evidence": gate["excluded_evidence"]}]
        invalid += [{**gate, "reopen_revision": value}
                    for value in (True, 0, -1, 1.0, "3", self.state["revision"] + 1)]
        invalid += [{**gate, "excluded_evidence": value}
                    for value in (None, [], "old-doc", ["old-doc", "old-doc"], ["absent"], [3], [" "])]
        for value in invalid:
            with self.subTest(value=value):
                direct = deepcopy(self.state)
                direct["tasks"][0]["revalidation"] = value
                with self.assertRaisesRegex(project.StateError, "revalidation"):
                    project.validate(self.root, direct, self.registry)

    def test_gate_cannot_point_to_a_non_reopen_event(self):
        self.reopened_fixture()
        self.claim()
        direct = deepcopy(self.state)
        direct["tasks"][0]["revalidation"]["reopen_revision"] = direct["revision"]
        with self.assertRaisesRegex(project.StateError, "latest reopen event"):
            project.validate(self.root, direct, self.registry)

    def test_gate_cannot_point_to_an_earlier_reopen(self):
        self.reopened_fixture()
        first_revision = self.state["tasks"][0]["revalidation"]["reopen_revision"]
        fresh = self.register_pair("fresh")
        self.complete(self.claim(), fresh)
        self.reopen()
        direct = deepcopy(self.state)
        direct["tasks"][0]["revalidation"]["reopen_revision"] = first_revision
        with self.assertRaisesRegex(project.StateError, "latest reopen event"):
            project.validate(self.root, direct, self.registry)

    def test_gate_cannot_point_to_another_tasks_reopen(self):
        self.reopened_fixture()
        direct = deepcopy(self.state)
        other = deepcopy(direct["tasks"][0])
        other["id"] = "TST-002"
        direct["tasks"].append(other)
        project.event(direct, other, "reopen", "reviewer", "Synthetic second-task event")
        other["revalidation"]["reopen_revision"] = direct["revision"]
        direct["tasks"][0]["revalidation"]["reopen_revision"] = direct["revision"]
        with self.assertRaisesRegex(project.StateError, "latest reopen event"):
            project.validate(self.root, direct, self.registry)

    def test_gate_without_any_recorded_reopen_is_rejected(self):
        old = self.completed_fixture()
        direct = deepcopy(self.state)
        direct["tasks"][0].update(status="todo", revalidation={
            "reopen_revision": direct["revision"], "excluded_evidence": old})
        with self.assertRaisesRegex(project.StateError, "latest reopen event"):
            project.validate(self.root, direct, self.registry)


if __name__ == "__main__":
    unittest.main()
