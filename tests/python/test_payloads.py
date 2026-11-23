"""Local byte-integrity fixtures; all payloads are invented, never hardware evidence."""
from __future__ import annotations

from contextlib import redirect_stderr, redirect_stdout
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
SPEC = importlib.util.spec_from_file_location("rhodes_payload_verifier", ROOT / "tools/verify_payloads.py")
assert SPEC and SPEC.loader
verifier = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(verifier)


class PayloadVerificationTests(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory()
        self.addCleanup(temporary.cleanup)
        # macOS can expose the temporary directory through a system symlink.
        self.work = Path(temporary.name).resolve()
        self.payload_root = self.work / "raw"
        self.payload_root.mkdir()
        self.manifest = self.work / "manifest.json"
        self.report = self.work / "report.json"
        self.payload = self.payload_root / "nested" / "test-only.bin"
        self.payload.parent.mkdir()
        self.contents = b"Invented raw payload for verifier tests.\x00\xff\n"
        self.payload.write_bytes(self.contents)
        self.artifact = {"path": "nested/test-only.bin", "sha256": self.digest(self.contents), "bytes": len(self.contents)}
        self.write_manifest([self.artifact])

    @staticmethod
    def digest(data):
        return hashlib.sha256(data).hexdigest()

    def write_manifest(self, artifacts):
        self.manifest.write_text(json.dumps({"artifacts": artifacts, "notes": "Invented test-only bytes"}), encoding="utf-8")

    def cli(self, *, manifest=None, root=None, report=None):
        output, error = io.StringIO(), io.StringIO()
        with redirect_stdout(output), redirect_stderr(error):
            status = verifier.main(["--manifest", str(self.manifest if manifest is None else manifest),
                                    "--payload-root", str(self.payload_root if root is None else root),
                                    "--report", str(self.report if report is None else report)])
        return status, output.getvalue(), error.getvalue()

    def read_report(self):
        return json.loads(self.report.read_text(encoding="utf-8"))

    def symlink(self, path, target, *, directory=False):
        try:
            path.symlink_to(target, target_is_directory=directory)
        except (OSError, NotImplementedError):
            self.skipTest("Host does not permit symlinks")

    def test_cli_streams_multiple_files_and_binds_exact_manifest_bytes(self):
        contents = bytes(range(256)) * (verifier.CHUNK_SIZE // 256 + 11)
        self.payload.write_bytes(contents)
        first = {**self.artifact, "bytes": len(contents), "sha256": self.digest(contents)}
        (self.payload_root / "second.bin").write_bytes(b"second invented payload")
        second = {"path": "second.bin", "bytes": 23, "sha256": self.digest(b"second invented payload")}
        self.write_manifest([first, second])
        manifest_bytes = self.manifest.read_bytes()
        result = subprocess.run([sys.executable, str(ROOT / "tools/verify_payloads.py"),
                                 "--manifest", str(self.manifest), "--payload-root", str(self.payload_root),
                                 "--report", str(self.report)], capture_output=True, text=True, timeout=10)
        self.assertEqual(result.returncode, 0, result.stderr)
        report = self.read_report()
        self.assertEqual(report["result"], "pass")
        self.assertEqual(report["tool"]["version"], verifier.VERSION)
        self.assertEqual(report["manifest"]["sha256"], self.digest(manifest_bytes))
        self.assertEqual(report["manifest"]["bytes"], len(manifest_bytes))
        self.assertEqual(report["payload_root"], str(self.payload_root))
        for actual, expected in zip(report["artifacts"], [first, second]):
            self.assertEqual(actual["result"], "pass")
            self.assertEqual(actual["observed"], {"sha256": expected["sha256"], "bytes": expected["bytes"], "read_complete": True})
            self.assertEqual(actual["failures"], [])
        self.assertEqual(self.payload.read_bytes(), contents)
        self.assertEqual(self.manifest.read_bytes(), manifest_bytes)
        self.assertEqual(list(self.work.glob(".payload-report-*")), [])

    def test_mismatch_missing_and_valid_files_are_all_reported(self):
        missing = {**self.artifact, "path": "absent.bin"}
        bad = {**self.artifact, "sha256": "0" * 64, "bytes": len(self.contents) + 10}
        (self.payload_root / "valid.bin").write_bytes(self.contents)
        valid = {**self.artifact, "path": "valid.bin"}
        self.write_manifest([bad, missing, valid])
        self.assertEqual(self.cli()[0], 1)
        report = self.read_report()
        self.assertEqual(report["result"], "fail")
        first, absent, good = report["artifacts"]
        self.assertEqual(first["failures"], ["bytes mismatch", "sha256 mismatch"])
        self.assertEqual(first["observed"]["bytes"], len(self.contents))
        self.assertEqual(first["observed"]["sha256"], self.digest(self.contents))
        self.assertEqual(absent["result"], "fail")
        self.assertIsNone(absent["observed"]["sha256"])
        self.assertEqual(good["result"], "pass")

    def test_truncated_file_is_not_accepted(self):
        self.payload.write_bytes(self.contents[:5])
        self.assertEqual(self.cli()[0], 1)
        result = self.read_report()["artifacts"][0]
        self.assertEqual(result["observed"]["bytes"], 5)
        self.assertEqual(result["failures"], ["bytes mismatch", "sha256 mismatch"])

    def test_directories_and_fifos_are_rejected_without_reading(self):
        self.payload.unlink()
        self.payload.mkdir()
        self.assertEqual(self.cli()[0], 1)
        self.assertIn("regular file required", self.read_report()["artifacts"][0]["failures"][0])
        if hasattr(os, "mkfifo"):
            self.report.unlink()
            self.payload.rmdir()
            os.mkfifo(self.payload)
            result = subprocess.run([sys.executable, str(ROOT / "tools/verify_payloads.py"),
                                     "--manifest", str(self.manifest), "--payload-root", str(self.payload_root),
                                     "--report", str(self.report)], capture_output=True, text=True, timeout=5)
            self.assertEqual(result.returncode, 1, result.stderr)
            self.assertIsNone(self.read_report()["artifacts"][0]["observed"]["bytes"])

    def test_file_and_parent_symlinks_fail_even_when_they_stay_inside_root(self):
        real = self.payload_root / "real.bin"
        real.write_bytes(self.contents)
        self.payload.unlink()
        self.symlink(self.payload, real)
        self.assertEqual(self.cli()[0], 1)
        self.assertIn("symlink", self.read_report()["artifacts"][0]["failures"][0])
        self.report.unlink()
        self.payload.unlink()
        self.payload.parent.rmdir()
        self.symlink(self.payload.parent, self.payload_root, directory=True)
        self.write_manifest([{**self.artifact, "path": "nested/real.bin"}])
        self.assertEqual(self.cli()[0], 1)
        self.assertIn("symlink", self.read_report()["artifacts"][0]["failures"][0])

    def test_root_and_ancestor_symlinks_are_rejected(self):
        alias = self.work / "root-link"
        self.symlink(alias, self.payload_root, directory=True)
        self.assertEqual(self.cli(root=alias)[0], 2)
        self.assertFalse(self.report.exists())
        ancestor = self.work / "ancestor-link"
        self.symlink(ancestor, self.work, directory=True)
        self.assertEqual(self.cli(root=ancestor / "raw")[0], 2)
        # '..' must not normalize away a symlink before component inspection.
        self.assertEqual(self.cli(root=alias / ".." / "raw")[0], 2)

    def test_manifest_symlink_is_reported_without_reading_its_target(self):
        alias = self.work / "manifest-link.json"
        self.symlink(alias, self.manifest)
        self.assertEqual(self.cli(manifest=alias)[0], 1)
        report = self.read_report()
        self.assertIsNone(report["manifest"]["sha256"])
        self.assertEqual(report["artifacts"], [])
        self.assertIn("symlink", report["failures"][0])

    def test_manifest_symlink_loop_is_rejected_cleanly(self):
        alias = self.work / "manifest-loop.json"
        self.symlink(alias, alias)
        self.assertEqual(self.cli(manifest=alias)[0], 1)
        self.assertIn("symlink", self.read_report()["failures"][0])

    def test_unsafe_artifact_paths_reject_the_whole_manifest(self):
        for path in ("../outside", "/absolute", "a/../b", "a//b", "./file", "raw/", ".git/config",
                     "https://example.invalid/raw", "C:/raw", "a\\b", "a\x00b", "a\nb"):
            with self.subTest(path=path):
                self.write_manifest([self.artifact, {**self.artifact, "path": path}])
                with patch.object(verifier, "verify_artifact", side_effect=AssertionError("invalid manifest opened a payload")):
                    self.assertEqual(self.cli()[0], 1)
                self.assertEqual(self.read_report()["artifacts"], [])
                self.report.unlink()

    def test_strict_json_and_artifact_types(self):
        a = self.artifact
        invalid = ['[]', '{"artifacts": [], "artifacts": []}', '{"artifacts": [], "x":NaN}',
                   '{"artifacts": [], "nested":{"x":Infinity}}', '{"artifacts": [], "x":1e999}',
                   '{"artifacts":', '{}', '{"artifacts": []}', '{"artifacts": {}}',
                   json.dumps({"artifacts": [a, a]}), json.dumps({"artifacts": [None]})]
        for field, value in (("path", 2), ("sha256", "A" * 64), ("sha256", "a" * 63),
                             ("bytes", True), ("bytes", 0), ("bytes", -1), ("bytes", 1.5), ("bytes", "5")):
            invalid.append(json.dumps({"artifacts": [{**a, field: value}]}))
        invalid.append(json.dumps({"artifacts": [{**a, "command": "never execute"}]}))
        invalid.append(json.dumps({"artifacts": [{"path": a["path"], "bytes": a["bytes"]}]}))
        for raw in invalid:
            with self.subTest(raw=raw):
                self.manifest.write_text(raw, encoding="utf-8")
                self.assertEqual(self.cli()[0], 1)
                report = self.read_report()
                self.assertEqual(report["result"], "fail")
                self.assertTrue(report["failures"])
                self.assertEqual(report["manifest"]["sha256"], self.digest(raw.encode("utf-8")))
                self.report.unlink()

    def test_unreadable_payload_is_a_failure(self):
        real_open = os.open

        def unreadable(path, flags, *args, **kwargs):
            if Path(path) == self.payload:
                raise PermissionError("simulated access denied")
            return real_open(path, flags, *args, **kwargs)

        with patch.object(verifier.os, "open", side_effect=unreadable):
            self.assertEqual(self.cli()[0], 1)
        result = self.read_report()["artifacts"][0]
        self.assertIn("access denied", result["failures"][0])
        self.assertIsNone(result["observed"]["bytes"])

    def test_read_failure_retains_partial_hash_and_count(self):
        real_read, target_identity = os.read, verifier.identity(self.payload.stat())
        chunks = 0

        def broken_read(descriptor, size):
            nonlocal chunks
            if verifier.identity(os.fstat(descriptor)) == target_identity:
                chunks += 1
                if chunks == 2:
                    raise OSError("simulated read interruption")
                return real_read(descriptor, 5)
            return real_read(descriptor, size)

        with patch.object(verifier.os, "read", side_effect=broken_read):
            self.assertEqual(self.cli()[0], 1)
        result = self.read_report()["artifacts"][0]
        self.assertEqual(result["observed"], {"bytes": 5, "sha256": self.digest(self.contents[:5]), "read_complete": False})
        self.assertIn("read interruption", result["failures"][0])

    def test_mutation_after_read_still_fails_when_observed_digest_matches(self):
        real_read, original_stat = os.read, self.payload.stat()
        changed = False

        def mutate_after_read(descriptor, size):
            nonlocal changed
            chunk = real_read(descriptor, size)
            if not changed and verifier.identity(os.fstat(descriptor)) == verifier.identity(original_stat):
                changed = True
                self.payload.write_bytes(b"x" * len(self.contents))
                os.utime(self.payload, ns=(original_stat.st_atime_ns, original_stat.st_mtime_ns + 1_000_000_000))
            return chunk

        with patch.object(verifier.os, "read", side_effect=mutate_after_read):
            self.assertEqual(self.cli()[0], 1)
        result = self.read_report()["artifacts"][0]
        self.assertEqual(result["observed"]["sha256"], self.artifact["sha256"])
        self.assertIn("file changed while reading", result["failures"][0])

    def test_path_replacement_after_final_fstat_is_detected(self):
        real_fstat, target_identity = os.fstat, verifier.identity(self.payload.stat())
        calls = 0

        def replace_after_fstat(descriptor):
            nonlocal calls
            info = real_fstat(descriptor)
            if verifier.identity(info) == target_identity:
                calls += 1
                if calls == 2:
                    replacement = self.payload.with_suffix(".replacement")
                    replacement.write_bytes(self.contents)
                    try:
                        os.replace(replacement, self.payload)
                    except PermissionError:
                        self.skipTest("Host cannot replace an open file")
            return info

        with patch.object(verifier.os, "fstat", side_effect=replace_after_fstat):
            self.assertEqual(self.cli()[0], 1)
        self.assertIn("pathname changed while reading", self.read_report()["artifacts"][0]["failures"][0])

    def test_existing_report_and_input_aliases_are_never_overwritten(self):
        self.report.write_bytes(b"existing immutable report")
        self.assertEqual(self.cli()[0], 2)
        self.assertEqual(self.report.read_bytes(), b"existing immutable report")
        self.report.unlink()
        manifest_bytes = self.manifest.read_bytes()
        self.assertEqual(self.cli(report=self.manifest)[0], 2)
        self.assertEqual(self.manifest.read_bytes(), manifest_bytes)
        try:
            os.link(self.payload, self.report)
        except (OSError, NotImplementedError):
            self.skipTest("Host does not support hard links")
        self.assertEqual(self.cli()[0], 2)
        self.assertEqual(self.payload.read_bytes(), self.contents)

    def test_reports_inside_payload_root_or_symlinked_parent_are_rejected(self):
        inside = self.payload_root / "report.json"
        self.assertEqual(self.cli(report=inside)[0], 2)
        self.assertFalse(inside.exists())
        alias = self.work / "report-parent-link"
        self.symlink(alias, self.payload_root, directory=True)
        self.assertEqual(self.cli(report=alias / "report.json")[0], 2)
        self.assertFalse(inside.exists())

    def test_concurrent_report_creation_wins_without_overwrite(self):
        real_link = os.link

        def publish_competitor(source, destination, *args, **kwargs):
            Path(destination).write_bytes(b"concurrent report wins")
            return real_link(source, destination, *args, **kwargs)

        with patch.object(verifier.os, "link", side_effect=publish_competitor):
            self.assertEqual(self.cli()[0], 2)
        self.assertEqual(self.report.read_bytes(), b"concurrent report wins")
        self.assertEqual(list(self.work.glob(".payload-report-*")), [])

    def test_urls_are_rejected_for_every_cli_path(self):
        for field in ("manifest", "root", "report"):
            with self.subTest(field=field):
                status, _, error = self.cli(**{field: "https://example.invalid/input"})
                self.assertEqual(status, 2)
                self.assertIn("local filesystem paths required", error)
                self.assertFalse(self.report.exists())

    def test_cleanup_failure_preserves_published_result(self):
        with patch.object(verifier.Path, "unlink", side_effect=PermissionError("cleanup blocked")):
            status, output, error = self.cli()
        self.assertEqual(status, 0)
        self.assertIn("Payload integrity pass", output)
        self.assertIn("temporary cleanup failed", error)
        self.assertNotIn("could not publish", error)
        self.assertEqual(self.read_report()["result"], "pass")
        self.assertEqual(self.read_report()["manifest"]["sha256"], self.digest(self.manifest.read_bytes()))
        self.assertEqual(len(list(self.work.glob(".payload-report-*"))), 1)

    def test_cleanup_failure_does_not_mask_publication_error(self):
        with patch.object(verifier.os, "link", side_effect=PermissionError("publication blocked")), \
                patch.object(verifier.Path, "unlink", side_effect=PermissionError("cleanup blocked")):
            status, _, error = self.cli()
        self.assertEqual(status, 2)
        self.assertIn("temporary cleanup failed", error)
        self.assertIn("could not publish a report: publication blocked", error)
        self.assertFalse(self.report.exists())


if __name__ == "__main__":
    unittest.main()
