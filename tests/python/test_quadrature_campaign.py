"""Campaign corruption checks using isolated synthetic metadata, never real agents."""
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
    "rhodes_quadrature_campaign", ROOT / "tools/quadrature_campaign.py")
assert SPEC and SPEC.loader
campaign = importlib.util.module_from_spec(SPEC)
with patch("argparse.ArgumentParser.parse_args", side_effect=AssertionError("CLI ran on import")):
    SPEC.loader.exec_module(campaign)


class QuadratureCampaignTests(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory()
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name)
        self.populate(40)
        self.assert_success("sync")

    def populate(self, count):
        data = {"schema_version": 1, "baseline_commit": "6" * 40,
                "requested_assignments": 100, "requested_waves": 5,
                "assignments_per_wave": 20, "maximum_concurrent_workers": 3,
                "scope": "Temporary synthetic fixture, not actual worker history.",
                "completed_waves": list(range(1, count // 20 + 1)), "assignments": []}
        for number in range(1, count + 1):
            wave = (number - 1) // 20 + 1
            report = f"{campaign.REPORTS}/w{wave}_a{number:02d}.md"
            path = self.root / report
            path.parent.mkdir(parents=True, exist_ok=True)
            content = f"# Temporary fixture {number}\nThis is not actual agent work.\n".encode()
            path.write_bytes(content)
            data["assignments"].append({
                "assignment": number, "wave": wave,
                "agent": f"/root/w{wave}_a{number:02d}_synthetic_fixture",
                "scope": "Isolated test fixture", "status": "completed", "report": report,
                "report_sha256": hashlib.sha256(content).hexdigest()})
        self.write(data)
        return data

    def write(self, data):
        ledger = self.root / campaign.LEDGER
        ledger.parent.mkdir(parents=True, exist_ok=True)
        ledger.write_text(json.dumps(data) + "\n", encoding="utf-8")

    def cli(self, *arguments):
        output, errors = io.StringIO(), io.StringIO()
        with redirect_stdout(output), redirect_stderr(errors):
            result = campaign.main(["--root", str(self.root), *arguments])
        return result, output.getvalue(), errors.getvalue()

    def assert_success(self, *arguments):
        result, output, errors = self.cli(*arguments)
        self.assertEqual((result, errors), (0, ""), output)
        return output

    def snapshot(self):
        return tuple((self.root / path).read_bytes() if (self.root / path).is_file() else None
                     for path in (campaign.LEDGER, campaign.INDEX))

    def assert_rejected(self, *arguments, message):
        before = self.snapshot()
        result, output, errors = self.cli(*arguments)
        self.assertEqual(result, 1, output)
        self.assertIn(message, errors)
        self.assertEqual(self.snapshot(), before)

    def test_partial_completion_and_exact_hundred_gate(self):
        self.assertIn("40/100", self.assert_success("validate"))
        self.assert_rejected("validate", "--require-complete", message="not complete")
        for count in (0, 1, 19, 20, 21, 99, 100):
            with self.subTest(count=count):
                self.populate(count)
                self.assert_success("validate")
                if count < 100:
                    self.assert_rejected("validate", "--require-complete", message="not complete")
                else:
                    self.assert_success("validate", "--require-complete")

    def test_strict_top_level_schema_and_integer_types(self):
        original = campaign.read(self.root)
        changes = (("schema_version", True), ("requested_assignments", 99),
                   ("requested_waves", 4), ("assignments_per_wave", 25),
                   ("maximum_concurrent_workers", True), ("scope", " \n"),
                   ("baseline_commit", "not-a-commit"), ("baseline_commit", True),
                   ("assignments", {}), ("completed_waves", True),
                   ("unexpected_field", "ignored?"))
        for field, value in changes:
            with self.subTest(field=field):
                data = copy.deepcopy(original)
                data[field] = value
                self.write(data)
                self.assert_rejected("validate", message="campaign error")
        self.populate(101)
        self.assert_rejected("validate", message="schema")

    def test_record_sequence_rejects_gaps_reordering_duplicates_and_booleans(self):
        original = campaign.read(self.root)
        for value in (0, 2, True, "1"):
            data = copy.deepcopy(original)
            data["assignments"][0]["assignment"] = value
            self.write(data)
            self.assert_rejected("validate", message="consecutively")
        data = copy.deepcopy(original)
        data["assignments"][0], data["assignments"][1] = data["assignments"][1], data["assignments"][0]
        self.write(data)
        self.assert_rejected("validate", message="consecutively")
        data = copy.deepcopy(original)
        del data["assignments"][3]
        self.write(data)
        self.assert_rejected("validate", message="consecutively")

    def test_completed_wave_claims_are_derived_not_trusted(self):
        original = campaign.read(self.root)
        for waves in ([], [1], [1, 2, 3], [2, 1], [1, 1], [True, 2], [1.0, 2]):
            with self.subTest(waves=waves):
                data = copy.deepcopy(original)
                data["completed_waves"] = waves
                self.write(data)
                self.assert_rejected("validate", message="completed_waves")

    def test_assignment_shape_wave_name_status_and_scope_are_checked(self):
        original = campaign.read(self.root)
        changes = (("wave", True), ("wave", 2), ("agent", "/root/w2_a01_wrong_wave"),
                   ("agent", "/root/w1_a01_bad-slug"), ("agent", None),
                   ("scope", []), ("scope", "  "), ("status", "running"),
                   ("status", ["completed"]), ("unexpected", 1))
        for field, value in changes:
            with self.subTest(field=field, value=value):
                data = copy.deepcopy(original)
                data["assignments"][0][field] = value
                self.write(data)
                self.assert_rejected("validate", message="campaign error")
        data = copy.deepcopy(original)
        data["assignments"][1]["agent"] = data["assignments"][0]["agent"]
        self.write(data)
        self.assert_rejected("validate", message="duplicate actual agent")

    def test_report_paths_cannot_reuse_or_escape(self):
        original = campaign.read(self.root)
        paths = ("../escape.md", "/tmp/escape.md", "C:/escape.md", "C:\\escape.md",
                 "docs/agents/../escape.md", original["assignments"][1]["report"], None)
        for path in paths:
            with self.subTest(path=path):
                data = copy.deepcopy(original)
                data["assignments"][0]["report"] = path
                self.write(data)
                self.assert_rejected("validate", message="report path")
        for path in (".", "a/../b", "a//b", "a/./b", "C:/outside", "/outside", "a\\b"):
            with self.subTest(checked_path=path), self.assertRaises(ValueError):
                campaign.checked_path(self.root, path)

    def test_missing_changed_empty_and_reused_content_fail_without_rehashing(self):
        original = campaign.read(self.root)
        first = self.root / original["assignments"][0]["report"]
        first.unlink()
        self.assert_rejected("validate", message="missing regular")
        first.write_bytes(b"Changed observation\n")
        self.assert_rejected("validate", message="completed report changed")
        first.write_bytes(b"\n \t")
        self.assert_rejected("validate", message="report is empty")
        data = self.populate(40)
        first = self.root / data["assignments"][0]["report"]
        second = self.root / data["assignments"][1]["report"]
        second.write_bytes(first.read_bytes())
        data["assignments"][1]["report_sha256"] = data["assignments"][0]["report_sha256"]
        self.write(data)
        self.assert_rejected("validate", message="reused report content")

    def test_hash_shape_is_strict(self):
        original = campaign.read(self.root)
        for digest in (None, "0" * 63, "G" * 64, "A" * 64, []):
            data = copy.deepcopy(original)
            data["assignments"][0]["report_sha256"] = digest
            self.write(data)
            self.assert_rejected("validate", message="invalid report SHA256")

    def test_nonfinite_duplicate_and_nonobject_json_fail_without_writes(self):
        path = self.root / campaign.LEDGER
        for content in ('{"schema_version":1,"schema_version":1}', '{"value":NaN}',
                        '{"value":Infinity}', '{"value":1e999}', '[]', 'null', '{'):
            with self.subTest(content=content):
                path.write_text(content, encoding="utf-8")
                self.assert_rejected("validate", message="campaign error")

    def test_report_symlink_and_parent_symlink_are_rejected(self):
        data = campaign.read(self.root)
        report = self.root / data["assignments"][0]["report"]
        target = self.root / "outside-report.md"
        target.write_bytes(report.read_bytes())
        report.unlink()
        try:
            report.symlink_to(target)
        except (OSError, NotImplementedError):
            self.skipTest("symlink creation is unavailable")
        self.assert_rejected("validate", message="symlink campaign path")
        report.unlink()
        report.write_bytes(target.read_bytes())
        reports = self.root / campaign.REPORTS
        moved = self.root / "moved-reports"
        reports.rename(moved)
        reports.symlink_to(moved, target_is_directory=True)
        self.assert_rejected("validate", message="symlink campaign path")

    def test_index_generation_is_deterministic_and_preserves_correction(self):
        before = self.snapshot()
        self.assert_success("sync", "--check")
        self.assert_success("sync")
        self.assertEqual(before, self.snapshot())
        text = (self.root / campaign.INDEX).read_text(encoding="utf-8")
        self.assertIn("40 of 100 assignments; waves 1–2 of 5", text)
        self.assertIn("[Assignment 40](transient-campaign/w2_a40.md)", text)
        self.assertIn("8192 frames/points", text)
        self.assertIn("4096 frames", text)
        self.assertIn("original returned report is retained unchanged", text)
        data = campaign.read(self.root)
        data["assignments"][0]["scope"] = "pipe | and \\ slash\nsecond line"
        self.write(data)
        self.assert_success("sync")
        text = (self.root / campaign.INDEX).read_text(encoding="utf-8")
        self.assertIn("pipe \\| and \\\\ slash second line", text)

    def test_index_drift_repair_never_rewrites_ledger(self):
        ledger = (self.root / campaign.LEDGER).read_bytes()
        index = self.root / campaign.INDEX
        index.write_text("# Stale index\n", encoding="utf-8")
        self.assert_rejected("sync", "--check", message="index is stale")
        self.assert_success("sync")
        self.assertEqual((self.root / campaign.LEDGER).read_bytes(), ledger)
        self.assert_success("sync", "--check")
        index.unlink()
        self.assert_rejected("sync", "--check", message="index is stale")
        self.assert_success("sync")

    def test_invalid_ledger_cannot_be_published_by_sync(self):
        data = campaign.read(self.root)
        data["completed_waves"].append(3)
        self.write(data)
        self.assert_rejected("sync", message="completed_waves")

    def test_failed_index_replace_preserves_existing_files_and_cleans_temporary(self):
        with patch.object(campaign.os, "replace", side_effect=OSError("injected replace failure")):
            self.assert_rejected("sync", message="injected replace failure")
        self.assertEqual(list((self.root / campaign.INDEX).parent.glob(".quadrature-index-*")), [])

    def test_index_symlink_is_never_followed(self):
        index = self.root / campaign.INDEX
        external = self.root / "external-index.md"
        external.write_text("Unrelated destination\n", encoding="utf-8")
        index.unlink()
        try:
            index.symlink_to(external)
        except (OSError, NotImplementedError):
            self.skipTest("symlink creation is unavailable")
        before = external.read_bytes()
        self.assert_rejected("sync", message="symlink campaign path")
        self.assert_rejected("sync", "--check", message="symlink campaign path")
        self.assertEqual(external.read_bytes(), before)


if __name__ == "__main__":
    unittest.main()
