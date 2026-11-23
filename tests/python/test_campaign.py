"""Campaign integrity regressions; every assignment and report is a temporary fixture.

No canonical campaign data is loaded or changed. These synthetic task names test
ledger behavior; they do not represent collaboration launches or agent identity.
"""
from __future__ import annotations

from contextlib import redirect_stderr, redirect_stdout
import hashlib
import importlib.util
import io
import json
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[2]
SPEC = importlib.util.spec_from_file_location("rhodes_campaign", ROOT / "tools/campaign.py")
assert SPEC and SPEC.loader
campaign = importlib.util.module_from_spec(SPEC)
# Import the functions without executing the CLI or consulting the real ledger.
with patch("argparse.ArgumentParser.parse_args", side_effect=AssertionError("CLI ran on import")):
    SPEC.loader.exec_module(campaign)


class CampaignTests(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory()
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name).resolve()
        self.ledger = self.root / "project/agent-campaign.json"
        self.index = self.root / "docs/agents/CAMPAIGN.md"
        self.ledger.parent.mkdir()
        (self.root / "docs/agents/reviews").mkdir(parents=True)
        self.data = {"schema_version": 1, "requested_agents": 100, "agents": []}
        self.save()
        self.index.write_text("# Previous generated index\n", encoding="utf-8")
        replacements = patch.multiple(campaign, ROOT=self.root, LEDGER=self.ledger, INDEX=self.index)
        replacements.start()
        self.addCleanup(replacements.stop)

    def save(self):
        self.ledger.write_text(json.dumps(self.data, indent=2) + "\n", encoding="utf-8")

    def read(self):
        return json.loads(self.ledger.read_text(encoding="utf-8"))

    def add_agent(self, identifier="A001", completed=False):
        agent = {
            "id": identifier, "task_name": f"/root/fixture_{identifier.lower()}",
            "status": "running", "scope": "Synthetic isolated test assignment",
            "report": f"docs/agents/reviews/{identifier}.md", "report_sha256": None,
        }
        if completed:
            payload = f"# {identifier} synthetic test report\nNo actual agent or observation.\n".encode()
            (self.root / agent["report"]).write_bytes(payload)
            agent.update(status="completed", report_sha256=hashlib.sha256(payload).hexdigest())
        self.data["agents"].append(agent)
        return agent

    def cli(self, *arguments):
        output, errors = io.StringIO(), io.StringIO()
        with patch.object(sys, "argv", ["campaign.py", *arguments]), \
                redirect_stdout(output), redirect_stderr(errors):
            status = campaign.main()
        return status, output.getvalue(), errors.getvalue()

    def assert_success(self, *arguments):
        status, output, error = self.cli(*arguments)
        self.assertEqual((status, error), (0, ""))
        return output

    def assert_rejected_without_writes(self, *arguments, message=None):
        before_ledger = self.ledger.read_bytes()
        before_index = self.index.read_bytes() if self.index.exists() else None
        status, output, error = self.cli(*arguments)
        self.assertEqual(status, 1, (output, error))
        self.assertTrue(error.startswith("Campaign error:"), error)
        if message is not None:
            self.assertIn(message, error)
        self.assertEqual(self.ledger.read_bytes(), before_ledger)
        self.assertEqual(self.index.read_bytes() if self.index.exists() else None, before_index)

    def test_partial_campaign_validates_without_changing_files(self):
        self.add_agent(completed=True)
        self.add_agent("A002")
        self.save()
        before = (self.ledger.read_bytes(), self.index.read_bytes())
        self.assertIn("1/100 completed", self.assert_success("validate"))
        self.assertEqual((self.ledger.read_bytes(), self.index.read_bytes()), before)
        self.assert_rejected_without_writes("validate", "--require-complete", message="not complete")

    def test_strict_completion_requires_all_100_reports_and_statuses(self):
        for number in range(1, 100):
            self.add_agent(f"A{number:03d}", completed=True)
        self.save()
        self.assert_rejected_without_writes("validate", "--require-complete", message="not complete")
        last = self.add_agent("A100")
        self.save()
        self.assert_success("validate")
        self.assert_rejected_without_writes("validate", "--require-complete", message="not complete")
        (self.root / last["report"]).write_text("# Synthetic final fixture report\n", encoding="utf-8")
        self.assert_success("complete", "A100")
        before = self.ledger.read_bytes()
        self.assertIn("100/100 completed", self.assert_success("validate", "--require-complete"))
        self.assertEqual(self.ledger.read_bytes(), before)

    def test_requested_count_must_be_a_positive_integer(self):
        for count in (0, -1, 1000, True, False, 100.0, "100", None, [], {}):
            with self.subTest(count=count):
                self.data["requested_agents"] = count
                self.save()
                self.assert_rejected_without_writes("validate")

    def test_requested_count_and_id_range_accept_lower_and_upper_bounds(self):
        for count, identifier in ((1, "A001"), (999, "A999")):
            with self.subTest(count=count):
                self.data.update(requested_agents=count, agents=[])
                self.add_agent(identifier)
                self.save()
                self.assert_success("validate")

    def test_assignments_cannot_exceed_requested_count(self):
        self.data["requested_agents"] = 1
        self.add_agent()
        self.save()
        self.assert_success("validate")
        self.assert_rejected_without_writes(
            "register", "A002", "/root/fixture_a002", "Excess synthetic assignment",
            message="more assignments than requested")
        self.add_agent("A002")
        self.save()
        self.assert_rejected_without_writes("validate", message="more assignments than requested")

    def test_duplicate_ids_and_task_names_are_rejected_on_register(self):
        self.add_agent()
        self.save()
        for identifier, name in (("A001", "/root/other_fixture"), ("A002", "/root/fixture_a001")):
            with self.subTest(identifier=identifier, task_name=name):
                self.assert_rejected_without_writes(
                    "register", identifier, name, "Duplicate synthetic fixture", message="duplicate")

    def test_duplicate_ids_and_task_names_are_rejected_in_existing_ledger(self):
        first = self.add_agent()
        second = self.add_agent("A002")
        for field in ("id", "task_name"):
            with self.subTest(field=field):
                original = second[field]
                second[field] = first[field]
                self.save()
                self.assert_rejected_without_writes("validate", message="duplicate")
                second[field] = original

    def test_malformed_ids_are_rejected(self):
        agent = self.add_agent()
        for identifier in ("", "A000", "A101", "a001", "A01", "A0001", " A001", "A001\n", "../A001",
                           "A\u0660\u0660\u0661", 1, None, [], {}):
            with self.subTest(identifier=identifier):
                agent["id"] = identifier
                agent["report"] = f"docs/agents/reviews/{identifier}.md"
                self.save()
                self.assert_rejected_without_writes("validate")

    def test_malformed_task_names_and_scopes_are_controlled_failures(self):
        agent = self.add_agent()
        cases = {
            "task_name": ("", "fixture_a001", "/else/fixture_a001", "/root/", "/root//fixture",
                          "/root/../fixture", "/root/fixture/", "/root/fixture name",
                          "/root/fixture\n", None, 1, [], {}),
            "scope": ("", " \t\n", None, 1, [], {}),
        }
        for field, values in cases.items():
            original = agent[field]
            for value in values:
                with self.subTest(field=field, value=value):
                    agent[field] = value
                    self.save()
                    self.assert_rejected_without_writes("validate")
            agent[field] = original

    def test_invalid_statuses_are_rejected(self):
        agent = self.add_agent()
        for status in ("", "done", "complete", "COMPLETED", None, 1, [], {}):
            with self.subTest(status=status):
                agent["status"] = status
                self.save()
                self.assert_rejected_without_writes("validate")

    def test_report_paths_must_match_assignment_exactly(self):
        agent = self.add_agent()
        for report in ("docs/agents/reviews/A002.md", "docs/agents/reviews/../A001.md",
                       "/tmp/A001.md", "A001.md", "docs/agents/reviews/A001.MD", None, []):
            with self.subTest(report=report):
                agent["report"] = report
                self.save()
                self.assert_rejected_without_writes("validate", message="report path mismatch")

    def test_malformed_ledger_shapes_and_json_are_controlled_failures(self):
        cases = [[], None, "ledger", 1, {}, {"schema_version": 1, "requested_agents": 100},
                 {"schema_version": 1, "requested_agents": 100, "agents": None},
                 {"schema_version": 1, "requested_agents": 100, "agents": {}},
                 {"schema_version": 1, "requested_agents": 100, "agents": ""},
                 {"schema_version": 1, "requested_agents": 100, "agents": [None]},
                 {"schema_version": 1, "requested_agents": 100, "agents": [{}]}]
        cases.extend(dict(self.data, schema_version=version) for version in (True, 1.0, "1", 2, None))
        for data in cases:
            with self.subTest(data=data):
                self.ledger.write_text(json.dumps(data) + "\n", encoding="utf-8")
                self.assert_rejected_without_writes("validate")
        self.ledger.write_text("{broken JSON\n", encoding="utf-8")
        self.assert_rejected_without_writes("validate")

    def test_assignment_fields_are_required(self):
        original = self.add_agent()
        for field in original:
            with self.subTest(field=field):
                self.data["agents"] = [{key: value for key, value in original.items() if key != field}]
                self.save()
                self.assert_rejected_without_writes("validate")

    def test_register_stores_supplied_assignment_as_running_and_updates_index(self):
        task_name = "/root/synthetic_supplied_name"
        self.assert_success("register", "A007", task_name, "Fixture scope")
        records = self.read()["agents"]
        self.assertEqual(len(records), 1)
        self.assertEqual(records[0], {
            "id": "A007", "task_name": task_name, "scope": "Fixture scope",
            "status": "running", "report": "docs/agents/reviews/A007.md", "report_sha256": None,
        })
        self.assertFalse((self.root / records[0]["report"]).exists())
        self.assertIn("| A007 | Fixture scope | running | pending |", self.index.read_text())
        self.assert_success("sync", "--check")

    def test_nested_task_names_can_be_registered(self):
        task_name = "/root/fixture_parent/fixture_child"
        self.assert_success("register", "A001", task_name, "Nested synthetic fixture")
        self.assertEqual(self.read()["agents"][0]["task_name"], task_name)

    def test_running_assignment_cannot_claim_a_completed_digest(self):
        agent = self.add_agent()
        for digest in ("0" * 64, "", 1, False, [], {}):
            with self.subTest(digest=digest):
                agent["report_sha256"] = digest
                self.save()
                self.assert_rejected_without_writes("validate")

    def test_complete_records_exact_report_bytes_and_is_idempotent(self):
        agent = self.add_agent()
        self.save()
        payload = b"# Synthetic fixture\r\nUTF-8: \xc3\xa9\n"
        report = self.root / agent["report"]
        report.write_bytes(payload)
        self.assert_success("complete", "A001")
        completed = self.read()["agents"][0]
        self.assertEqual(completed["status"], "completed")
        self.assertEqual(completed["report_sha256"], hashlib.sha256(payload).hexdigest())
        self.assertEqual(report.read_bytes(), payload)
        self.assertIn("[report](reviews/A001.md)", self.index.read_text())
        before = (self.ledger.read_bytes(), self.index.read_bytes())
        self.assert_success("complete", "A001")
        self.assertEqual((self.ledger.read_bytes(), self.index.read_bytes()), before)

    def test_batch_completion_validates_every_assignment_before_publishing(self):
        first = self.add_agent()
        second = self.add_agent("A002")
        self.save()
        (self.root / first["report"]).write_text("# Synthetic fixture\n", encoding="utf-8")
        self.assert_rejected_without_writes("complete", "A001", "A999", message="unknown agent")
        self.assert_rejected_without_writes("complete", "A001", "A002", message="missing regular report")
        self.assertEqual([a["status"] for a in self.read()["agents"]], ["running", "running"])
        (self.root / second["report"]).write_text("# Synthetic second fixture\n", encoding="utf-8")
        self.assert_success("complete", "A001", "A002")
        self.assertEqual([a["status"] for a in self.read()["agents"]], ["completed", "completed"])

    def test_completed_report_cannot_be_rehashed_by_complete_or_sync(self):
        agent = self.add_agent(completed=True)
        self.save()
        (self.root / agent["report"]).write_text("# Changed after completion\n", encoding="utf-8")
        for arguments in (("validate",), ("complete", "A001"), ("sync",), ("sync", "--check"),
                          ("register", "A002", "/root/fixture_a002", "Synthetic fixture")):
            with self.subTest(arguments=arguments):
                self.assert_rejected_without_writes(*arguments, message="report changed")

    def test_completed_report_must_exist_as_a_regular_file(self):
        agent = self.add_agent(completed=True)
        self.save()
        report = self.root / agent["report"]
        report.unlink()
        self.assert_rejected_without_writes("validate", message="missing regular report")
        report.mkdir()
        self.assert_rejected_without_writes("validate", message="missing regular report")

    def test_report_symlink_is_rejected_even_when_target_bytes_match(self):
        agent = self.add_agent(completed=True)
        self.save()
        report = self.root / agent["report"]
        target = self.root / "matching-report.md"
        target.write_bytes(report.read_bytes())
        report.unlink()
        try:
            report.symlink_to(target)
        except (OSError, NotImplementedError):
            self.skipTest("Host does not permit symlinks")
        self.assert_rejected_without_writes("validate", message="missing regular report")
        agent.update(status="running", report_sha256=None)
        self.save()
        self.assert_rejected_without_writes("complete", "A001", message="missing regular report")

    def test_completed_digest_must_match_the_report(self):
        agent = self.add_agent(completed=True)
        for digest in (None, "", "0" * 64, "not-a-digest", [], {}):
            with self.subTest(digest=digest):
                agent["report_sha256"] = digest
                self.save()
                self.assert_rejected_without_writes("validate")

    def test_symlinked_report_ancestor_cannot_escape_root_or_read_external_bytes(self):
        agent = self.add_agent()
        reviews = self.root / "docs/agents/reviews"
        with tempfile.TemporaryDirectory() as outside:
            external = Path(outside) / "A001.md"
            payload = b"# Synthetic external fixture report\n"
            external.write_bytes(payload)
            reviews.rmdir()
            try:
                reviews.symlink_to(outside, target_is_directory=True)
            except (OSError, NotImplementedError):
                self.skipTest("Host does not permit symlinks")
            original_read_bytes = Path.read_bytes

            def refuse_report_read(path):
                if path == self.root / agent["report"] or path == external:
                    raise AssertionError("Report bytes outside the campaign root were read")
                return original_read_bytes(path)

            for status in ("running", "completed"):
                with self.subTest(status=status):
                    agent.update(status=status, report_sha256=(hashlib.sha256(payload).hexdigest()
                                 if status == "completed" else None))
                    self.save()
                    with patch.object(Path, "read_bytes", new=refuse_report_read):
                        for arguments in (("validate",), ("complete", "A001"), ("sync",)):
                            self.assert_rejected_without_writes(*arguments, message="escapes campaign root")
            self.assertEqual(external.read_bytes(), payload)

    def test_render_is_order_independent_and_preserves_input(self):
        third = self.add_agent("A003")
        third["scope"] = "Scope | separator\nsecond line"
        self.add_agent("A001", completed=True)
        self.add_agent("A002")
        before = json.dumps(self.data, sort_keys=True)
        first = campaign.render(self.data)
        self.assertEqual(json.dumps(self.data, sort_keys=True), before)
        reordered = dict(self.data, agents=list(reversed(self.data["agents"])))
        self.assertEqual(first, campaign.render(reordered))
        rows = [line for line in first.splitlines() if line.startswith("| A")]
        self.assertEqual([line.split("|")[1].strip() for line in rows], ["A001", "A002", "A003"])
        self.assertIn(r"Scope \| separator second line", first)
        self.assertIn("Recorded: **3**. Completed: **1**.", first)
        self.assertTrue(first.endswith("\n"))

    def test_sync_detects_drift_and_repairs_index_without_rewriting_ledger(self):
        self.add_agent(completed=True)
        self.save()
        before_ledger = self.ledger.read_bytes()
        self.assert_rejected_without_writes("sync", "--check", message="index is stale")
        self.assert_success("sync")
        expected = self.index.read_bytes()
        self.assert_success("sync", "--check")
        self.assert_success("sync")
        self.assertEqual(self.index.read_bytes(), expected)
        self.assertEqual(self.ledger.read_bytes(), before_ledger)
        self.index.unlink()
        self.assert_rejected_without_writes("sync", "--check", message="index is stale")
        self.assert_success("sync")
        self.assertEqual(self.index.read_bytes(), expected)
        self.assertEqual(self.ledger.read_bytes(), before_ledger)

    def test_failed_ledger_replace_preserves_files_and_removes_temporary_file(self):
        with patch.object(campaign.os, "replace", side_effect=OSError("Injected ledger rename failure")):
            self.assert_rejected_without_writes(
                "register", "A001", "/root/fixture_a001", "Synthetic fixture",
                message="Injected ledger rename failure")
        self.assertEqual(list(self.ledger.parent.glob(".campaign-*")), [])
        self.assertEqual(list(self.index.parent.glob(".campaign-*")), [])

    def test_failed_index_replace_preserves_committed_ledger_for_sync_recovery(self):
        original_replace = campaign.os.replace
        original_index = self.index.read_bytes()

        def replace_except_index(source, destination):
            if Path(destination) == self.index:
                raise OSError("Injected index rename failure")
            return original_replace(source, destination)

        with patch.object(campaign.os, "replace", side_effect=replace_except_index):
            status, _, error = self.cli("register", "A001", "/root/fixture_a001", "Synthetic fixture")
        self.assertEqual(status, 1)
        self.assertIn("Injected index rename failure", error)
        self.assertEqual([a["id"] for a in self.read()["agents"]], ["A001"])
        committed_ledger = self.ledger.read_bytes()
        self.assertEqual(self.index.read_bytes(), original_index)
        self.assertEqual(list(self.index.parent.glob(".campaign-*")), [])
        self.assert_success("validate")
        self.assert_rejected_without_writes("sync", "--check", message="index is stale")
        self.assert_success("sync")
        self.assert_success("sync", "--check")
        self.assertEqual(self.ledger.read_bytes(), committed_ledger)


if __name__ == "__main__":
    unittest.main()
