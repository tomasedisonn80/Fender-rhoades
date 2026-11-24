"""Continuation-D integration checks against isolated, explicitly synthetic ledgers."""
from __future__ import annotations

import copy
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import stat
import subprocess
import sys
import tempfile
from types import SimpleNamespace
import unittest
from unittest.mock import patch

SCRIPT = Path(__file__).resolve().parents[2] / "tools/validate_continuation_d.py"
SPEC = importlib.util.spec_from_file_location("rhodes_continuation_d", SCRIPT)
assert SPEC and SPEC.loader
campaign = importlib.util.module_from_spec(SPEC)
with patch.object(sys, "path", [str(SCRIPT.parent), *sys.path]), patch(
        "argparse.ArgumentParser.parse_args", side_effect=AssertionError("CLI ran on import")):
    SPEC.loader.exec_module(campaign)


class ContinuationDTests(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory()
        self.addCleanup(temporary.cleanup)
        self.base = Path(temporary.name)
        self.root = self.base / "repository"
        self.ledger = self.root / "project/continuation-d-20261124.json"
        self.ledger.parent.mkdir(parents=True)
        (self.ledger.parent / "state.json").write_bytes(b"Unrelated sentinel.\n")

    def fixture(self, count, completed=(), kind="d"):
        records = []
        for number in range(1, count + 1):
            wave = (number - 1) // 20 + 1
            record = {"assignment": number, "wave": wave,
                      "agent_task_name": f"/root/w{wave}_{number:03d}_synthetic_fixture",
                      "status": "running"}
            if number in completed:
                report = f"docs/agents/reports/continuation-{kind}/w{wave}-{number:03d}.md"
                content = f"# Synthetic {kind.upper()} report {number}\nNo actual worker claim.\n".encode()
                path = self.root / report
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_bytes(content)
                record.update(status="completed", report=report,
                              sha256=hashlib.sha256(content).hexdigest())
            records.append(record)
        return {"schema_version": 1, "campaign": f"continuation-{kind}-20261124",
                "baseline_commit": "d" * 40, "requested_agents": 100, "requested_waves": 5,
                "concurrency_limit_including_integrator": 4, "status": "in_progress",
                "completed_agents": sum(record["status"] == "completed" for record in records),
                "agents": records}

    def write(self, data, kind="d"):
        path = self.ledger.parent / f"continuation-{kind}-20261124.json"
        path.write_text(json.dumps(data) + "\n", encoding="utf-8")

    def snapshot(self):
        result = {}
        for path in self.base.rglob("*"):
            info = path.lstat()
            content = (str(path.readlink()) if stat.S_ISLNK(info.st_mode) else
                       path.read_bytes() if stat.S_ISREG(info.st_mode) else None)
            result[path.relative_to(self.base).as_posix()] = (info.st_mode, info.st_mtime_ns, content)
        return result

    def cli(self, *arguments, error=None):
        before = self.snapshot()
        # The public script must work outside the checkout and leave fixture inputs intact.
        result = subprocess.run([sys.executable, "-B", str(SCRIPT), "--root", str(self.root),
                                 *arguments], cwd=self.base, capture_output=True, text=True,
                                timeout=10)
        self.assertEqual(self.snapshot(), before, "validator mutated its inputs")
        if error is None:
            self.assertEqual((result.returncode, result.stderr), (0, ""))
        else:
            self.assertEqual((result.returncode, result.stdout), (1, ""))
            self.assertIn("Continuation D error:", result.stderr)
            self.assertIn(error, result.stderr)
            self.assertNotIn("Traceback", result.stderr)
        return result.stdout

    def test_partial_d_and_c_defaults_are_independent(self):
        self.write(self.fixture(2, completed=(1,), kind="c"), kind="c")
        self.write(self.fixture(23, completed=(*range(1, 21), 22)))
        self.assertEqual(campaign.validate(self.root), (23, 21))
        self.assertEqual(campaign.validate_campaign(self.root), (2, 1))
        # A different campaign's broken metadata must not affect the selected D ledger.
        (self.ledger.parent / "continuation-c-20261124.json").write_bytes(b"{not JSON")
        self.assertIn("23/100 recorded; 21/100 completed", self.cli())
        self.cli("--require-complete", error="campaign not complete")

    def test_cross_campaign_substitution_and_missing_d_do_not_fall_back_to_c(self):
        original = self.fixture(1, completed=(1,))
        other = self.fixture(1, completed=(1,), kind="c")
        self.write(other, kind="c")
        self.write(other)
        self.cli(error="campaign must be continuation-d-20261124")
        data = copy.deepcopy(original)
        data["agents"][0].update(report=other["agents"][0]["report"],
                                 sha256=other["agents"][0]["sha256"])
        self.write(data)
        self.cli(error="incorrect canonical report path")
        self.write(original)
        (self.root / original["agents"][0]["report"]).write_bytes(
            (self.root / other["agents"][0]["report"]).read_bytes())
        self.cli(error="report SHA256 mismatch")
        self.ledger.unlink()
        self.cli(error="continuation-d-20261124.json")

    def test_next_wave_requires_every_previous_completed_report(self):
        for count, missing in ((21, 20), (41, 21), (81, 80)):
            with self.subTest(count=count, missing=missing):
                data = self.fixture(count, completed=set(range(1, count + 1)) - {missing})
                self.write(data)
                self.cli(error="requires all 20 reports completed in every earlier wave")
        self.write(self.fixture(21, completed=range(1, 21)))
        self.assertIn("21/100 recorded; 20/100 completed", self.cli())

    def test_completion_needs_actual_report_count_and_explicit_status(self):
        data = self.fixture(100, completed=range(1, 100))
        self.write(data)
        self.assertIn("100/100 recorded; 99/100 completed", self.cli())
        self.cli("--require-complete", error="campaign not complete")
        data["completed_agents"] = 100
        self.write(data)
        self.cli(error="completed_agents does not match")
        data.update(completed_agents=99, status="complete")
        self.write(data)
        self.cli(error="complete status requires all 100")
        data = self.fixture(100, completed=range(1, 101))
        self.write(data)
        self.cli()
        self.cli("--require-complete", error="campaign not complete")
        data["status"] = "complete"
        self.write(data)
        self.assertIn("100/100 completed", self.cli("--require-complete"))

    def test_assignment_identity_cannot_be_reused_or_reordered(self):
        original = self.fixture(2, completed=(1, 2))
        for field in ("agent_task_name", "report"):
            data = copy.deepcopy(original)
            data["agents"][1][field] = data["agents"][0][field]
            self.write(data)
            self.cli(error="duplicate")
        data = copy.deepcopy(original)
        data["agents"].reverse()
        self.write(data)
        self.cli(error="consecutive integers")
        for name in ("/root/w2_001_fixture", "/root/w1_002_fixture", "/root/w1_001_fixture\n"):
            data = copy.deepcopy(original)
            data["agents"][0]["agent_task_name"] = name
            self.write(data)
            self.cli(error="incorrect canonical agent_task_name")

    def test_strict_types_and_status_cannot_change_the_campaign_contract(self):
        original = self.fixture(1)
        for field, value in (("schema_version", True), ("requested_agents", 99),
                             ("requested_waves", 4), ("concurrency_limit_including_integrator", 3),
                             ("completed_agents", False), ("status", "completed"),
                             ("baseline_commit", "d" * 39), ("extra", "ignored?")):
            with self.subTest(field=field, value=value):
                data = copy.deepcopy(original)
                data[field] = value
                self.write(data)
                self.cli(error="error:")
        for field, value in (("assignment", 1.0), ("wave", True), ("status", "complete"),
                             ("agent_task_name", None), ("report", "uncompleted-report.md")):
            with self.subTest(field=field, value=value):
                data = copy.deepcopy(original)
                data["agents"][0][field] = value
                self.write(data)
                self.cli(error="error:")

    def test_ambiguous_json_cannot_hide_record_status_or_counts(self):
        content = json.dumps(self.fixture(1))
        malformed = [
            content.replace('"status": "running"', '"status": "completed", "status": "running"'),
            content.replace('"completed_agents": 0', '"completed_agents": 1, "completed_agents": 0'),
        ]
        for candidate in malformed:
            self.ledger.write_text(candidate, encoding="utf-8")
            self.cli(error="duplicate JSON key")
        for number in ("NaN", "Infinity", "1e400"):
            self.ledger.write_text(content.replace('"requested_agents": 100',
                                                   f'"requested_agents": {number}'), encoding="utf-8")
            self.cli(error="nonfinite JSON number")
        self.ledger.write_bytes(b"\xff")
        self.cli(error="error:")

    def test_altered_bytes_and_even_matching_external_hashes_are_rejected(self):
        original = self.fixture(1, completed=(1,))
        self.write(original)
        report = self.root / original["agents"][0]["report"]
        report.write_bytes(report.read_bytes() + b"Changed after registration.\n")
        self.cli(error="report SHA256 mismatch")
        outside = self.base / "outside-report.md"
        outside.write_bytes(b"Matching digest does not authorize an outside path.\n")
        for path in (str(outside), "../outside-report.md", "docs/agents/reports/continuation-d/../w1-001.md"):
            data = copy.deepcopy(original)
            data["agents"][0].update(report=path, sha256=hashlib.sha256(outside.read_bytes()).hexdigest())
            self.write(data)
            self.cli(error="incorrect canonical report path")
        data = copy.deepcopy(original)
        data["agents"][0]["sha256"] = "A" * 64
        self.write(data)
        self.cli(error="sha256 must be 64 lowercase hexadecimal digits")

    def test_linked_reports_directories_and_ledgers_are_rejected(self):
        data = self.fixture(1, completed=(1,))
        self.write(data)
        report = self.root / data["agents"][0]["report"]
        outside = self.base / "outside-report.md"
        report.rename(outside)
        try:
            report.symlink_to(outside)
        except (OSError, NotImplementedError) as exc:
            self.skipTest(f"symlinks unavailable: {exc}")
        self.cli(error="symlink or reparse")
        report.unlink()
        outside.rename(report)
        target = self.base / "outside-reports"
        report.parent.rename(target)
        report.parent.symlink_to(target, target_is_directory=True)
        self.cli(error="symlink or reparse")
        report.parent.unlink()
        target.rename(report.parent)
        self.ledger.rename(self.base / "outside-ledger.json")
        self.ledger.symlink_to(self.base / "outside-ledger.json")
        self.cli(error="symlink or reparse")

    def test_reparse_attribute_rejects_a_report_parent_before_report_read(self):
        data = self.fixture(1, completed=(1,))
        self.write(data)
        # The validator resolves its root before lstat. Temporary-directory names
        # can change on resolution; force a different spelling on every platform.
        root_argument = self.root / ".." / self.root.name
        resolved_root = root_argument.resolve(strict=True)
        self.assertNotEqual(root_argument, resolved_root)
        self.assertTrue(root_argument.samefile(resolved_root))
        parent = (resolved_root / data["agents"][0]["report"]).parent
        ledger = self.ledger.resolve(strict=True)
        original_lstat, original_read = Path.lstat, Path.read_bytes
        reads, reparse_paths = [], []

        def reparse_lstat(path, *args, **kwargs):
            info = original_lstat(path, *args, **kwargs)
            if path == parent:
                reparse_paths.append(path)
                return SimpleNamespace(st_mode=info.st_mode, st_file_attributes=0x400)
            return info

        def record_read(path):
            reads.append(path)
            return original_read(path)

        # Portable attribute simulation exercises the Windows guard without a native claim.
        with patch.object(stat, "FILE_ATTRIBUTE_REPARSE_POINT", 0x400, create=True), \
                patch.object(Path, "lstat", reparse_lstat), patch.object(Path, "read_bytes", record_read):
            with self.assertRaisesRegex(ValueError, "symlink or reparse"):
                campaign.validate(root_argument)
        self.assertEqual(reparse_paths, [parent], "synthetic reparse parent was not checked exactly once")
        self.assertEqual(reads, [ledger])

    def test_missing_and_nonregular_reports_fail_without_blocking(self):
        data = self.fixture(1, completed=(1,))
        self.write(data)
        report = self.root / data["agents"][0]["report"]
        report.unlink()
        self.cli(error="w1-001.md")
        report.mkdir()
        self.cli(error="not a regular file")
        if hasattr(os, "mkfifo"):
            report.rmdir()
            os.mkfifo(report)
            self.cli(error="not a regular file")


if __name__ == "__main__":
    unittest.main()
