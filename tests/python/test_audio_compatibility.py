"""Replay failure retention and frozen-input checks; no full audio study is run."""
from __future__ import annotations

import copy
from contextlib import redirect_stderr, redirect_stdout
import importlib.util
import io
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest import mock

ROOT = Path(__file__).resolve().parents[2]
TOOL = ROOT / "tools/verify_audio_compatibility.py"
SPEC = importlib.util.spec_from_file_location("audio_compatibility_tests", TOOL)
compatibility = importlib.util.module_from_spec(SPEC)
sys.path.insert(0, str(ROOT / "tools"))
try:
    SPEC.loader.exec_module(compatibility)
finally:
    sys.path.pop(0)


class HistoricalInputTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.legacy = json.loads(compatibility.WAV_BASELINE.read_bytes())

    def test_actual_24_case_contract_is_accepted(self):
        compatibility.validate_legacy(self.legacy)

    def test_duplicated_fixture_cannot_replace_missing_coverage(self):
        document = copy.deepcopy(self.legacy)
        document["cases"][1] = document["cases"][0]
        with self.assertRaisesRegex(ValueError, "Historical WAV input changed"):
            compatibility.validate_legacy(document)

    def test_changed_names_cannot_escape_or_collide_with_retained_output(self):
        for field, value in (("file_name", "../outside.wav"),
                             ("file_name", "/outside.wav"),
                             ("file_name", "C:\\outside.wav"),
                             ("file_name", "direct-r32000-k00.stdout.txt"),
                             ("case_id", "../outside")):
            with self.subTest(field=field, value=value):
                document = copy.deepcopy(self.legacy)
                document["cases"][0][field] = value
                with self.assertRaises(ValueError):
                    compatibility.validate_legacy(document)

    def test_changed_parameters_counts_and_digest_are_rejected(self):
        for field, value in (("velocity", 0.8), ("seconds", 0.03),
                             ("monitoring_gain", 1.0), ("key", False),
                             ("sample_rate_hz", 32000.0), ("frame_count", 641),
                             ("wav_sha256", "x" * 64)):
            with self.subTest(field=field):
                document = copy.deepcopy(self.legacy)
                document["cases"][0][field] = value
                with self.assertRaises(ValueError):
                    compatibility.validate_legacy(document)
        document = copy.deepcopy(self.legacy)
        document["total_frame_count"] += 1
        with self.assertRaisesRegex(ValueError, "total frame count"):
            compatibility.validate_legacy(document)

    def test_ambiguous_nonfinite_and_nonobject_json_rejected(self):
        for raw in (b'{"a":1,"a":2}', b'{"a":NaN}', b'{"a":Infinity}',
                    b'{"a":1e400}', b'[]'):
            with self.subTest(raw=raw), self.assertRaises(ValueError):
                compatibility.baseline_document(raw)

    def test_baseline_read_is_bounded(self):
        with tempfile.TemporaryDirectory() as temporary:
            path = Path(temporary) / "baseline.json"
            path.write_bytes(b"123456789")
            with mock.patch.object(compatibility, "BASELINE_BYTE_LIMIT", 8):
                with self.assertRaisesRegex(ValueError, "exceeds"):
                    compatibility.baseline_bytes(path)
                path.write_bytes(b"12345678")
                self.assertEqual(compatibility.baseline_bytes(path), b"12345678")

    def test_duplicate_q6_columns_and_negative_metadata_index_fail_before_launch(self):
        original = json.loads(compatibility.Q6_BASELINE.read_bytes())
        for alteration in ("columns", "metadata"):
            with self.subTest(alteration=alteration), tempfile.TemporaryDirectory() as temporary:
                document = copy.deepcopy(original)
                columns = document["case_table_schema"]["columns"]
                if alteration == "columns":
                    columns[-1] = columns[0]
                else:
                    document["cases"][0][columns.index("probe_metadata_index")] = -1
                with mock.patch.object(compatibility, "baseline_bytes", side_effect=[
                        json.dumps(document).encode(), json.dumps(self.legacy).encode()]), \
                        mock.patch.object(compatibility.q6.reference, "run_probe") as child:
                    output = Path(temporary) / "output"
                    with self.assertRaises(ValueError):
                        compatibility.run(Path(sys.executable), Path(sys.executable), output)
                    child.assert_not_called()
                    self.assertFalse(output.exists())


class RendererRetentionTests(unittest.TestCase):
    def test_success_retains_streams_and_returns_completed_process(self):
        with tempfile.TemporaryDirectory() as temporary:
            directory = Path(temporary)
            destination = directory / "render.wav"
            code = ("import pathlib,sys; pathlib.Path(sys.argv[1]).write_bytes(b'fixture'); "
                    "sys.stdout.buffer.write(b'completed')")
            result = compatibility.render_with_retention(
                [sys.executable, "-c", code, str(destination)], directory, "success",
                destination=destination)
            self.assertEqual(result.returncode, 0)
            self.assertEqual(destination.read_bytes(), b"fixture")
            self.assertEqual((directory / "success.stdout.txt").read_bytes(), b"completed")
            self.assertEqual((directory / "success.stderr.txt").read_bytes(), b"")

    def test_nonzero_exit_preserves_actual_binary_streams_and_partial_file(self):
        with tempfile.TemporaryDirectory() as temporary:
            directory = Path(temporary)
            partial = directory / "partial.wav"
            code = ("import pathlib,sys; pathlib.Path(sys.argv[1]).write_bytes(b'RIFF'); "
                    "sys.stdout.buffer.write(bytes([0,255,10])); "
                    "sys.stderr.buffer.write(bytes([195,169,10])); sys.exit(7)")
            with self.assertRaisesRegex(RuntimeError, r"Renderer failed \(7\)"):
                compatibility.render_with_retention(
                    [sys.executable, "-c", code, str(partial)], directory, "failure")
            self.assertEqual((directory / "failure.stdout.txt").read_bytes(), b"\x00\xff\n")
            self.assertEqual((directory / "failure.stderr.txt").read_bytes(), b"\xc3\xa9\n")
            self.assertEqual(partial.read_bytes(), b"RIFF")

    def test_success_without_wav_is_operational_failure_with_retained_streams(self):
        with tempfile.TemporaryDirectory() as temporary:
            directory = Path(temporary)
            with self.assertRaisesRegex(RuntimeError, "did not produce a regular WAV"):
                compatibility.render_with_retention(
                    [sys.executable, "-c", "print('no wav')"], directory, "missing",
                    destination=directory / "missing.wav")
            self.assertEqual((directory / "missing.stdout.txt").read_bytes().splitlines(), [b"no wav"])
            self.assertEqual((directory / "missing.stderr.txt").read_bytes(), b"")

    def test_timeout_preserves_actual_child_prefix_streams(self):
        with tempfile.TemporaryDirectory() as temporary:
            directory = Path(temporary)
            code = ("import sys,time; sys.stdout.buffer.write(b'partial stdout\\n'); "
                    "sys.stdout.buffer.flush(); sys.stderr.buffer.write(b'partial stderr\\n'); "
                    "sys.stderr.buffer.flush(); time.sleep(30)")
            with self.assertRaises(subprocess.TimeoutExpired):
                compatibility.render_with_retention(
                    [sys.executable, "-c", code], directory, "timeout", timeout=3)
            self.assertEqual((directory / "timeout.stdout.txt").read_bytes(), b"partial stdout\n")
            self.assertEqual((directory / "timeout.stderr.txt").read_bytes(), b"partial stderr\n")


class ReplayFailureTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.output = Path(self.temporary.name) / "replay"
        self.first = compatibility.q6.build_cases("full")[0]
        self.error = compatibility.q6.reference.ProbeFailure(
            "synthetic process failure", b"partial probe", b"diagnostic stderr")

    def run_failure(self, snapshot_effect=None, probe=None):
        with mock.patch.object(compatibility.q6.reference, "run_probe", side_effect=self.error), \
                mock.patch.object(compatibility, "snapshot", side_effect=snapshot_effect,
                                  return_value={"source": "fixed"}), \
                redirect_stdout(io.StringIO()):
            return compatibility.run(probe or Path(sys.executable),
                                     Path(sys.executable), self.output)

    def report(self):
        return json.loads((self.output / "result.json").read_bytes())

    def test_failed_probe_keeps_raw_streams_and_incomplete_report(self):
        self.assertEqual(self.run_failure(), 3)
        directory = self.output / "probe"
        self.assertEqual((directory / f"{self.first.id}.stdout.json").read_bytes(), b"partial probe")
        self.assertEqual((directory / f"{self.first.id}.stderr.txt").read_bytes(), b"diagnostic stderr")
        self.assertEqual((directory / f"{self.first.id}.stdin.txt").read_bytes(),
                         compatibility.q6.reference.probe_input(self.first))
        report = self.report()
        self.assertEqual(report["exit_code"], 3)
        self.assertEqual(report["summary"]["q6_cases"], 0)
        self.assertIsNone(report["summary"]["q6_frames"])
        self.assertTrue(report["unchanged_inputs_and_binaries"])

    def test_unreadable_final_source_keeps_primary_error_and_report(self):
        self.assertEqual(self.run_failure([
            {"source": "fixed"}, FileNotFoundError("source removed during replay")]), 3)
        report = self.report()
        self.assertFalse(report["unchanged_inputs_and_binaries"])
        self.assertEqual(len(report["errors"]), 2)
        self.assertIn("synthetic process failure", report["errors"][0])
        self.assertIn("Final integrity check", report["errors"][1])
        self.assertIn("source removed", report["errors"][1])

    def test_removed_binary_is_recorded_as_failed_final_integrity(self):
        probe = Path(self.temporary.name) / "synthetic-probe"
        probe.write_bytes(b"declared binary bytes; execution is injected")

        def remove_and_fail(*_):
            probe.unlink()
            raise self.error

        with mock.patch.object(compatibility.q6.reference, "run_probe", side_effect=remove_and_fail), \
                mock.patch.object(compatibility, "snapshot", return_value={"source": "fixed"}), \
                redirect_stdout(io.StringIO()):
            result = compatibility.run(probe, Path(sys.executable), self.output)
        self.assertEqual(result, 3)
        report = self.report()
        self.assertFalse(report["unchanged_inputs_and_binaries"])
        self.assertIn("Final integrity check", report["errors"][1])
        self.assertTrue((self.output / "probe" / f"{self.first.id}.stdout.json").is_file())

    def test_invalid_probe_json_is_retained_before_interpretation(self):
        with mock.patch.object(compatibility.q6.reference, "run_probe", return_value=(b'{"bad":', b'bad')), \
                mock.patch.object(compatibility.q6, "numerical_libraries", return_value=(None, None)), \
                mock.patch.object(compatibility, "snapshot", return_value={"source": "fixed"}), \
                redirect_stdout(io.StringIO()):
            result = compatibility.run(Path(sys.executable), Path(sys.executable), self.output)
        self.assertEqual(result, 3)
        self.assertEqual((self.output / "probe" / f"{self.first.id}.stdout.json").read_bytes(), b'{"bad":')
        self.assertIn("JSONDecodeError", self.report()["errors"][0])

    def test_existing_output_is_preserved_and_no_child_runs(self):
        self.output.mkdir()
        marker = self.output / "keep.txt"
        marker.write_bytes(b"original")
        with mock.patch.object(compatibility.q6.reference, "run_probe") as child, \
                mock.patch.object(compatibility, "snapshot", return_value={}):
            with self.assertRaises(FileExistsError):
                compatibility.run(Path(sys.executable), Path(sys.executable), self.output)
            child.assert_not_called()
        self.assertEqual(marker.read_bytes(), b"original")
        self.assertEqual(list(self.output.iterdir()), [marker])

    def test_child_directory_creation_failure_keeps_operational_report(self):
        real_mkdir = Path.mkdir

        def fail_wav_directory(path, *args, **kwargs):
            if path == self.output / "wav":
                raise OSError("injected WAV directory failure")
            return real_mkdir(path, *args, **kwargs)

        with mock.patch.object(Path, "mkdir", fail_wav_directory), \
                mock.patch.object(compatibility.q6.reference, "run_probe") as child, \
                mock.patch.object(compatibility, "snapshot", return_value={}), \
                redirect_stdout(io.StringIO()):
            result = compatibility.run(Path(sys.executable), Path(sys.executable), self.output)
            child.assert_not_called()
        self.assertEqual(result, 3)
        self.assertIn("injected WAV directory failure", self.report()["errors"][0])

    def test_failed_git_preflight_is_exit_three_without_creating_output(self):
        argv = [str(TOOL), "--probe", sys.executable, "--renderer", sys.executable,
                "--output", str(self.output)]
        with mock.patch.object(sys, "argv", argv), \
                mock.patch.object(compatibility, "snapshot", return_value={}), \
                mock.patch.object(compatibility.subprocess, "check_output", side_effect=
                                  subprocess.CalledProcessError(128, ["git", "rev-parse"])), \
                redirect_stderr(io.StringIO()):
            self.assertEqual(compatibility.main(), 3)
        self.assertFalse(self.output.exists())


if __name__ == "__main__":
    unittest.main()
