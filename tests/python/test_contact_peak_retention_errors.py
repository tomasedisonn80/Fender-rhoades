"""Fault-injected retention boundaries; constructed bytes are not physics evidence."""
from __future__ import annotations

from contextlib import redirect_stderr, redirect_stdout
import errno
import hashlib
import io
import json
import os
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch

from test_contact_peak_cli import forced_clamp_payload
from test_contact_peak_schema import ROOT, encode

with patch.object(sys, "path", [str(ROOT / "tools"), *sys.path]):
    import validate_contact_peaks as cli


class ContactPeakRetentionErrorTests(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory(prefix="contact-peak-retention-")
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name)
        self.output = self.root / "evidence"
        self.raw = encode(forced_clamp_payload())
        self.candidates = [self.root / f"input-{index}.json" for index in range(1, 4)]
        for path in self.candidates:
            path.write_bytes(self.raw)

    def invoke(self, candidates=None):
        candidates = self.candidates if candidates is None else candidates
        arguments = ["--output", str(self.output)]
        for path in candidates:
            arguments.extend(("--candidate", str(path)))
        stdout, stderr = io.StringIO(), io.StringIO()
        with redirect_stdout(stdout), redirect_stderr(stderr):
            code = cli.main(arguments)
        return code, stdout.getvalue(), stderr.getvalue()

    def terminal_failure(self, result):
        code, stdout, stderr = result
        self.assertEqual(code, 3, stdout + stderr)
        self.assertEqual(stderr, "")
        summary = json.loads(stdout)
        manifest = json.loads((self.output / "manifest.json").read_bytes())
        for report in (summary, manifest):
            self.assertEqual(report["exit_code"], 3)
            self.assertEqual(report["status"], "operational_failure")
            self.assertEqual(report["phy002_acceptance"], "incomplete")
        return manifest

    def retained_success(self, manifest, index):
        record = manifest["cases"][index - 1]
        self.assertEqual(record["status"], "pass")
        self.assertEqual(record["exit_code"], 0)
        self.assertEqual((self.output / record["raw_file"]).read_bytes(), self.raw)
        self.assertEqual(json.loads((self.output / f"candidate-{index:04d}.record.json").read_bytes()), record)
        comparison = json.loads((self.output / record["comparison_file"]).read_bytes())
        self.assertEqual(comparison["exit_code"], 0)
        self.assertEqual(comparison["phy002_acceptance"], "incomplete")
        return record

    def test_interrupted_manifest_keeps_first_success_and_lists_unattempted_inputs(self):
        original_replace = os.replace
        replacements = 0
        preserved = {}

        def fail_first_replacement(source, destination):
            nonlocal replacements
            replacements += 1
            if replacements == 1:
                for suffix in ("raw", "comparison.json", "record.json"):
                    name = "candidate-0001." + suffix
                    preserved[name] = (self.output / name).read_bytes()
                raise OSError(errno.ENOSPC, "injected manifest replacement failure")
            return original_replace(source, destination)

        with patch.object(cli.os, "replace", side_effect=fail_first_replacement), \
                patch.object(cli, "_bounded_read", wraps=cli._bounded_read) as read:
            result = self.invoke()
        manifest = self.terminal_failure(result)
        self.assertEqual(replacements, 2, "only the terminal failure manifest may recover")
        self.assertEqual(read.call_count, 1, "an interrupted retention boundary must not retry input work")
        self.assertEqual(len(manifest["cases"]), 1)
        self.retained_success(manifest, 1)
        self.assertEqual(manifest["unattempted_inputs"], list(map(str, self.candidates[1:])))
        self.assertEqual(json.loads((self.output / "plan.json").read_bytes())["candidates"],
                         list(map(str, self.candidates)))
        self.assertIn("injected manifest replacement failure", manifest["operational_errors"][0])
        for name, raw in preserved.items():
            self.assertEqual((self.output / name).read_bytes(), raw)
        self.assertEqual(list(self.output.glob("candidate-000[23].*")), [])
        self.assertEqual(list(self.output.glob(".contact-peaks-*")), [])

    def test_manifest_cleanup_failure_does_not_mask_primary_replacement_error(self):
        self.output.mkdir()
        manifest_path = self.output / "manifest.json"
        previous = b'{"status":"running","cases":[{"status":"pass"}]}\n'
        manifest_path.write_bytes(previous)
        original_unlink = Path.unlink

        def fail_temporary_cleanup(path, *args, **kwargs):
            if path.parent == self.output and path.name.startswith(".contact-peaks-"):
                raise PermissionError(errno.EACCES, "injected secondary cleanup failure")
            return original_unlink(path, *args, **kwargs)

        with patch.object(cli.os, "replace", side_effect=OSError(
                errno.ENOSPC, "injected primary replacement failure")), \
                patch.object(Path, "unlink", new=fail_temporary_cleanup):
            with self.assertRaises(OSError) as caught:
                cli._replace_manifest(manifest_path, {"status": "operational_failure"})
        self.assertEqual(manifest_path.read_bytes(), previous)
        self.assertIn("injected primary replacement failure", str(caught.exception))

    def test_partial_raw_write_accounts_for_evidence_without_reopening_byte_budget(self):
        prefix = self.raw[:17]
        original_open = Path.open
        failed_path = self.output / "candidate-0002.raw"

        class PartialWriter:
            def __init__(self, handle):
                self.handle = handle

            def __getattr__(self, name):
                return getattr(self.handle, name)

            def __enter__(self):
                self.handle.__enter__()
                return self

            def __exit__(self, *error):
                return self.handle.__exit__(*error)

            def write(self, raw):
                self.handle.write(raw[:len(prefix)])
                self.handle.flush()
                raise OSError(errno.ENOSPC, "injected partial raw write")

        def interrupt_raw_write(path, mode="r", *args, **kwargs):
            handle = original_open(path, mode, *args, **kwargs)
            return PartialWriter(handle) if path == failed_path and mode == "xb" else handle

        with patch.object(Path, "open", new=interrupt_raw_write), \
                patch.object(cli, "MAX_COLLECTION_BYTES", 2 * len(self.raw)):
            result = self.invoke()
        manifest = self.terminal_failure(result)
        self.assertEqual(manifest["unattempted_inputs"], [])
        self.assertEqual([case["status"] for case in manifest["cases"]],
                         ["pass", "operational_failure", "input_limit_exceeded"])
        self.retained_success(manifest, 1)
        self.assertEqual(failed_path.read_bytes(), prefix)
        failed = manifest["cases"][1]
        self.assertEqual(failed["exit_code"], 3)
        self.assertIn("injected partial raw write", failed["error"])
        self.assertEqual(failed.get("raw_file"), failed_path.name)
        self.assertFalse(failed["raw_complete"])
        self.assertEqual(failed["retained_bytes"], len(prefix))
        self.assertEqual(failed["retained_sha256"], hashlib.sha256(prefix).hexdigest())
        self.assertNotIn("raw_sha256", failed, "partial bytes must not claim a whole-input hash")
        self.assertEqual(json.loads((self.output / "candidate-0002.record.json").read_bytes()), failed)
        self.assertFalse((self.output / "candidate-0002.comparison.json").exists())
        self.assertEqual((self.output / "candidate-0003.raw").read_bytes(), b"")
        self.assertFalse(manifest["cases"][2]["raw_complete"])
        self.assertEqual(manifest["retained_candidate_bytes"], len(self.raw) + len(prefix))

    def test_comparison_serialization_failure_remains_operational_and_continues(self):
        original_compare = cli.compare_candidate
        calls = 0

        def nonfinite_second_comparison(parsed, reference):
            nonlocal calls
            calls += 1
            comparison = original_compare(parsed, reference)
            if calls == 2:
                comparison["injected_nonfinite_result"] = float("nan")
            return comparison

        with patch.object(cli, "compare_candidate", side_effect=nonfinite_second_comparison):
            result = self.invoke()
        manifest = self.terminal_failure(result)
        self.assertEqual(calls, 3)
        self.assertEqual(manifest["unattempted_inputs"], [])
        self.retained_success(manifest, 1)
        self.retained_success(manifest, 3)
        failed = manifest["cases"][1]
        self.assertEqual(failed["status"], "operational_failure")
        self.assertEqual(failed["exit_code"], 3)
        self.assertIn("ValueError", failed["error"])
        self.assertTrue(failed["raw_complete"])
        self.assertEqual((self.output / failed["raw_file"]).read_bytes(), self.raw)
        self.assertFalse((self.output / "candidate-0002.comparison.json").exists())
        self.assertEqual(json.loads((self.output / "candidate-0002.record.json").read_bytes()), failed)

    def test_final_manifest_failure_reports_three_and_preserves_previous_manifest(self):
        original_replace = os.replace
        replacements = 0
        previous = None

        def fail_terminal_replacement(source, destination):
            nonlocal replacements, previous
            replacements += 1
            if replacements == 2:
                previous = Path(destination).read_bytes()
                raise OSError(errno.ENOSPC, "injected final manifest replacement failure")
            return original_replace(source, destination)

        with patch.object(cli.os, "replace", side_effect=fail_terminal_replacement):
            code, stdout, stderr = self.invoke(self.candidates[:1])
        self.assertEqual(code, 3)
        self.assertEqual(stdout, "", "a failed terminal write must not print a success summary")
        self.assertIn("Could not retain final manifest", stderr)
        self.assertIn("injected final manifest replacement failure", stderr)
        self.assertEqual(replacements, 2)
        self.assertIsNotNone(previous)
        self.assertEqual((self.output / "manifest.json").read_bytes(), previous)
        manifest = json.loads(previous)
        self.assertEqual(manifest["status"], "running")
        self.retained_success(manifest, 1)
        self.assertEqual(list(self.output.glob(".contact-peaks-*")), [])

    def test_source_change_is_operational_and_keeps_successful_comparison(self):
        before = cli._source_identity()
        after = dict(before)
        after["tools/validate_contact_peaks.py"] = "0" * 64
        self.assertNotEqual(before, after)
        with patch.object(cli, "_source_identity", side_effect=(before, after)):
            result = self.invoke(self.candidates[:1])
        manifest = self.terminal_failure(result)
        self.retained_success(manifest, 1)
        self.assertFalse(manifest["source_unchanged"])
        self.assertEqual(manifest["source_sha256_before"], before)
        self.assertEqual(manifest["source_sha256_after"], after)
        self.assertEqual(manifest["unattempted_inputs"], [])
        self.assertIn("diagnostic source changed", manifest["operational_errors"][0])

    @unittest.skipUnless(hasattr(os, "mkfifo"), "requires POSIX FIFO support")
    def test_fifo_and_final_symlink_are_refused_before_opening(self):
        symlink, fifo = self.root / "input-link.json", self.root / "input.fifo"
        symlink.symlink_to(self.candidates[0])
        os.mkfifo(fifo)
        refused = {symlink, fifo}
        original_open = os.open

        def guarded_open(path, *args, **kwargs):
            self.assertNotIn(Path(path), refused, "refused inputs must never be opened")
            return original_open(path, *args, **kwargs)

        with patch.object(cli.os, "open", side_effect=guarded_open):
            result = self.invoke([symlink, fifo, self.candidates[0]])
        manifest = self.terminal_failure(result)
        self.assertEqual(manifest["unattempted_inputs"], [])
        for index, record in enumerate(manifest["cases"][:2], 1):
            self.assertEqual(record["status"], "operational_failure")
            self.assertEqual(record["exit_code"], 3)
            self.assertIn("regular file", record["error"])
            self.assertFalse((self.output / f"candidate-{index:04d}.raw").exists())
        self.retained_success(manifest, 3)


if __name__ == "__main__":
    unittest.main()
