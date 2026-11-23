"""Read-only continuation validation using explicitly synthetic temporary data."""
from __future__ import annotations

from contextlib import redirect_stderr, redirect_stdout
import copy
import hashlib
import importlib.util
import io
import json
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[2]
SPEC = importlib.util.spec_from_file_location(
    "rhodes_continuation_campaign", ROOT / "tools/validate_continuation_campaign.py")
assert SPEC and SPEC.loader
campaign = importlib.util.module_from_spec(SPEC)
with patch("argparse.ArgumentParser.parse_args", side_effect=AssertionError("CLI ran on import")):
    SPEC.loader.exec_module(campaign)


class ContinuationCampaignTests(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory()
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name)
        replacement = patch.object(campaign, "ROOT", self.root)
        replacement.start()
        self.addCleanup(replacement.stop)
        self.ledger = self.root / "project/continuation-campaign-20261123.json"
        self.ledger.parent.mkdir(parents=True)
        for name in ("agent-campaign.json", "development-campaign-20261123.json", "state.json"):
            (self.ledger.parent / name).write_text("untouched unrelated fixture\n", encoding="utf-8")
        self.write(self.empty())

    @staticmethod
    def empty():
        return {"schema_version": 1, "requested_agents": 100, "waves": 5,
                "agents_per_wave": 20, "baseline_commit": "a" * 40, "agents": []}

    def write(self, data):
        self.ledger.write_text(json.dumps(data) + "\n", encoding="utf-8")

    def agent(self, number, *, completed=False):
        wave = (number - 1) // 20 + 1
        report = f"docs/agents/continuation-20261123/w{wave}-{number:03d}.md"
        digest = None
        if completed:
            path = self.root / report
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text(f"# Synthetic fixture {number}\nNot an actual agent report.\n",
                            encoding="utf-8")
            digest = hashlib.sha256(path.read_bytes()).hexdigest()
        return {"id": number, "wave": wave, "task_name": f"/root/w{wave}_{number:03d}_fixture",
                "scope": "Synthetic isolated test fixture", "status": "completed" if completed else "running",
                "report": report, "report_sha256": digest}

    def populated(self, count):
        data = self.empty()
        data["agents"] = [self.agent(number, completed=True) for number in range(1, count + 1)]
        return data

    def symlink(self, path, target, *, directory=False):
        try:
            path.symlink_to(target, target_is_directory=directory)
        except (OSError, NotImplementedError) as exc:
            self.skipTest(f"symlink creation is unavailable: {exc}")

    def snapshot(self):
        contents = {}
        for path in self.root.rglob("*"):
            name = path.relative_to(self.root).as_posix()
            if path.is_symlink():
                contents[name] = ("symlink", str(path.readlink()))
            elif path.is_file():
                contents[name] = ("file", path.read_bytes(), path.stat().st_mtime_ns)
            elif path.is_dir():
                contents[name] = ("directory", path.stat().st_mtime_ns)
        return contents

    def cli(self, *arguments, error=None):
        before = self.snapshot()
        output, errors = io.StringIO(), io.StringIO()
        with redirect_stdout(output), redirect_stderr(errors):
            result = campaign.main(list(arguments))
        self.assertEqual(self.snapshot(), before, "validator changed a fixture")
        if error is None:
            self.assertEqual((result, errors.getvalue()), (0, ""), output.getvalue())
        else:
            self.assertEqual(result, 1, output.getvalue())
            self.assertIn(error, errors.getvalue())
            self.assertEqual(output.getvalue(), "")
        return output.getvalue()

    def test_empty_and_running_records_are_valid_without_creating_reports(self):
        self.assertIn("0/100 recorded", self.cli())
        data = self.empty()
        data["agents"] = [self.agent(1)]
        self.write(data)
        self.assertIn("1/100 recorded, 0/100 completed", self.cli())
        self.assertFalse((self.root / data["agents"][0]["report"]).exists())

    def test_exact_top_level_schema_and_integer_types(self):
        mutations = [(key, value) for key in ("schema_version", "requested_agents", "waves", "agents_per_wave")
                     for value in (True, False, 1.0, "1", None, 101)]
        mutations.extend((("agents", {}), ("agents", None), ("unexpected", 1)))
        for key, value in mutations:
            with self.subTest(key=key, value=value):
                data = self.empty()
                data[key] = value
                self.write(data)
                self.cli(error="schema")
        for key in self.empty():
            with self.subTest(missing=key):
                data = self.empty()
                del data[key]
                self.write(data)
                self.cli(error="schema")
        for data in ([], None, True, "ledger"):
            self.write(data)
            self.cli(error="schema")

    def test_baseline_requires_full_hexadecimal_commit_id(self):
        for baseline in (None, True, 123, "a" * 39, "a" * 41, "g" * 40, " " + "a" * 40):
            with self.subTest(baseline=baseline):
                data = self.empty()
                data["baseline_commit"] = baseline
                self.write(data)
                self.cli(error="baseline_commit")

    def test_malformed_duplicate_and_nonfinite_json(self):
        data = self.empty()
        data["agents"] = [self.agent(1)]
        duplicate_nested = json.dumps(data).replace('"id": 1,', '"id": 1, "id": 1,')
        cases = [("{", "campaign error"),
                 ('{"schema_version": 1, "schema_version": 1}', "duplicate JSON key"),
                 (duplicate_nested, "duplicate JSON key")]
        cases.extend((json.dumps(self.empty()).replace('"requested_agents": 100',
                                                      f'"requested_agents": {value}'), "nonfinite")
                     for value in ("NaN", "Infinity", "-Infinity", "1e400", "-1e400"))
        for content, message in cases:
            with self.subTest(content=content):
                self.ledger.write_text(content, encoding="utf-8")
                self.cli(error=message)
        self.ledger.write_bytes(b"\xff")
        self.cli(error="campaign error")

    def test_assignment_fields_and_types_cannot_bypass_validation(self):
        data = self.empty()
        original = self.agent(1)
        mutations = [("id", value) for value in (True, False, 1.0, "1", None, [], {}, 0, 101)]
        mutations.extend((("wave", True), ("wave", 1.0), ("wave", 2), ("scope", " \n"),
                          ("scope", False), ("status", "pending"), ("status", []),
                          ("task_name", None), ("report", None), ("unexpected", 1)))
        for key, value in mutations:
            with self.subTest(key=key, value=value):
                agent = copy.deepcopy(original)
                agent[key] = value
                data["agents"] = [agent]
                self.write(data)
                self.cli(error="campaign error")
        for key in original:
            agent = copy.deepcopy(original)
            del agent[key]
            data["agents"] = [agent]
            self.write(data)
            self.cli(error="assignment fields")
        for entry in ([], True, None):
            data["agents"] = [entry]
            self.write(data)
            self.cli(error="assignment fields")

    def test_conflicting_identifiers_and_task_names_are_rejected(self):
        data = self.empty()
        data["agents"] = [self.agent(1), self.agent(1)]
        data["agents"][1]["task_name"] = "/root/w1_001_other"
        self.write(data)
        self.cli(error="duplicate assignment id")
        data["agents"][1] = self.agent(2)
        data["agents"][1]["task_name"] = data["agents"][0]["task_name"]
        self.write(data)
        self.cli(error="duplicate task_name")

    def test_task_name_prefix_and_slug_are_checked(self):
        for name in ("/root/w2_001_fixture", "/root/w1_002_fixture", "/root/w1_1_fixture",
                     "/root/w1_001_", "/root/w1_001_has space", "/root/w1_001_../escape",
                     "/root/w1_001_FIXTURE", "/root/w1_001_fixture\n", "/other/w1_001_fixture"):
            with self.subTest(name=name):
                data = self.empty()
                agent = self.agent(1)
                agent["task_name"] = name
                data["agents"] = [agent]
                self.write(data)
                self.cli(error="task_name")

    def test_report_path_must_match_assignment_and_stay_inside_repository(self):
        for report in ("../escape.md", "/tmp/escape.md", "docs/agents/../escape.md",
                       "docs/agents/continuation-20261123/w1-002.md",
                       "docs/agents/continuation-20261123/w2-001.md",
                       "docs/agents/continuation-20261123/./w1-001.md"):
            with self.subTest(report=report):
                data = self.empty()
                data["agents"] = [self.agent(1)]
                data["agents"][0]["report"] = report
                self.write(data)
                self.cli(error="report path")
        for relative in ("../escape", "/tmp/escape", "project/../escape", "project\\escape", "./project"):
            with self.subTest(relative=relative), self.assertRaisesRegex(ValueError, "unsafe"):
                campaign.checked_path(relative)

    def test_running_report_hash_is_always_null_even_if_report_exists(self):
        data = self.empty()
        completed = self.agent(1, completed=True)
        data["agents"] = [completed]
        completed["status"] = "running"
        for digest in (completed["report_sha256"], "", False, 0):
            completed["report_sha256"] = digest
            self.write(data)
            self.cli(error="running report hash must be null")
        completed["report_sha256"] = None
        self.write(data)
        self.cli()

    def test_completed_report_requires_current_bytes_and_valid_hash(self):
        data = self.populated(1)
        self.write(data)
        self.cli()
        original = data["agents"][0]["report_sha256"]
        for digest in (None, True, 1, "a" * 63, "g" * 64):
            data["agents"][0]["report_sha256"] = digest
            self.write(data)
            self.cli(error="invalid report SHA256")
        data["agents"][0]["report_sha256"] = original
        self.write(data)
        path = self.root / data["agents"][0]["report"]
        path.write_bytes(path.read_bytes() + b"Changed fixture.\n")
        self.cli(error="completed report changed")
        path.unlink()
        self.cli(error="missing regular completed report")
        path.mkdir()
        self.cli(error="missing regular completed report")

    def test_each_later_wave_requires_every_prior_assignment_completed(self):
        for boundary in (20, 40, 60, 80):
            with self.subTest(boundary=boundary):
                data = self.populated(boundary - 1)
                data["agents"].append(self.agent(boundary + 1))
                self.write(data)
                self.cli(error="earlier wave completed")
                data["agents"].insert(-1, self.agent(boundary))
                self.write(data)
                self.cli(error="earlier wave completed")
                data["agents"][-2] = self.agent(boundary, completed=True)
                data["agents"].reverse()
                self.write(data)
                self.cli()
                data["agents"] = [agent for agent in data["agents"] if agent["id"] != 1]
                self.write(data)
                self.cli(error="agent 1 is incomplete")

    def test_require_complete_needs_all_hundred_current_reports(self):
        self.cli("--require-complete", error="require 100 completed reports")
        data = self.populated(99)
        self.write(data)
        self.cli("--require-complete", error="require 100 completed reports")
        data["agents"].append(self.agent(100))
        self.write(data)
        self.cli("--require-complete", error="require 100 completed reports")
        data["agents"][-1] = self.agent(100, completed=True)
        self.write(data)
        self.assertIn("100/100 completed", self.cli("--require-complete"))
        path = self.root / data["agents"][0]["report"]
        path.write_bytes(b"Mutated synthetic report.\n")
        self.cli("--require-complete", error="completed report changed")

    def test_symlink_reports_and_parent_directories_are_rejected(self):
        data = self.populated(1)
        self.write(data)
        report = self.root / data["agents"][0]["report"]
        target = self.root / "target.md"
        report.rename(target)
        self.symlink(report, target)
        self.cli(error="symlink campaign path")
        data["agents"][0].update(status="running", report_sha256=None)
        self.write(data)
        self.cli(error="symlink campaign path")
        report.unlink()
        parent = report.parent
        parent.rmdir()
        target_directory = self.root / "redirected-reports"
        target_directory.mkdir()
        self.symlink(parent, target_directory, directory=True)
        self.cli(error="symlink campaign path")

    def test_symlink_or_missing_ledger_is_rejected(self):
        target = self.root / "target-ledger.json"
        self.ledger.rename(target)
        self.symlink(self.ledger, target)
        self.cli(error="symlink campaign path")
        self.ledger.unlink()
        self.cli(error="missing regular continuation campaign ledger")
        project = self.ledger.parent
        redirected = self.root / "redirected-project"
        project.rename(redirected)
        self.symlink(project, redirected, directory=True)
        self.cli(error="symlink campaign path")


if __name__ == "__main__":
    unittest.main()
