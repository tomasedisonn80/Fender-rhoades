"""Development ledger regressions using temporary synthetic assignments only."""
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
SPEC = importlib.util.spec_from_file_location(
    "rhodes_development_campaign", ROOT / "tools/development_campaign.py")
assert SPEC and SPEC.loader
campaign = importlib.util.module_from_spec(SPEC)
with patch("argparse.ArgumentParser.parse_args", side_effect=AssertionError("CLI ran on import")):
    SPEC.loader.exec_module(campaign)


class DevelopmentCampaignTests(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory()
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name)
        replacement = patch.object(campaign, "ROOT", self.root)
        replacement.start()
        self.addCleanup(replacement.stop)
        self.assert_success("init")

    def cli(self, *arguments):
        output, errors = io.StringIO(), io.StringIO()
        with patch.object(sys, "argv", ["development_campaign.py", *arguments]), \
                redirect_stdout(output), redirect_stderr(errors):
            result = campaign.main()
        return result, output.getvalue(), errors.getvalue()

    def assert_success(self, *arguments):
        result, output, errors = self.cli(*arguments)
        self.assertEqual((result, errors), (0, ""), output)
        return output

    def snapshot(self):
        return tuple((self.root / path).read_bytes()
                     for path in (campaign.LEDGER, campaign.INDEX))

    def assert_rejected(self, *arguments, message):
        before = self.snapshot()
        result, output, errors = self.cli(*arguments)
        self.assertEqual(result, 1, output)
        self.assertIn(message, errors)
        self.assertEqual(self.snapshot(), before)

    def write_report(self, number):
        path = self.root / campaign.report(f"A{number:03d}")
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(f"# Synthetic test fixture {number}\nNot an actual agent.\n", encoding="utf-8")
        return path

    def populate(self, count):
        """Construct temporary synthetic history without touching the real ledger."""
        data = campaign.read()
        for number in range(1, count + 1):
            identifier = f"A{number:03d}"
            wave = campaign.wave(identifier)
            path = self.write_report(number)
            data["agents"].append({"id": identifier, "wave": wave,
                                   "task_name": f"/root/w{wave}_a{number:03d}",
                                   "scope": "Synthetic isolated fixture", "status": "completed",
                                   "report": campaign.report(identifier),
                                   "report_sha256": hashlib.sha256(path.read_bytes()).hexdigest()})
        campaign.atomic(campaign.LEDGER, json.dumps(data) + "\n")

    def test_init_is_non_destructive_and_import_does_not_run_cli(self):
        self.assert_rejected("init", message="already exists")
        self.assert_success("validate")
        self.assert_success("sync", "--check")
        self.assertFalse((self.root / "project/agent-campaign.json").exists())
        self.assertFalse((self.root / "docs/agents/CAMPAIGN.md").exists())

    def test_actual_name_wave_and_identifier_mapping_are_checked(self):
        for identifier, name in (("A000", "/root/w1_a000"), ("A101", "/root/w6_a101"),
                                 ("A001", "/root/another_agent"), ("A001", "/root/w2_a001")):
            with self.subTest(identifier=identifier, name=name):
                self.assert_rejected("register", identifier, name, "Fixture", message="campaign error")
        self.assert_success("register", "A001", "/root/w1_a001", "Fixture")
        self.assert_rejected("register", "A001", "/root/w1_a001", "Fixture", message="duplicate")

    def test_later_wave_requires_twenty_completed_previous_reports(self):
        self.populate(19)
        self.assert_rejected("register", "A021", "/root/w2_a021", "Fixture", message="earlier waves")
        self.assert_success("register", "A020", "/root/w1_a020", "Fixture")
        self.assert_rejected("register", "A021", "/root/w2_a021", "Fixture", message="earlier waves")
        self.write_report(20)
        self.assert_success("complete", "A020")
        self.assert_success("register", "A021", "/root/w2_a021", "Fixture")
        self.assert_rejected("register", "A041", "/root/w3_a041", "Fixture", message="earlier waves")

    def test_complete_is_atomic_and_cannot_rehash_changed_report(self):
        self.assert_success("register", "A001", "/root/w1_a001", "Fixture")
        self.assert_rejected("complete", "A001", message="missing regular report")
        path = self.write_report(1)
        self.assert_rejected("complete", "A001", "A002", message="unknown assignment")
        self.assert_success("complete", "A001")
        self.assert_rejected("complete", "A001", message="already completed")
        path.write_text("# Changed fixture\n", encoding="utf-8")
        self.assert_rejected("complete", "A001", message="completed report changed")
        self.assert_rejected("validate", message="completed report changed")

    def test_strict_completion_requires_all_hundred_and_current_hashes(self):
        self.populate(99)
        self.assert_rejected("validate", "--require-complete", message="not complete")
        self.assert_success("register", "A100", "/root/w5_a100", "Fixture")
        self.assert_rejected("validate", "--require-complete", message="not complete")
        self.write_report(100)
        self.assert_success("complete", "A100")
        self.assertIn("100/100", self.assert_success("validate", "--require-complete"))
        self.assert_success("sync", "--check")

    def test_generated_index_drift_is_detected_and_recoverable(self):
        before = (self.root / campaign.LEDGER).read_bytes()
        (self.root / campaign.INDEX).write_text("# Stale fixture\n", encoding="utf-8")
        self.assert_rejected("sync", "--check", message="index is stale")
        self.assert_success("sync")
        self.assertEqual((self.root / campaign.LEDGER).read_bytes(), before)
        self.assert_success("sync", "--check")

    def test_malformed_or_duplicate_json_is_rejected_without_writes(self):
        ledger = self.root / campaign.LEDGER
        for content in ('{"schema_version":1,"schema_version":1}', '{"value": NaN}', '[]'):
            with self.subTest(content=content):
                ledger.write_text(content, encoding="utf-8")
                self.assert_rejected("validate", message="campaign error")

    def test_schema_and_report_path_cannot_bypass_wave_or_confinement(self):
        self.assert_success("register", "A001", "/root/w1_a001", "Fixture")
        original = campaign.read()
        for field, value in (("wave", True), ("wave", 2), ("report", "../../escape.md"),
                             ("scope", " "), ("report_sha256", "0" * 64)):
            with self.subTest(field=field):
                data = json.loads(json.dumps(original))
                data["agents"][0][field] = value
                campaign.atomic(campaign.LEDGER, json.dumps(data))
                self.assert_rejected("validate", message="campaign error")

    def test_symlink_report_is_rejected(self):
        self.assert_success("register", "A001", "/root/w1_a001", "Fixture")
        target = self.root / "external-report.md"
        target.write_text("# Synthetic symlink target\n", encoding="utf-8")
        path = self.root / campaign.report("A001")
        path.parent.mkdir(parents=True, exist_ok=True)
        try:
            path.symlink_to(target)
        except (OSError, NotImplementedError):
            self.skipTest("symlink creation is unavailable")
        self.assert_rejected("complete", "A001", message="unsafe campaign path")


if __name__ == "__main__":
    unittest.main()
