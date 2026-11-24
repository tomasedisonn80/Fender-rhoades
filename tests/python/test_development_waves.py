"""Synthetic campaign fixtures verify metadata checks, never actual agent work."""
from __future__ import annotations

from contextlib import redirect_stderr, redirect_stdout
import copy
import hashlib
import importlib.util
import io
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[2]
SPEC = importlib.util.spec_from_file_location(
    "rhodes_development_waves", ROOT / "tools/validate_development_waves.py")
assert SPEC and SPEC.loader
waves = importlib.util.module_from_spec(SPEC)
with patch("argparse.ArgumentParser.parse_args", side_effect=AssertionError("CLI ran on import")):
    SPEC.loader.exec_module(waves)


class DevelopmentWaveTests(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory()
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name)
        self.data = self.populate_wave(1)

    def populate_wave(self, wave, count=20, completed=None, status=None):
        completed = count if completed is None else completed
        data = {"schema_version": 1, "wave": wave, "requested_actual_agents": 20,
                "status": status or ("complete" if count == completed == 20 else "in_progress"),
                "scope": "Synthetic metadata fixture; no actual worker execution.",
                "completed_actual_agents": completed, "agents": []}
        for offset in range(1, count + 1):
            number = (wave - 1) * 20 + offset
            record = {"assignment": number, "wave": wave, "id": f"W{wave}-{number:02d}",
                      "agent_task_name": f"/root/w{wave}_{number:02d}_fixture",
                      "status": "completed" if offset <= completed else "running"}
            if offset <= completed:
                content = f"# Synthetic report {number}\nNot actual agent work.\n".encode()
                record["report"] = f"docs/agents/reports/20261124-w{wave}-{number:02d}-fixture.md"
                record["sha256"] = hashlib.sha256(content).hexdigest()
                path = self.root / record["report"]
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_bytes(content)
            data["agents"].append(record)
        self.write(data)
        return data

    def write(self, data):
        path = self.root / waves.ledger_name(data["wave"] if type(data.get("wave")) is int else 1)
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(json.dumps(data) + "\n", encoding="utf-8")

    def cli(self, *args):
        stdout, stderr = io.StringIO(), io.StringIO()
        with redirect_stdout(stdout), redirect_stderr(stderr):
            code = waves.main(["--root", str(self.root), *args])
        return code, stdout.getvalue(), stderr.getvalue()

    def assert_invalid(self, fragment, *args):
        code, stdout, stderr = self.cli(*args)
        self.assertEqual(code, 1, stdout)
        self.assertEqual(stdout, "")
        self.assertIn(fragment, stderr)

    def test_progress_and_complete_gate(self):
        code, stdout, stderr = self.cli("--json")
        self.assertEqual((code, stderr), (0, ""))
        summary = json.loads(stdout)
        self.assertEqual(summary["completed_agents"], 20)
        self.assertEqual(summary["completed_waves"], 1)
        self.assertEqual(summary["status"], "in_progress")
        self.assertIn("does not attest", summary["limitation"])
        self.assert_invalid("not complete", "--require-complete")
        for wave in range(2, 6):
            self.populate_wave(wave)
        code, stdout, stderr = self.cli("--require-complete", "--json")
        self.assertEqual((code, stderr), (0, ""))
        summary = json.loads(stdout)
        self.assertEqual((summary["status"], summary["completed_agents"], summary["completed_waves"]),
                         ("complete", 100, 5))

    def test_running_and_pending_workers_need_actual_names_but_not_reports(self):
        data = self.populate_wave(2, count=3, completed=1)
        data["agents"][2]["status"] = "pending"
        self.write(data)
        self.assertEqual(waves.validate(self.root)["completed_agents"], 21)
        self.assertEqual(waves.validate(self.root)["recorded_agents"], 23)
        del data["agents"][2]["agent_task_name"]
        self.write(data)
        self.assert_invalid("invalid fields")

    def test_unfinished_report_may_be_reserved_but_hash_must_match_if_supplied(self):
        data = self.populate_wave(2, count=1, completed=0)
        record = data["agents"][0]
        record["report"] = "docs/agents/reports/20261124-w2-21-reserved.md"
        self.write(data)
        self.assertEqual(waves.validate(self.root)["recorded_agents"], 21)
        record["sha256"] = "a" * 64
        self.write(data)
        self.assert_invalid("missing regular file")
        del record["report"]
        self.write(data)
        self.assert_invalid("sha256 requires a report")

    def test_wave_order_and_missing_first_wave(self):
        self.populate_wave(3)
        self.assert_invalid("missing earlier wave")
        (self.root / waves.ledger_name(1)).unlink()
        self.assert_invalid("missing earlier wave")
        (self.root / waves.ledger_name(3)).unlink()
        self.assert_invalid("missing wave 1")

    def test_next_wave_requires_previous_complete(self):
        self.populate_wave(1, count=19)
        self.populate_wave(2, count=1, completed=0)
        self.assert_invalid("previous wave is not complete")

    def test_extra_wave_and_zero_padded_filename_are_rejected(self):
        for name in ("wave6-agent-ledger-20261124.json", "wave01-agent-ledger-20261124.json"):
            with self.subTest(name=name):
                path = self.root / "project" / name
                path.write_text("{}", encoding="utf-8")
                self.assert_invalid("unexpected campaign wave")
                path.unlink()

    def test_complete_requires_twenty_completed_agents(self):
        for count, completed in ((19, 19), (20, 19)):
            with self.subTest(count=count):
                self.populate_wave(1, count=count, completed=completed, status="complete")
                self.assert_invalid("exactly 20 completed")

    def test_empty_or_excessive_wave_is_not_a_valid_placeholder(self):
        for count in (0, 21):
            self.populate_wave(1, count=count)
            self.assert_invalid("expected 1..20")

    def test_top_level_shape_types_counts_and_unknown_fields(self):
        changes = (("schema_version", True), ("requested_actual_agents", 19),
                   ("status", []), ("status", "done"), ("scope", "  "),
                   ("agents", {}), ("completed_actual_agents", True),
                   ("completed_actual_agents", 19), ("concurrency_limit_total", 5),
                   ("concurrency_limit_total", True), ("phy002_acceptance", []),
                   ("unknown", "unrecognized"))
        for field, value in changes:
            with self.subTest(field=field, value=value):
                data = copy.deepcopy(self.data)
                data[field] = value
                self.write(data)
                self.assert_invalid("Development wave error")
        self.write(self.data)
        path = self.root / waves.ledger_name(1)
        for raw in (b"[]", b"null", b"true", b"{}"):
            path.write_bytes(raw)
            self.assert_invalid("invalid ledger fields")

    def test_ledger_wave_must_match_its_filename(self):
        data = copy.deepcopy(self.data)
        for value in (True, 1.0, 2, "1"):
            with self.subTest(value=value):
                data["wave"] = value
                (self.root / waves.ledger_name(1)).write_text(json.dumps(data), encoding="utf-8")
                self.assert_invalid("invalid wave")

    def test_assignment_sequence_and_id_are_not_repaired(self):
        for value in (True, 1.0, 0, 2, "1"):
            data = copy.deepcopy(self.data)
            data["agents"][0]["assignment"] = value
            self.write(data)
            self.assert_invalid("ordered consecutively")
        for key, value in (("wave", True), ("wave", 2), ("id", "W1-1"), ("id", 1),
                           ("status", "done"), ("status", []), ("extra", 1),
                           ("followup", "  ")):
            with self.subTest(key=key):
                data = copy.deepcopy(self.data)
                data["agents"][0][key] = value
                self.write(data)
                self.assert_invalid("Development wave error")

    def test_names_are_unique_canonical_and_wave_scoped(self):
        for name in (None, "/root", "/root/w2_wrong", "/root/w1_a/../w1_b", "/root/w1_a/",
                     "/root/w1_a//w1_b", "/root/w1_bad-slug", "/root/w1_a/w2_b",
                     self.data["agents"][1]["agent_task_name"]):
            with self.subTest(name=name):
                data = copy.deepcopy(self.data)
                data["agents"][0]["agent_task_name"] = name
                self.write(data)
                self.assert_invalid("agent name")

    def test_nested_coordinator_identity_and_historical_metadata(self):
        data = copy.deepcopy(self.data)
        coordinator = data["agents"][0]["agent_task_name"]
        for record in data["agents"][3:]:
            record["agent_task_name"] = coordinator + record["agent_task_name"][5:]
        data.update(coordinator=coordinator, coordinator_additional_actual_spawns=17,
                    concurrency_limit_total=4, phy002_acceptance="incomplete")
        data["agents"][2]["followup"] = "One existing worker was reused, not counted again."
        self.write(data)
        self.assertEqual(waves.validate(self.root)["recorded_agents"], 20)
        data["coordinator_additional_actual_spawns"] = 18
        self.write(data)
        self.assert_invalid("spawn count mismatch")
        data["coordinator_additional_actual_spawns"] = 17
        data["coordinator"] = "/root/w1_missing"
        self.write(data)
        self.assert_invalid("coordinator must be a recorded")

    def test_completed_reports_require_both_path_and_valid_digest(self):
        for key in ("report", "sha256"):
            data = copy.deepcopy(self.data)
            del data["agents"][0][key]
            self.write(data)
            self.assert_invalid("requires report and sha256")
        for digest in (None, True, "a" * 63, "A" * 64, "0" * 64):
            with self.subTest(digest=digest):
                data = copy.deepcopy(self.data)
                data["agents"][0]["sha256"] = digest
                self.write(data)
                self.assert_invalid("SHA256")

    def test_report_paths_cannot_escape_change_assignment_or_normalize(self):
        for value in (None, "../report.md", "/tmp/report.md", "C:/report.md", "C:\\report.md",
                      "docs//agents/reports/20261124-w1-01-fixture.md",
                      "docs/agents/reports/../20261124-w1-01-fixture.md",
                      self.data["agents"][1]["report"]):
            data = copy.deepcopy(self.data)
            data["agents"][0]["report"] = value
            self.write(data)
            self.assert_invalid("report path")
        for value in (".", "a/./b", "a//b", "a/../b", "a\\b", "a:b", "a\0b", "a\nb"):
            with self.subTest(path=value), self.assertRaises(ValueError):
                waves.checked_path(self.root, value)

    def test_missing_changed_empty_or_copied_report_fails(self):
        first = self.root / self.data["agents"][0]["report"]
        first.unlink()
        self.assert_invalid("missing regular file")
        first.write_bytes(b"Changed\n")
        self.assert_invalid("SHA256 mismatch")
        first.write_bytes(b" \n\t")
        self.assert_invalid("empty report")
        data = self.populate_wave(1)
        second = self.root / data["agents"][1]["report"]
        second.write_bytes(first.read_bytes())
        data["agents"][1]["sha256"] = data["agents"][0]["sha256"]
        self.write(data)
        self.assert_invalid("reused report content")

    def test_strict_json_rejects_duplicates_nonfinite_invalid_utf8_and_deep_nesting(self):
        path = self.root / waves.ledger_name(1)
        mutations = [
            b'{"schema_version":1,"schema_version":1}',
            b'{"agents":[{"assignment":1,"assignment":1}]}',
            b'{"extra":NaN}', b'{"extra":Infinity}', b'{"extra":-Infinity}',
            b'{"extra":1e9999}', b"\xff", b"\xef\xbb\xbf{}", b"[" * 2000 + b"]" * 2000,
        ]
        for raw in mutations:
            with self.subTest(raw=raw[:70]):
                path.write_bytes(raw)
                self.assert_invalid("Development wave error")

    def make_symlink(self, path, target, directory=False):
        try:
            path.symlink_to(target, target_is_directory=directory)
        except (OSError, NotImplementedError) as exc:
            self.skipTest(f"symlinks unavailable on this test filesystem: {exc}")

    def test_final_report_symlink_is_rejected_even_inside_root(self):
        first = self.root / self.data["agents"][0]["report"]
        second = self.root / self.data["agents"][1]["report"]
        first.unlink()
        self.make_symlink(first, second)
        self.assert_invalid("symlink path component")

    def test_intermediate_symlink_is_rejected(self):
        original = self.root / "docs"
        moved = self.root / "original-docs"
        original.rename(moved)
        self.make_symlink(original, moved, directory=True)
        self.assert_invalid("symlink path component")

    def test_dangling_ledger_symlink_is_not_treated_as_future_absence(self):
        self.make_symlink(self.root / waves.ledger_name(2), self.root / "absent.json")
        self.assert_invalid("symlink path component")

    def test_nonregular_report_rejected(self):
        first = self.root / self.data["agents"][0]["report"]
        first.unlink()
        first.mkdir()
        self.assert_invalid("missing regular file")
        first.rmdir()
        if hasattr(os, "mkfifo"):
            os.mkfifo(first)
            self.assert_invalid("missing regular file")

    def test_bounded_reads_reject_large_ledgers_and_reports(self):
        with patch.object(waves, "MAX_LEDGER_BYTES", 32):
            self.assert_invalid("exceeds 32-byte limit")
        with patch.object(waves, "MAX_REPORT_BYTES", 8):
            self.assert_invalid("exceeds 8-byte limit")

    def test_cli_is_read_only_on_success_and_failure(self):
        before = {str(path.relative_to(self.root)): path.read_bytes()
                  for path in self.root.rglob("*") if path.is_file()}
        self.assertEqual(self.cli()[0], 0)
        self.assert_invalid("not complete", "--require-complete")
        after = {str(path.relative_to(self.root)): path.read_bytes()
                 for path in self.root.rglob("*") if path.is_file()}
        self.assertEqual(before, after)

    def test_entrypoint_works_with_explicit_root_from_other_directory(self):
        result = subprocess.run(
            [sys.executable, str(ROOT / "tools/validate_development_waves.py"),
             "--root", str(self.root), "--json"], cwd=self.root, capture_output=True, check=False)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(result.stderr, b"")
        self.assertEqual(json.loads(result.stdout)["completed_agents"], 20)


if __name__ == "__main__":
    unittest.main()
