"""Independent whole-campaign rejection probes using only temporary fake records."""
from __future__ import annotations

from contextlib import redirect_stderr, redirect_stdout
import hashlib
import importlib.util
import io
import json
import os
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch


ROOT = Path(__file__).resolve().parents[2]
SPEC = importlib.util.spec_from_file_location(
    "development_waves_adversarial", ROOT / "tools/validate_development_waves.py")
assert SPEC and SPEC.loader
waves = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(waves)


class WholeCampaignBoundaryTests(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory()
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name)
        self.ledgers = {}
        for wave in range(1, 6):
            records = []
            for number in range((wave - 1) * 20 + 1, wave * 20 + 1):
                relative = f"docs/agents/reports/20261124-w{wave}-{number:02d}-probe.md"
                content = f"# Temporary synthetic fixture {number}\nNo agent executed.\n".encode()
                path = self.root / relative
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_bytes(content)
                records.append({
                    "assignment": number, "wave": wave, "id": f"W{wave}-{number:02d}",
                    "agent_task_name": f"/root/w{wave}_synthetic_{number}",
                    "status": "completed", "report": relative,
                    "sha256": hashlib.sha256(content).hexdigest(),
                })
            self.ledgers[wave] = {
                "schema_version": 1, "wave": wave, "requested_actual_agents": 20,
                "status": "complete", "scope": "Temporary fake records, never actual execution.",
                "completed_actual_agents": 20, "agents": records,
            }
            self.write_ledger(wave)

    def write_ledger(self, wave):
        path = self.root / waves.ledger_name(wave)
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(json.dumps(self.ledgers[wave]), encoding="utf-8")

    def assert_cli_rejects(self, reason):
        stdout, stderr = io.StringIO(), io.StringIO()
        before = {path.relative_to(self.root): path.read_bytes()
                  for path in self.root.rglob("*") if path.is_file()}
        with redirect_stdout(stdout), redirect_stderr(stderr):
            result = waves.main(["--root", str(self.root), "--require-complete", "--json"])
        self.assertEqual(result, 1)
        self.assertEqual(stdout.getvalue(), "")
        self.assertIn(reason, stderr.getvalue())
        after = {path.relative_to(self.root): path.read_bytes()
                 for path in self.root.rglob("*") if path.is_file()}
        self.assertEqual(before, after)

    def test_complete_gate_visits_the_final_report(self):
        self.assertEqual(waves.validate(self.root, require_complete=True)["completed_agents"], 100)
        final = self.ledgers[5]["agents"][-1]
        path = self.root / final["report"]
        path.write_bytes(path.read_bytes() + b"An unregistered later change.\n")
        self.assert_cli_rejects("report SHA256 mismatch")

    def test_rehashed_cross_wave_report_replay_is_rejected(self):
        first = self.ledgers[1]["agents"][0]
        final = self.ledgers[5]["agents"][-1]
        (self.root / final["report"]).write_bytes((self.root / first["report"]).read_bytes())
        final["sha256"] = first["sha256"]
        self.write_ledger(5)
        self.assert_cli_rejects("reused report content")

    def test_reused_or_cross_wave_nested_names_cannot_complete_campaign(self):
        final = self.ledgers[5]["agents"][-1]
        for name, reason in (
            (self.ledgers[5]["agents"][0]["agent_task_name"], "duplicate actual agent name"),
            (self.ledgers[1]["agents"][0]["agent_task_name"], "incorrect wave prefix"),
            ("/root/w5_coordinator/w1_old_worker", "incorrect wave prefix"),
        ):
            with self.subTest(name=name):
                final["agent_task_name"] = name
                self.write_ledger(5)
                self.assert_cli_rejects(reason)

    def test_escaped_duplicate_digest_key_in_last_wave_is_rejected(self):
        path = self.root / waves.ledger_name(5)
        raw = path.read_text(encoding="utf-8")
        # The two lexical spellings decode to the same key before validation.
        escaped = '"sha\\u0032\\u0035\\u0036": "' + "0" * 64 + '", "sha256":'
        path.write_text(raw.replace('"sha256":', escaped, 1), encoding="utf-8")
        self.assert_cli_rejects("duplicate JSON key: sha256")

    def test_read_budget_survives_file_growth_after_initial_stat(self):
        path = self.root / "growing-input"
        path.write_bytes(b"12345678")
        original_fstat = os.fstat
        first = True

        def grow_after_stat(descriptor):
            nonlocal first
            result = original_fstat(descriptor)
            if first:
                first = False
                with path.open("ab") as stream:
                    stream.write(b"9")
            return result

        with patch.object(waves.os, "fstat", side_effect=grow_after_stat):
            with self.assertRaisesRegex(ValueError, "exceeds 8-byte limit"):
                waves.read_regular(self.root, "growing-input", 8)
        self.assertEqual(path.read_bytes(), b"123456789")


if __name__ == "__main__":
    unittest.main()
