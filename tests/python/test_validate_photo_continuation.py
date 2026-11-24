"""Photo continuation integrity checks against isolated synthetic fixtures."""
from __future__ import annotations

import copy
import hashlib
import json
from pathlib import Path
import stat
import subprocess
import sys
import tempfile
import unittest


SCRIPT = Path(__file__).resolve().parents[2] / "tools/validate_photo_continuation.py"


class PhotoContinuationTests(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory()
        self.addCleanup(temporary.cleanup)
        self.base = Path(temporary.name)
        self.root = self.base / "fixture-root"
        self.path = self.root / "project/photo-continuation-20261124.json"
        self.path.parent.mkdir(parents=True)
        (self.path.parent / "state.json").write_text("Unrelated sentinel.\n", encoding="utf-8")
        self.write(self.fixture(0))

    def fixture(self, count, *, completed=None):
        completed = set(range(1, count + 1) if completed is None else completed)
        records = []
        for number in range(1, count + 1):
            wave = (number - 1) // 20 + 1
            report = f"docs/agents/reports/photo-continuation-20261124/w{wave}-{number:03d}.md"
            record = {"id": f"A{number:03d}", "wave": wave,
                      "task_name": f"/root/w{wave}_{number:03d}_synthetic_fixture",
                      "scope": "Synthetic validator fixture, not worker evidence.",
                      "status": "running", "report": report, "report_sha256": None}
            if number in completed:
                content = f"# Synthetic report {number}\nThis fixture is not agent execution.\n".encode()
                path = self.root / report
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_bytes(content)
                record.update(status="completed", report_sha256=hashlib.sha256(content).hexdigest())
            records.append(record)
        return {"schema_version": 1, "requested_agents": 100, "waves": 5,
                "agents_per_wave": 20, "agents": records}

    def write(self, data):
        self.path.write_text(json.dumps(data) + "\n", encoding="utf-8")

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
        result = subprocess.run(
            [sys.executable, "-B", str(SCRIPT), "--root", str(self.root), *arguments],
            cwd=self.base, text=True, capture_output=True, timeout=10, check=False)
        self.assertEqual(self.snapshot(), before, "validator changed an input or created a file")
        self.assertNotIn("Traceback", result.stderr)
        if error is None:
            self.assertEqual((result.returncode, result.stderr), (0, ""), result.stdout)
        else:
            self.assertEqual((result.returncode, result.stdout), (1, ""), result.stderr)
            self.assertIn(error, result.stderr)
        return result.stdout

    def symlink(self, link, target, *, directory=False):
        try:
            link.symlink_to(target, target_is_directory=directory)
        except (OSError, NotImplementedError) as exc:
            self.skipTest(f"symlinks unavailable: {exc}")

    def test_empty_and_partial_metadata_and_custom_ledger_are_read_only(self):
        self.assertIn("0/100 recorded; 0/100 completed reports", self.cli())
        data = self.fixture(23, completed=(*range(1, 21), 22))
        self.write(data)
        result = json.loads(self.cli("--json"))
        self.assertEqual((result["recorded_agents"], result["completed_agents"]), (23, 21))
        self.assertEqual(result["completed_by_wave"], [20, 1, 0, 0, 0])
        self.assertEqual(result["ledger_sha256"], hashlib.sha256(self.path.read_bytes()).hexdigest())
        for index in (20, 22):
            self.assertFalse((self.root / data["agents"][index]["report"]).exists())
        self.path.rename(self.root / "alternate-ledger.json")
        self.assertIn("23/100 recorded", self.cli("--ledger", "alternate-ledger.json"))

    def test_all_hundred_reports_pass_complete_cli_with_explicit_limitations(self):
        self.write(self.fixture(100))
        result = json.loads(self.cli("--require-complete", "--json"))
        self.assertEqual((result["recorded_agents"], result["completed_agents"]), (100, 100))
        self.assertEqual(result["completed_by_wave"], [20, 20, 20, 20, 20])
        output = self.cli("--require-complete")
        self.assertIn("100/100 recorded; 100/100 completed reports", output)
        for wording in ("Checks metadata and report bytes only", "does not attest agent execution",
                        "execution order", "scientific acceptance", "physical fidelity"):
            with self.subTest(wording=wording):
                self.assertIn(wording, result["limitation"])
                self.assertIn(wording, output)

    def test_require_complete_rejects_empty_and_one_unfinished_report(self):
        self.cli("--require-complete", error="require all 100 completed rows")
        self.write(self.fixture(100, completed=range(1, 100)))
        result = json.loads(self.cli("--json"))
        self.assertEqual((result["recorded_agents"], result["completed_agents"]), (100, 99))
        self.assertEqual(result["completed_by_wave"], [20, 20, 20, 20, 19])
        self.cli("--require-complete", error="require all 100 completed rows")

    def test_reordered_and_gapped_active_wave_ids_are_valid_snapshot_metadata(self):
        data = self.fixture(40, completed=range(1, 21))
        rows = data["agents"]
        data["agents"] = list(reversed(rows[:20] + [rows[39], rows[21], rows[29]]))
        self.write(data)
        result = json.loads(self.cli("--json"))
        self.assertEqual((result["recorded_agents"], result["completed_agents"]), (23, 20))
        self.assertEqual(result["completed_by_wave"], [20, 0, 0, 0, 0])

    def test_later_wave_requires_every_earlier_wave_complete_without_gaps(self):
        original = self.fixture(81, completed=range(1, 81))
        for boundary in (20, 40, 60, 80):
            for missing in (False, True):
                with self.subTest(earlier_wave=boundary // 20, missing=missing):
                    data = copy.deepcopy(original)
                    if missing:
                        del data["agents"][boundary - 1]
                    else:
                        data["agents"][boundary - 1].update(status="running", report_sha256=None)
                    self.write(data)
                    self.cli(error=f"wave {boundary // 20} must have all 20 completed rows")

    def test_duplicate_json_keys_in_headers_and_rows_are_rejected(self):
        content = json.dumps(self.fixture(1, completed=()))
        for token in ('"schema_version": 1', '"id": "A001"'):
            with self.subTest(token=token):
                self.path.write_text(content.replace(token, f"{token}, {token}", 1), encoding="utf-8")
                self.cli(error="duplicate JSON key")

    def test_complete_mode_rejects_duplicate_ids_and_task_names(self):
        original = self.fixture(100)
        for field, message in (("id", "duplicate agent id"), ("task_name", "duplicate task_name")):
            with self.subTest(field=field):
                data = copy.deepcopy(original)
                data["agents"][-1][field] = data["agents"][0][field]
                self.write(data)
                self.cli("--require-complete", error=message)

    def test_integer_metadata_rejects_boolean_float_and_string_impostors(self):
        original = self.fixture(1, completed=())
        for field in ("schema_version", "requested_agents", "waves", "agents_per_wave"):
            for value in (True, float(original[field]), str(original[field])):
                with self.subTest(field=field, value=value):
                    data = copy.deepcopy(original)
                    data[field] = value
                    self.write(data)
                    self.cli(error=f"{field} must be the integer")
        for value in (True, 1.0, "1"):
            with self.subTest(field="wave", value=value):
                data = copy.deepcopy(original)
                data["agents"][0]["wave"] = value
                self.write(data)
                self.cli(error="wave must be the integer 1")

    def test_schema_fields_and_agent_list_size_are_strict(self):
        original = self.fixture(1, completed=())
        for row_level, field in ((False, "waves"), (True, "report_sha256")):
            for extra in (False, True):
                with self.subTest(row_level=row_level, extra=extra):
                    data = copy.deepcopy(original)
                    target = data["agents"][0] if row_level else data
                    if extra:
                        target["unexpected"] = "fixture"
                    else:
                        del target[field]
                    self.write(data)
                    self.cli(error="must contain exactly")
        for records in ({}, [None] * 101):
            with self.subTest(records_type=type(records).__name__):
                data = copy.deepcopy(original)
                data["agents"] = records
                self.write(data)
                self.cli(error="agents must be a list of at most 100 rows")

    def test_row_identity_and_canonical_report_paths_are_enforced(self):
        original = self.fixture(1, completed=())
        cases = [("id", value, "agent id must be A001..A100") for value in ("A000", "A101", "A1")]
        cases += [("wave", 2, "wave must be the integer 1"),
                  ("task_name", "/root/../fixture", "canonical /root/... task path"),
                  ("scope", " \n\t", "scope must be a nonempty string"),
                  ("status", "accepted", "status must be running or completed")]
        cases += [("report", value, "report must be") for value in (
            "../outside-report.md", str(self.base / "outside-report.md"),
            "docs/agents/reports/photo-continuation-20261124/w1-002.md")]
        for field, value, message in cases:
            with self.subTest(field=field, value=value):
                data = copy.deepcopy(original)
                data["agents"][0][field] = value
                self.write(data)
                self.cli(error=message)

    def test_running_and_completed_hash_protocols_are_distinct(self):
        data = self.fixture(1, completed=())
        data["agents"][0]["report_sha256"] = "a" * 64
        self.write(data)
        self.cli(error="running report_sha256 must be null")
        original = self.fixture(1)
        for digest in (None, True, "a" * 63, "A" * 64):
            with self.subTest(digest=digest):
                data = copy.deepcopy(original)
                data["agents"][0]["report_sha256"] = digest
                self.write(data)
                self.cli(error="completed report_sha256 must be 64 lowercase hex digits")

    def test_completed_report_must_be_present_and_a_regular_file(self):
        data = self.fixture(1)
        self.write(data)
        report = self.root / data["agents"][0]["report"]
        report.unlink()
        self.cli(error="missing regular file")
        report.mkdir()
        self.cli(error="invalid report path component")

    def test_changed_completed_report_bytes_do_not_match_recorded_digest(self):
        data = self.fixture(1)
        self.write(data)
        report = self.root / data["agents"][0]["report"]
        report.write_bytes(report.read_bytes() + b"Altered fixture bytes.\n")
        self.cli(error="report SHA256 mismatch")

    def test_completed_report_and_ancestor_symlinks_are_rejected(self):
        data = self.fixture(1)
        self.write(data)
        report = self.root / data["agents"][0]["report"]
        outside = self.base / "outside-report.md"
        report.rename(outside)
        self.symlink(report, outside)
        self.cli(error="symlink or reparse")
        report.unlink()
        outside.rename(report)
        outside_directory = self.base / "outside-reports"
        report.parent.rename(outside_directory)
        self.symlink(report.parent, outside_directory, directory=True)
        self.cli(error="symlink or reparse")

    def test_absent_running_reports_still_reject_symlink_path_components(self):
        data = self.fixture(1, completed=())
        self.write(data)
        report = self.root / data["agents"][0]["report"]
        report.parent.mkdir(parents=True)
        self.symlink(report, self.base / "missing-report.md")
        self.cli(error="symlink or reparse")
        report.unlink()
        report.parent.rmdir()
        outside_directory = self.base / "empty-outside-reports"
        outside_directory.mkdir()
        self.symlink(report.parent, outside_directory, directory=True)
        self.cli(error="symlink or reparse")

    def test_ledger_paths_cannot_escape_root_or_follow_symlinks(self):
        for relative in ("../outside.json", str(self.path), "project/../outside.json",
                         "./project/photo-continuation-20261124.json", "project\\outside.json"):
            with self.subTest(relative=relative):
                self.cli("--ledger", relative, error="unsafe root-relative path")
        outside = self.base / "outside-ledger.json"
        self.path.rename(outside)
        self.symlink(self.path, outside)
        self.cli(error="symlink or reparse")


if __name__ == "__main__":
    unittest.main()
