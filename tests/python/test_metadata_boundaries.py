"""A089: bounded malformed-metadata regressions in disposable fixture roots.

All Git-looking paths and report contents below are invented local fixtures.
These tests never open the checkout's Git metadata or canonical campaign ledger.
"""
from __future__ import annotations

from contextlib import redirect_stderr, redirect_stdout
import io
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch

from test_campaign import campaign
from test_project import project


class MetadataBoundaryTests(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory(prefix="metadata-boundary-")
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name).resolve()
        (self.root / "project").mkdir()
        (self.root / "docs/agents/reviews").mkdir(parents=True)
        self.state_path = self.root / "project/state.json"
        self.registry_path = self.root / "project/evidence.json"
        self.dashboard = self.root / "docs/PROJECT_STATE.md"
        self.ledger = self.root / "project/agent-campaign.json"
        self.index = self.root / "docs/agents/CAMPAIGN.md"
        self.state = {
            "schema_version": 1, "project": "Metadata fixture", "goal": "Check input boundaries",
            "target": "Temporary synthetic fixture", "fidelity_status": "unvalidated",
            "revision": 0, "updated_at": "2026-01-01T00:00:00Z", "history": [],
            "tasks": [{
                "id": "TST-001", "title": "Fixture task", "milestone": "Fixture",
                "status": "todo", "priority": 0, "dependencies": [],
                "acceptance": ["Test fixture only"], "required_evidence": ["documentary"],
                "evidence": [], "owner": None, "claim": None, "blockers": [], "paths": [],
            }],
        }
        self.state_path.write_text(project.encode(self.state), encoding="utf-8")
        self.registry_path.write_text(project.encode({"schema_version": 1, "records": []}), encoding="utf-8")
        self.dashboard.write_text(project.render_state(self.state), encoding="utf-8")
        self.data = {"schema_version": 1, "requested_agents": 2, "agents": []}
        self.ledger.write_text(json.dumps(self.data), encoding="utf-8")
        self.index.write_text(campaign.render(self.data), encoding="utf-8")
        replacements = patch.multiple(campaign, ROOT=self.root, LEDGER=self.ledger, INDEX=self.index)
        replacements.start()
        self.addCleanup(replacements.stop)

    def snapshot(self):
        return {path: path.read_bytes() for path in
                (self.state_path, self.registry_path, self.dashboard, self.ledger, self.index)}

    def project_cli(self, *arguments, root=None):
        output, errors = io.StringIO(), io.StringIO()
        with redirect_stdout(output), redirect_stderr(errors):
            status = project.main(["--root", str(self.root if root is None else root), *arguments])
        return status, output.getvalue(), errors.getvalue()

    def campaign_cli(self, *arguments):
        output, errors = io.StringIO(), io.StringIO()
        with patch.object(sys, "argv", ["campaign.py", *arguments]), \
                redirect_stdout(output), redirect_stderr(errors):
            status = campaign.main()
        return status, output.getvalue(), errors.getvalue()

    def assert_rejected(self, cli, *arguments, message, **kwargs):
        before = self.snapshot()
        status, output, error = cli(*arguments, **kwargs)
        expected = 2 if cli == self.project_cli else 1
        self.assertEqual(status, expected, (output, error))
        self.assertTrue(error.startswith("project:" if expected == 2 else "Campaign error:"), error)
        self.assertIn(message, error)
        self.assertNotIn("Traceback", error)
        self.assertEqual(self.snapshot(), before)
        self.assertFalse((self.root / "project/.project.lock").exists())

    def evidence_args(self, artifact):
        return ("evidence-add", "fixture-report", "--kind", "documentary", "--result", "pass",
                "--summary", "Synthetic path-boundary test", "--artifact", artifact)

    def symlink(self, link, target, *, directory=False):
        try:
            link.symlink_to(target, target_is_directory=directory)
        except (OSError, NotImplementedError):
            self.skipTest("Host does not permit symlinks")

    def test_project_invalid_utf8_state_and_registry_fail_before_writes(self):
        for path in (self.state_path, self.registry_path):
            original = path.read_bytes()
            with self.subTest(path=path.name):
                path.write_bytes(b"\xff")
                for arguments in (("validate",), ("claim", "TST-001", "--owner", "fixture")):
                    self.assert_rejected(self.project_cli, *arguments, message="Cannot read")
            path.write_bytes(original)

    def test_project_malformed_and_deep_json_fail_before_writes(self):
        cases = {"syntax": b'{"schema_version":',
                 "depth": b"[" * 10000 + b"0" + b"]" * 10000}
        for label, raw in cases.items():
            with self.subTest(case=label):
                self.state_path.write_bytes(raw)
                self.assert_rejected(self.project_cli, "sync", message="Cannot read")

    def test_project_duplicate_nonfinite_and_overflow_json_are_rejected(self):
        cases = [(b'{"schema_version":1,"schema_version":1}', "duplicate JSON key")]
        cases += [(b'{"value":' + literal + b'}', "nonfinite")
                  for literal in (b"NaN", b"Infinity", b"-Infinity", b"1e999", b"-1e999")]
        for raw, message in cases:
            with self.subTest(raw=raw):
                self.registry_path.write_bytes(raw)
                self.assert_rejected(self.project_cli, "validate", message=message)

    def test_project_invalid_utf8_dashboard_is_a_controlled_failure(self):
        self.dashboard.write_bytes(b"\xff")
        self.assert_rejected(self.project_cli, "sync", "--check", message="decode")

    def test_project_controls_are_rejected_before_resolving_paths(self):
        for control in ("\x00", "\t", "\n", "\r", "\x1f", "\x7f"):
            with self.subTest(control=repr(control)), \
                    patch.object(Path, "resolve", side_effect=AssertionError("unsafe path reached filesystem")):
                with self.assertRaisesRegex(project.StateError, "control characters"):
                    project.relative_path(self.root, f"docs/a{control}b.txt", "fixture")
                with self.assertRaisesRegex(project.StateError, "control characters"):
                    project.resolve_path(Path(f"bad{control}root"), "fixture root")

    def test_project_control_characters_in_task_paths_preserve_files(self):
        for value in ("bad\x00path", "docs/line\nbreak"):
            with self.subTest(value=value):
                self.state["tasks"][0]["paths"] = [value]
                self.state_path.write_text(project.encode(self.state), encoding="utf-8")
                self.assert_rejected(self.project_cli, "claim", "TST-001", "--owner", "fixture",
                                     message="control characters")

    def test_project_control_characters_in_root_are_controlled(self):
        for control in ("\x00", "\n"):
            with self.subTest(control=repr(control)):
                self.assert_rejected(self.project_cli, "validate", root=f"{self.root}/bad{control}root",
                                     message="control characters")

    def test_project_resolve_failures_are_normalized(self):
        for error in (ValueError("Injected invalid path"), RuntimeError("Injected symlink loop"),
                      OSError("Injected resolution failure")):
            with self.subTest(error=type(error).__name__), patch.object(Path, "resolve", side_effect=error):
                with self.assertRaisesRegex(project.StateError, "cannot resolve path"):
                    project.relative_path(self.root, "docs/report.txt", "fixture")

    def test_project_symlink_loop_is_a_controlled_failure(self):
        loop = self.root / "docs/loop"
        self.symlink(loop, "loop", directory=True)
        self.state["tasks"][0]["paths"] = ["docs/loop/report.txt"]
        self.state_path.write_text(project.encode(self.state), encoding="utf-8")
        self.assert_rejected(self.project_cli, "claim", "TST-001", "--owner", "fixture",
                             message="cannot resolve path")

    def test_project_git_case_variants_are_rejected_before_resolution(self):
        for value in (".git/config", ".GIT/config", "docs/.GiT/config"):
            with self.subTest(value=value), \
                    patch.object(Path, "resolve", side_effect=AssertionError("Git path reached filesystem")):
                with self.assertRaisesRegex(project.StateError, "unsafe path"):
                    project.relative_path(self.root, value, "fixture", exists=True)

    def test_project_git_aliases_are_rejected_before_opening_bytes(self):
        fake = self.root / ".git/config"
        fake.parent.mkdir()
        fake.write_bytes(b"Synthetic Git-looking fixture only.\n")
        self.symlink(self.root / "docs/metadata-alias", fake.parent, directory=True)
        self.symlink(self.root / "docs/config-alias", fake)
        original = fake.read_bytes()
        original_open = Path.open

        def guarded_open(path, *args, **kwargs):
            if path == fake or path in (self.root / "docs/metadata-alias/config", self.root / "docs/config-alias"):
                raise AssertionError("Git-looking fixture bytes were opened")
            return original_open(path, *args, **kwargs)

        with patch.object(Path, "open", new=guarded_open):
            for alias in ("docs/metadata-alias/config", "docs/config-alias"):
                with self.subTest(alias=alias):
                    self.assert_rejected(self.project_cli, *self.evidence_args(alias), message="Git metadata")
        self.assertEqual(fake.read_bytes(), original)

    def test_project_alias_into_casefold_git_directory_is_rejected(self):
        fake = self.root / ".GIT/config"
        fake.parent.mkdir()
        fake.write_bytes(b"Synthetic portable-case fixture only.\n")
        self.symlink(self.root / "docs/metadata-alias", fake.parent, directory=True)
        with patch.object(project, "sha256", side_effect=AssertionError("Git-looking fixture was hashed")):
            self.assert_rejected(self.project_cli, *self.evidence_args("docs/metadata-alias/config"),
                                 message="Git metadata")

    def test_project_symlinked_git_directory_target_is_also_excluded(self):
        fake = self.root / "metadata/config"
        fake.parent.mkdir()
        fake.write_bytes(b"Synthetic linked Git-looking fixture only.\n")
        self.symlink(self.root / ".git", fake.parent, directory=True)
        with patch.object(project, "sha256", side_effect=AssertionError("Git-looking fixture was hashed")):
            self.assert_rejected(self.project_cli, *self.evidence_args("metadata/config"), message="Git metadata")

    def test_project_ordinary_in_root_symlink_remains_usable(self):
        artifact = self.root / "docs/ordinary.txt"
        artifact.write_bytes(b"Synthetic ordinary evidence fixture.\n")
        self.symlink(self.root / "docs/evidence-alias.txt", artifact)
        status, _, error = self.project_cli(*self.evidence_args("docs/evidence-alias.txt"))
        self.assertEqual((status, error), (0, ""))
        record = project.load_json(self.registry_path)["records"][0]
        self.assertEqual(record["artifacts"], [{"path": "docs/evidence-alias.txt", "sha256": project.sha256(artifact)}])

    def test_campaign_duplicate_keys_and_nonfinite_numbers_fail_before_writes(self):
        cases = [
            (b'{"schema_version":1,"requested_agents":1,"requested_agents":2,"agents":[]}', "duplicate JSON key"),
            (b'{"schema_version":1,"requested_agents":1,"agents":[],"extra":{"a":1,"a":2}}', "duplicate JSON key"),
            (b'{"schema_version":1,"requested_agents":1,"requested_agents":2,"agents":[],"extra":NaN}', "nonfinite"),
        ]
        cases += [(b'{"schema_version":1,"requested_agents":1,"agents":[],"extra":[{"value":'
                   + literal + b'}]}', "nonfinite")
                  for literal in (b"NaN", b"Infinity", b"-Infinity", b"1e999", b"-1e999")]
        for raw, message in cases:
            with self.subTest(raw=raw):
                self.ledger.write_bytes(raw)
                for arguments in (("validate",), ("sync",),
                                  ("register", "A001", "/root/synthetic_fixture", "Synthetic fixture")):
                    self.assert_rejected(self.campaign_cli, *arguments, message=message)

    def test_campaign_invalid_utf8_malformed_and_deep_json_fail_before_writes(self):
        cases = {"encoding": b"\xff", "syntax": b'{"schema_version":',
                 "depth": b"[" * 10000 + b"0" + b"]" * 10000}
        for label, raw in cases.items():
            with self.subTest(case=label):
                self.ledger.write_bytes(raw)
                for arguments in (("validate",), ("sync",),
                                  ("register", "A001", "/root/synthetic_fixture", "Synthetic fixture")):
                    self.assert_rejected(self.campaign_cli, *arguments, message="Cannot read")

    def test_campaign_finite_extra_fields_keep_existing_schema_and_cli_semantics(self):
        self.data["extra"] = {"finite": 1e308, "nested": [-0.0, 1e-300], "label": "Test café"}
        self.ledger.write_text(json.dumps(self.data), encoding="utf-8")
        before = self.snapshot()
        self.assertEqual(self.campaign_cli("validate")[0], 0)
        self.assertEqual(self.snapshot(), before)
        status, _, error = self.campaign_cli("register", "A001", "/root/synthetic_fixture", "Synthetic fixture")
        self.assertEqual((status, error), (0, ""))
        self.assertEqual(campaign.load_json(self.ledger)["extra"], self.data["extra"])
        self.assertEqual(self.campaign_cli("sync", "--check")[0], 0)

    def test_campaign_deep_extension_encoding_failure_preserves_files(self):
        # Decoder and encoder depth limits differ across supported Pythons.
        # Supply an already-decoded extension to isolate the encoding boundary.
        nested = 0
        for _ in range(sys.getrecursionlimit() + 100):
            nested = [nested]
        self.data["extra"] = nested
        with patch.object(campaign, "load_json", return_value=self.data):
            self.assert_rejected(self.campaign_cli, "register", "A001", "/root/synthetic_fixture",
                                 "Synthetic fixture", message="recursion")

    def test_campaign_report_resolution_errors_are_normalized(self):
        self.data["agents"] = [{
            "id": "A001", "task_name": "/root/synthetic_fixture", "status": "running",
            "scope": "Synthetic fixture", "report": "docs/agents/reviews/A001.md", "report_sha256": None,
        }]
        self.ledger.write_text(json.dumps(self.data), encoding="utf-8")
        report = self.root / self.data["agents"][0]["report"]
        original_resolve = Path.resolve
        for error in (OSError("Injected resolution failure"), ValueError("Injected invalid path"),
                      RuntimeError("Injected symlink loop")):
            def failing_resolve(path, *args, **kwargs):
                if path == report:
                    raise error
                return original_resolve(path, *args, **kwargs)

            with self.subTest(error=type(error).__name__), patch.object(Path, "resolve", new=failing_resolve):
                with self.assertRaisesRegex(ValueError, "cannot resolve"):
                    campaign.validate(self.data)
                self.assert_rejected(self.campaign_cli, "sync", message="cannot resolve")

    def test_campaign_invalid_report_spelling_is_rejected_before_resolution(self):
        self.data["agents"] = [{
            "id": "A001", "task_name": "/root/synthetic_fixture", "status": "running",
            "scope": "Synthetic fixture", "report": "docs/agents/reviews/A001.md\x00", "report_sha256": None,
        }]
        with patch.object(Path, "resolve", side_effect=AssertionError("invalid report reached filesystem")):
            with self.assertRaisesRegex(ValueError, "report path mismatch"):
                campaign.validate(self.data)

    def test_invalid_utf8_process_exit_codes_remain_project_two_campaign_one(self):
        self.state_path.write_bytes(b"\xff")
        self.ledger.write_bytes(b"\xff")
        tools = self.root / "tools"
        tools.mkdir()
        copied_campaign = tools / "campaign.py"
        copied_campaign.write_bytes(Path(campaign.__file__).read_bytes())
        before = self.snapshot()
        commands = [([sys.executable, project.__file__, "--root", str(self.root), "validate"], 2, "project:"),
                    ([sys.executable, str(copied_campaign), "validate"], 1, "Campaign error:")]
        for command, expected, prefix in commands:
            with self.subTest(cli=prefix):
                result = subprocess.run(command, capture_output=True, text=True, timeout=15)
                self.assertEqual(result.returncode, expected, result.stderr)
                self.assertTrue(result.stderr.startswith(prefix), result.stderr)
                self.assertNotIn("Traceback", result.stderr)
                self.assertEqual(self.snapshot(), before)
                self.assertFalse((self.root / "project/.project.lock").exists())


if __name__ == "__main__":
    unittest.main()
