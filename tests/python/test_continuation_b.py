"""Integrity checks against synthetic temporary ledgers, never the live ledger."""
from __future__ import annotations

from contextlib import redirect_stderr, redirect_stdout
import copy
import hashlib
import importlib.util
import io
import json
import os
from pathlib import Path
import stat
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[2]
SCRIPT = ROOT / "tools/validate_continuation_b.py"
SPEC = importlib.util.spec_from_file_location("rhodes_continuation_b", SCRIPT)
assert SPEC and SPEC.loader
campaign = importlib.util.module_from_spec(SPEC)
with patch("argparse.ArgumentParser.parse_args", side_effect=AssertionError("CLI ran on import")):
    SPEC.loader.exec_module(campaign)


class ContinuationBTests(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory()
        self.addCleanup(temporary.cleanup)
        self.base = Path(temporary.name)
        self.root = self.base / "fixture-root"
        self.path = self.root / "project/continuation-b-20261124.json"
        self.path.parent.mkdir(parents=True)
        (self.path.parent / "state.json").write_text("Unrelated sentinel.\n", encoding="utf-8")
        self.write(self.fixture(0))

    def fixture(self, count, completed=None):
        completed = count if completed is None else completed
        records = []
        for number in range(1, count + 1):
            wave = (number - 1) // 20 + 1
            record = {"assignment": number, "wave": wave,
                      "agent_task_name": f"/root/w{wave}_{number:03d}_synthetic_fixture",
                      "status": "running"}
            if number <= completed:
                report = f"docs/agents/reports/20261124b-w{wave}-{number:03d}-synthetic-fixture.md"
                content = f"# Synthetic report {number}\nThis fixture is not agent execution.\n".encode()
                path = self.root / report
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_bytes(content)
                record.update(status="completed", report=report,
                              sha256=hashlib.sha256(content).hexdigest())
            records.append(record)
        return {"schema_version": 1, "campaign": "continuation-b-20261124",
                "baseline_commit": "a17e665454c1daa1c1aff3d71b790b28fe145e12",
                "requested_agents": 100, "requested_waves": 5,
                "concurrency_limit_including_integrator": 4,
                "status": "complete" if completed == 100 else "in_progress",
                "agents": records, "completed_agents": completed}

    def write(self, data):
        self.path.write_text(json.dumps(data) + "\n", encoding="utf-8")

    def snapshot(self):
        result = {}
        for path in self.base.rglob("*"):
            info = path.lstat()
            content = None
            if stat.S_ISLNK(info.st_mode):
                content = str(path.readlink())
            elif stat.S_ISREG(info.st_mode):
                content = path.read_bytes()
            result[path.relative_to(self.base).as_posix()] = (info.st_mode, info.st_mtime_ns, content)
        return result

    def cli(self, *arguments, error=None):
        before = self.snapshot()
        output, errors = io.StringIO(), io.StringIO()
        with redirect_stdout(output), redirect_stderr(errors):
            result = campaign.main(["--root", str(self.root), *arguments])
        self.assertEqual(self.snapshot(), before, "validator changed an input or created a file")
        if error is None:
            self.assertEqual((result, errors.getvalue()), (0, ""), output.getvalue())
        else:
            self.assertEqual(result, 1)
            self.assertEqual(output.getvalue(), "")
            self.assertIn(error, errors.getvalue())
        return output.getvalue()

    def symlink(self, link, target, *, directory=False):
        try:
            link.symlink_to(target, target_is_directory=directory)
        except (OSError, NotImplementedError) as exc:
            self.skipTest(f"symlinks unavailable: {exc}")

    def test_partial_records_and_custom_ledger_are_read_only(self):
        self.assertIn("0/100 recorded", self.cli())
        data = self.fixture(21, completed=20)
        self.write(data)
        result = json.loads(self.cli("--json"))
        self.assertEqual((result["recorded_agents"], result["completed_agents"]), (21, 20))
        self.assertEqual(result["completed_by_wave"], [20, 0, 0, 0, 0])
        self.assertEqual(result["ledger_sha256"], hashlib.sha256(self.path.read_bytes()).hexdigest())
        self.assertIn("do not prove actual agent execution", result["limitation"])
        self.assertFalse((self.root / "docs/agents/reports/20261124b-w2-021-synthetic-fixture.md").exists())
        custom = self.root / "fixture.json"
        self.path.rename(custom)
        self.assertIn("21/100 recorded", self.cli("--ledger", "fixture.json"))

    def test_completion_requires_exact_hundred_and_twenty_per_wave(self):
        self.cli("--require-complete", error="require 100 completed reports")
        data = self.fixture(100, completed=99)
        self.write(data)
        self.cli("--require-complete", error="require 100 completed reports")
        data["status"] = "complete"
        self.write(data)
        self.cli(error="ledger status must be in_progress")
        data = self.fixture(100)
        self.write(data)
        result = json.loads(self.cli("--require-complete", "--json"))
        self.assertEqual(result["completed_by_wave"], [20, 20, 20, 20, 20])
        self.assertEqual(result["completed_waves"], 5)
        data["status"] = "in_progress"
        self.write(data)
        self.cli(error="ledger status must be complete")

    def test_top_level_schema_types_counts_and_baseline_are_strict(self):
        original = self.fixture(1)
        cases = [(key, value) for key in ("schema_version", "requested_agents", "requested_waves",
                                          "concurrency_limit_including_integrator", "completed_agents")
                 for value in (True, 1.0, "1", None, -1)]
        cases.extend((("unexpected", 1), ("campaign", "continuation-campaign-20261123"),
                      ("status", "completed"), ("status", []), ("agents", {}),
                      ("completed_agents", 0), ("completed_agents", 2)))
        cases.extend(("baseline_commit", value) for value in (True, "a" * 39, "A" * 40,
                                                              "g" * 40, "a" * 41))
        for key, value in cases:
            with self.subTest(key=key, value=value):
                data = copy.deepcopy(original)
                data[key] = value
                self.write(data)
                self.cli(error="Continuation B error")
        for key in original:
            with self.subTest(missing=key):
                data = copy.deepcopy(original)
                del data[key]
                self.write(data)
                self.cli(error="ledger fields")

    def test_duplicate_keys_nonfinite_numbers_and_malformed_json_fail(self):
        content = json.dumps(self.fixture(1))
        cases = [("{", "Continuation B error"), ("[]", "ledger fields"),
                 (content.replace('"schema_version": 1,', '"schema_version": 1, "schema_version": 1,'),
                  "duplicate JSON key"),
                 (content.replace('"assignment": 1,', '"assignment": 1, "assignment": 1,'),
                  "duplicate JSON key")]
        cases.extend((content.replace('"requested_agents": 100', f'"requested_agents": {value}'),
                      "nonfinite JSON number") for value in ("NaN", "Infinity", "-Infinity", "1e400"))
        for malformed, message in cases:
            with self.subTest(content=malformed[:60]):
                self.path.write_text(malformed, encoding="utf-8")
                self.cli(error=message)
        self.path.write_bytes(b"\xff")
        self.cli(error="Continuation B error")
        self.path.write_text("[" * 2000 + "]" * 2000, encoding="utf-8")
        self.cli(error="Continuation B error")

    def test_record_fields_follow_running_and_completed_protocol(self):
        data = self.fixture(1, completed=0)
        self.write(data)
        self.cli()
        for extra in ({"report": None}, {"sha256": None}, {"followup": "unrecognized"}):
            bad = copy.deepcopy(data)
            bad["agents"][0].update(extra)
            self.write(bad)
            self.cli(error="incorrect fields for running")
        for status in ("pending", "complete", None, []):
            bad = copy.deepcopy(data)
            bad["agents"][0]["status"] = status
            self.write(bad)
            self.cli(error="status must be running or completed")
        for value in ([], None, True):
            data["agents"] = [value]
            self.write(data)
            self.cli(error="invalid record fields")
        data = self.fixture(1)
        for missing in ("report", "sha256", "assignment"):
            bad = copy.deepcopy(data)
            del bad["agents"][0][missing]
            self.write(bad)
            self.cli(error="fields")

    def test_assignment_order_types_wave_and_identity_cannot_be_spoofed(self):
        original = self.fixture(2, completed=0)
        for assignment in (True, 1.0, 0, 2, 101):
            data = copy.deepcopy(original)
            data["agents"][0]["assignment"] = assignment
            self.write(data)
            self.cli(error="consecutive integers")
        for wave in (True, 1.0, 2):
            data = copy.deepcopy(original)
            data["agents"][0]["wave"] = wave
            self.write(data)
            self.cli(error="incorrect wave")
        data = copy.deepcopy(original)
        data["agents"].reverse()
        self.write(data)
        self.cli(error="consecutive integers")
        data = copy.deepcopy(original)
        data["agents"][1]["assignment"] = 1
        self.write(data)
        self.cli(error="consecutive integers")
        data = copy.deepcopy(original)
        data["agents"][1]["agent_task_name"] = data["agents"][0]["agent_task_name"]
        self.write(data)
        self.cli(error="duplicate agent_task_name")
        for name in ("/root/w1_1_fixture", "/root/w2_001_fixture", "/root/w1_002_fixture",
                     "/root/w1_001_", "/root/w1_001_A", "/root/w1_001_fixture\n", "/root/other"):
            data = copy.deepcopy(original)
            data["agents"][0]["agent_task_name"] = name
            self.write(data)
            self.cli(error="canonical agent_task_name")

    def test_report_path_must_match_assignment_and_task_slug(self):
        original = self.fixture(1)
        for report in ("../escape.md", "/tmp/escape.md", "docs/agents/../escape.md",
                       "docs\\agents\\reports\\escape.md",
                       "docs/agents/reports/20261124b-w1-002-synthetic-fixture.md",
                       "docs/agents/reports/20261124b-w1-001-other.md", None):
            data = copy.deepcopy(original)
            data["agents"][0]["report"] = report
            self.write(data)
            self.cli(error="incorrect report path")

    def test_reports_require_valid_hash_and_current_nonempty_utf8_text(self):
        data = self.fixture(1)
        for digest in (None, True, "a" * 63, "G" * 64, "A" * 64):
            bad = copy.deepcopy(data)
            bad["agents"][0]["sha256"] = digest
            self.write(bad)
            self.cli(error="64 lowercase hexadecimal")
        report = self.root / data["agents"][0]["report"]
        self.write(data)
        report.write_bytes(b"A changed but nonempty synthetic report.\n")
        self.cli(error="report SHA256 mismatch")
        for content, message in ((b"", "nonempty text"), (b" \n\t", "nonempty text"),
                                 ("\u2003\n".encode(), "nonempty text"),
                                 (b"\x00", "NUL bytes"), (b"\xff", "UTF-8 text")):
            report.write_bytes(content)
            data["agents"][0]["sha256"] = hashlib.sha256(content).hexdigest()
            self.write(data)
            self.cli(error=message)

    def test_missing_or_directory_reports_are_rejected(self):
        data = self.fixture(1)
        self.write(data)
        report = self.root / data["agents"][0]["report"]
        report.unlink()
        self.cli(error="missing regular file")
        report.mkdir()
        self.cli(error="not a regular file")

    @unittest.skipUnless(hasattr(os, "mkfifo"), "FIFO unavailable")
    def test_fifo_report_is_rejected_without_blocking(self):
        data = self.fixture(1)
        self.write(data)
        report = self.root / data["agents"][0]["report"]
        report.unlink()
        os.mkfifo(report)
        before = self.snapshot()
        result = subprocess.run([sys.executable, "-B", str(SCRIPT), "--root", str(self.root)],
                                text=True, capture_output=True, timeout=2, check=False)
        self.assertEqual(result.returncode, 1)
        self.assertIn("not a regular file", result.stderr)
        self.assertNotIn("Traceback", result.stderr)
        self.assertEqual(self.snapshot(), before)

    def test_symlink_reports_and_parent_directories_are_rejected(self):
        data = self.fixture(1)
        self.write(data)
        report = self.root / data["agents"][0]["report"]
        outside = self.base / "outside-report.md"
        report.rename(outside)
        self.symlink(report, outside)
        self.cli(error="symlink or reparse")
        report.unlink()
        outside.rename(report)
        target = self.base / "outside-reports"
        report.parent.rename(target)
        self.symlink(report.parent, target, directory=True)
        self.cli(error="symlink or reparse")

    def test_ledger_path_cannot_escape_root_or_follow_symlinks(self):
        for relative in ("../fixture.json", str(self.path), "project/../fixture.json",
                         "./project/continuation-b-20261124.json", "project\\fixture.json"):
            self.cli("--ledger", relative, error="unsafe root-relative path")
        outside = self.base / "outside-ledger.json"
        self.path.rename(outside)
        self.symlink(self.path, outside)
        self.cli(error="symlink or reparse")
        self.path.unlink()
        self.cli(error="missing regular file")

    def test_file_and_record_limits_are_enforced(self):
        data = self.fixture(0)
        data["agents"] = [None] * 101
        self.write(data)
        self.cli(error="at most 100 assignments")
        with self.path.open("wb") as output:
            output.truncate(campaign.MAX_LEDGER_BYTES + 1)
        self.cli(error="byte limit")
        data = self.fixture(1)
        self.write(data)
        report = self.root / data["agents"][0]["report"]
        with report.open("wb") as output:
            output.truncate(campaign.MAX_REPORT_BYTES + 1)
        self.cli(error="byte limit")


if __name__ == "__main__":
    unittest.main()
