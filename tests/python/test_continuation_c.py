"""Continuation-C integrity checks using isolated synthetic ledger/report files."""
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
import tempfile
import unittest
from unittest.mock import patch

SCRIPT = Path(__file__).resolve().parents[2] / "tools/validate_continuation_c.py"
SPEC = importlib.util.spec_from_file_location("rhodes_continuation_c", SCRIPT)
assert SPEC and SPEC.loader
campaign = importlib.util.module_from_spec(SPEC)
with patch("argparse.ArgumentParser.parse_args", side_effect=AssertionError("CLI ran on import")):
    SPEC.loader.exec_module(campaign)


class ContinuationCTests(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory()
        self.addCleanup(temporary.cleanup)
        self.base = Path(temporary.name)
        self.root = self.base / "repository"
        self.ledger = self.root / "project/continuation-c-20261124.json"
        self.ledger.parent.mkdir(parents=True)
        (self.ledger.parent / "state.json").write_text("Unrelated sentinel.\n", encoding="utf-8")

    def fixture(self, count=0, completed=()):
        records = []
        for number in range(1, count + 1):
            wave = (number - 1) // 20 + 1
            record = {"assignment": number, "wave": wave,
                      "agent_task_name": f"/root/w{wave}_{number:03d}_synthetic_fixture",
                      "status": "running"}
            if number in completed:
                report = f"docs/agents/reports/continuation-c/w{wave}-{number:03d}.md"
                content = f"# Synthetic report {number}\nNot actual agent execution.\n".encode()
                path = self.root / report
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_bytes(content)
                record.update(status="completed", report=report,
                              sha256=hashlib.sha256(content).hexdigest())
            records.append(record)
        return {"schema_version": 1, "campaign": "continuation-c-20261124",
                "baseline_commit": "a" * 40, "status": "in_progress",
                "requested_agents": 100, "requested_waves": 5,
                "concurrency_limit_including_integrator": 4,
                "completed_agents": sum(record["status"] == "completed" for record in records),
                "agents": records}

    def write(self, data):
        self.ledger.write_text(json.dumps(data) + "\n", encoding="utf-8")

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
        output, errors = io.StringIO(), io.StringIO()
        with redirect_stdout(output), redirect_stderr(errors):
            result = campaign.main(["--root", str(self.root), *arguments])
        self.assertEqual(self.snapshot(), before, "validator mutated its inputs")
        if error is None:
            self.assertEqual((result, errors.getvalue()), (0, ""))
        else:
            self.assertEqual((result, output.getvalue()), (1, ""))
            self.assertIn(error, errors.getvalue())
        return output.getvalue()

    def test_partial_wave_and_out_of_order_finishes_are_valid(self):
        self.write(self.fixture())
        self.assertIn("0/100 recorded; 0/100 completed", self.cli())
        # Three workers in wave two can finish in any order; no report for 21/23 is required.
        data = self.fixture(23, completed=(*range(1, 21), 22))
        self.write(data)
        self.assertIn("23/100 recorded; 21/100 completed", self.cli())
        self.assertFalse((self.root / "docs/agents/reports/continuation-c/w2-021.md").exists())
        self.cli("--require-complete", error="campaign not complete")

    def test_each_wave_waits_for_every_earlier_report(self):
        for missing in (1, 20, 21, 40, 60, 80):
            with self.subTest(missing=missing):
                count = ((missing - 1) // 20 + 1) * 20 + 1
                data = self.fixture(count, completed=set(range(1, count)) - {missing})
                self.write(data)
                self.cli(error="requires all 20 reports completed in every earlier wave")

    def test_completion_status_and_counts_cannot_be_forged(self):
        data = self.fixture(100, completed=range(1, 100))
        self.write(data)
        self.cli()
        self.cli("--require-complete", error="campaign not complete")
        data["status"] = "complete"
        self.write(data)
        self.cli(error="complete status requires all 100")
        data = self.fixture(100, completed=range(1, 101))
        self.write(data)
        self.cli()  # Reports may finish before the integrator marks the ledger complete.
        self.cli("--require-complete", error="campaign not complete")
        data["status"] = "complete"
        self.write(data)
        self.assertIn("100/100 completed", self.cli("--require-complete"))
        data["completed_agents"] = 99
        self.write(data)
        self.cli(error="completed_agents does not match")

    def test_schema_and_integer_types_are_strict(self):
        original = self.fixture(1, completed=(1,))
        cases = [(key, value) for key in ("schema_version", "requested_agents", "requested_waves",
                                          "concurrency_limit_including_integrator", "completed_agents")
                 for value in (True, 1.0, "1", None, -1)]
        cases += [("extra", 1), ("campaign", "continuation-b-20261124"), ("status", []),
                  ("agents", {}), ("agents", [None] * 101), ("baseline_commit", "g" * 40)]
        for key, value in cases:
            with self.subTest(key=key, value=value):
                data = copy.deepcopy(original)
                data[key] = value
                self.write(data)
                self.cli(error="Continuation C error")
        for key in original:
            data = copy.deepcopy(original)
            del data[key]
            self.write(data)
            self.cli(error="ledger fields")

    def test_json_ambiguity_and_nonfinite_numbers_are_rejected(self):
        content = json.dumps(self.fixture(1))
        cases = [("{", "Continuation C error"), ("[]", "ledger fields")]
        for key in ("schema_version", "assignment"):
            cases.append((content.replace(f'"{key}": 1,', f'"{key}": 1, "{key}": 1,'),
                          "duplicate JSON key"))
        cases += [(content.replace('"requested_agents": 100', f'"requested_agents": {value}'),
                   "nonfinite JSON number") for value in ("NaN", "Infinity", "-Infinity", "1e400")]
        for malformed, message in cases:
            with self.subTest(content=malformed[:50]):
                self.ledger.write_text(malformed, encoding="utf-8")
                self.cli(error=message)
        self.ledger.write_bytes(b"\xff")
        self.cli(error="Continuation C error")

    def test_assignment_identity_order_and_protocol_are_enforced(self):
        original = self.fixture(2, completed=(1, 2))
        for key, values in {"assignment": (True, 1.0, 0, 2), "wave": (True, 1.0, 2),
                            "agent_task_name": (None, "/root/w1_1_fixture", "/root/w2_001_fixture",
                                                "/root/w1_002_fixture", "/root/w1_001_fixture\n"),
                            "status": ([], "pending", "running")}.items():
            for value in values:
                with self.subTest(key=key, value=value):
                    data = copy.deepcopy(original)
                    data["agents"][0][key] = value
                    self.write(data)
                    self.cli(error="Continuation C error")
        for key in ("agent_task_name", "report"):
            data = copy.deepcopy(original)
            data["agents"][1][key] = data["agents"][0][key]
            self.write(data)
            self.cli(error="duplicate")
        data = copy.deepcopy(original)
        data["agents"].reverse()
        self.write(data)
        self.cli(error="consecutive integers")
        data = self.fixture(1)
        data["agents"][0]["sha256"] = None
        self.write(data)
        self.cli(error="incorrect fields for running")
        data = copy.deepcopy(original)
        del data["agents"][0]["sha256"]
        self.write(data)
        self.cli(error="incorrect fields for completed")

    def test_report_tampering_paths_and_digests_are_rejected(self):
        original = self.fixture(1, completed=(1,))
        for key, values in {"report": ("../escape.md", "/tmp/escape.md",
                                       "docs/agents/reports/continuation-c/../w1-001.md",
                                       "docs\\agents\\reports\\continuation-c\\w1-001.md",
                                       "docs/agents/reports/continuation-c/w1-002.md", None),
                            "sha256": (None, True, "a" * 63, "A" * 64)}.items():
            for value in values:
                with self.subTest(key=key, value=value):
                    data = copy.deepcopy(original)
                    data["agents"][0][key] = value
                    self.write(data)
                    self.cli(error="Continuation C error")
        self.write(original)
        (self.root / original["agents"][0]["report"]).write_text("Changed report.\n", encoding="utf-8")
        self.cli(error="report SHA256 mismatch")

    def test_missing_directory_and_fifo_reports_are_rejected(self):
        data = self.fixture(1, completed=(1,))
        self.write(data)
        report = self.root / data["agents"][0]["report"]
        report.unlink()
        self.cli(error="Continuation C error")
        report.mkdir()
        self.cli(error="not a regular file")
        if hasattr(os, "mkfifo"):
            report.rmdir()
            os.mkfifo(report)
            self.cli(error="not a regular file")

    def test_symlink_reports_parent_directories_and_ledger_are_rejected(self):
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
        self.ledger.rename(self.base / "outside-ledger.json")
        self.ledger.symlink_to(self.base / "outside-ledger.json")
        self.cli(error="symlink or reparse")


if __name__ == "__main__":
    unittest.main()
