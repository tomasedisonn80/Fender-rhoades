"""Independent preflight and partial-attempt checks without an audio replay."""
from __future__ import annotations

import copy
from contextlib import redirect_stderr, redirect_stdout
import errno
import importlib.util
import io
import json
import os
from pathlib import Path
import sys
import tempfile
import unittest
from unittest import mock

ROOT = Path(__file__).resolve().parents[2]
TOOL = ROOT / "tools/verify_audio_compatibility.py"
SPEC = importlib.util.spec_from_file_location("adversarial_audio_compatibility", TOOL)
compatibility = importlib.util.module_from_spec(SPEC)
sys.path.insert(0, str(ROOT / "tools"))
try:
    SPEC.loader.exec_module(compatibility)
finally:
    sys.path.pop(0)


class IndependentCompatibilityBoundaries(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.q6_bytes = compatibility.Q6_BASELINE.read_bytes()
        cls.wav_bytes = compatibility.WAV_BASELINE.read_bytes()
        cls.q6_document = json.loads(cls.q6_bytes)
        cls.cases = compatibility.q6.build_cases("full")

    def invoke_main(self, output):
        argv = [str(TOOL), "--probe", sys.executable, "--renderer", sys.executable,
                "--output", str(output)]
        with mock.patch.object(sys, "argv", argv), redirect_stderr(io.StringIO()), \
                redirect_stdout(io.StringIO()):
            return compatibility.main()

    def test_bad_comparison_digests_rejected_before_any_process_or_output(self):
        mutations = (
            ("raw_array_sha256", []),
            ("raw_array_sha256", ["0" * 64]),
            ("raw_array_sha256", ["0" * 64] * 6),
            ("raw_array_sha256", {"0": "0" * 64}),
            ("raw_array_sha256", ["0" * 64] * 4 + [False]),
            ("raw_array_sha256", ["invalid"] + ["0" * 64] * 4),
            ("probe_stdout_sha256", None),
            ("probe_stderr_sha256", "F" * 64),
        )
        columns = self.q6_document["case_table_schema"]["columns"]
        for field, value in mutations:
            with self.subTest(field=field, value=value), tempfile.TemporaryDirectory() as directory:
                document = copy.deepcopy(self.q6_document)
                document["cases"][0][columns.index(field)] = value
                output = Path(directory) / "new-output"
                with mock.patch.object(compatibility, "baseline_bytes", side_effect=[
                        json.dumps(document).encode("utf-8"), self.wav_bytes]), \
                        mock.patch.object(compatibility.subprocess, "check_output") as git, \
                        mock.patch.object(compatibility.q6.reference, "run_probe") as probe, \
                        mock.patch.object(compatibility, "render_with_retention") as renderer:
                    self.assertEqual(self.invoke_main(output), 3)
                    git.assert_not_called()
                    probe.assert_not_called()
                    renderer.assert_not_called()
                self.assertFalse(output.exists())

    def test_valid_json_with_wrong_probe_model_keeps_original_bytes(self):
        raw, diagnostic = b'{"model":"unexpected-model"}\n', b"stderr prefix\xff"
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory) / "new-output"
            with mock.patch.object(compatibility.q6.reference, "run_probe",
                                   return_value=(raw, diagnostic)), \
                    mock.patch.object(compatibility.q6, "numerical_libraries", return_value=(None, None)), \
                    mock.patch.object(compatibility, "snapshot", return_value={}):
                self.assertEqual(self.invoke_main(output), 3)
            first = self.cases[0].id
            self.assertEqual((output / "probe" / (first + ".stdout.json")).read_bytes(), raw)
            self.assertEqual((output / "probe" / (first + ".stderr.txt")).read_bytes(), diagnostic)
            report = json.loads((output / "result.json").read_bytes())
            self.assertIn("Unexpected probe model identity", report["errors"][0])
            self.assertEqual(report["summary"]["q6_cases"], 0)
            self.assertIsNone(report["summary"]["q6_frames"])

    def test_later_probe_failure_preserves_prior_mismatch_and_attempt_prefix(self):
        failure = compatibility.q6.reference.ProbeFailure(
            "second attempt failed", b"partial second output", b"second diagnostic")
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory) / "new-output"
            with mock.patch.object(compatibility.q6.reference, "run_probe", side_effect=[
                    (b"first output", b""), failure]), \
                    mock.patch.object(compatibility.q6, "parse_probe_output", return_value=([], {})), \
                    mock.patch.object(compatibility.q6, "array_hash", return_value="0" * 64), \
                    mock.patch.object(compatibility, "snapshot", return_value={}):
                self.assertEqual(self.invoke_main(output), 3)
            report = json.loads((output / "result.json").read_bytes())
            self.assertEqual(report["summary"]["q6_cases"], 1)
            self.assertEqual(report["summary"]["q6_exact_matches"], 0)
            self.assertFalse(report["q6_cases"][0][-1])
            self.assertIsNone(report["summary"]["q6_frames"])
            self.assertEqual(report["summary"]["wav_cases"], 0)
            self.assertIn("second attempt failed", report["errors"][0])
            self.assertEqual((output / "probe" / (self.cases[0].id + ".stdout.json")).read_bytes(),
                             b"first output")
            self.assertEqual((output / "probe" / (self.cases[1].id + ".stdout.json")).read_bytes(),
                             failure.stdout)
            self.assertEqual((output / "probe" / (self.cases[1].id + ".stderr.txt")).read_bytes(),
                             failure.stderr)

    def test_existing_output_regular_file_is_never_replaced(self):
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory) / "existing-file"
            output.write_bytes(b"retain existing bytes")
            with mock.patch.object(compatibility.q6.reference, "run_probe") as probe, \
                    mock.patch.object(compatibility, "snapshot", return_value={}):
                self.assertEqual(self.invoke_main(output), 3)
                probe.assert_not_called()
            self.assertEqual(output.read_bytes(), b"retain existing bytes")

    def test_renderer_symlink_wav_is_rejected_after_stream_retention(self):
        with tempfile.TemporaryDirectory() as directory:
            directory = Path(directory)
            target, destination = directory / "target.wav", directory / "render.wav"
            target.write_bytes(b"do not treat linked bytes as new renderer output")
            try:
                os.symlink(target, destination)
            except OSError as error:
                if error.errno in {errno.EPERM, errno.EACCES, errno.ENOSYS,
                                   getattr(errno, "ENOTSUP", None)}:
                    self.skipTest("symlink creation is unsupported by this environment")
                raise
            with self.assertRaisesRegex(RuntimeError, "did not produce a regular WAV"):
                compatibility.render_with_retention(
                    [sys.executable, "-c", "import sys; sys.stdout.buffer.write(b'done'); "
                     "sys.stderr.buffer.write(b'diagnostic')"],
                    directory, "linked", destination=destination)
            self.assertEqual((directory / "linked.stdout.txt").read_bytes(), b"done")
            self.assertEqual((directory / "linked.stderr.txt").read_bytes(), b"diagnostic")
            self.assertEqual(target.read_bytes(), b"do not treat linked bytes as new renderer output")


if __name__ == "__main__":
    unittest.main()
