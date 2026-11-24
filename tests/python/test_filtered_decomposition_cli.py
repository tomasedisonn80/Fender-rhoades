"""Bounded CLI/provenance checks, not a repeat of the frozen waveform study.

Public CLI, failure retention and launcher checks use the standard library.
Optional NumPy/SciPy checks use constructed arrays and mocked numerical work;
the independent bridge oracles live in test_filtered_error_bridge.py.
"""
from __future__ import annotations

from contextlib import redirect_stderr, redirect_stdout
from copy import deepcopy
from dataclasses import replace
import hashlib
import importlib.util
import io
import json
import os
from pathlib import Path
import signal
import subprocess
import sys
import tempfile
from types import SimpleNamespace
import unittest
from unittest import mock


ROOT = Path(__file__).resolve().parents[2]
TOOL = ROOT / "tools/decompose_filtered_audio.py"
RETAINED = ROOT / "docs/validation/filtered-decomposition-initial-20261124b.json"
CASE_SHA256 = "579b3844d0fd32ef8fa9706f189b5d6255c0c93b2218e64d235ed4b233cbc4bc"
with mock.patch.object(sys, "path", [str(ROOT / "tools"), *sys.path]):
    import decompose_filtered_audio as cli
    import filtered_error_bridge as bridge
reference = cli.reference
HAVE_SCIENCE = (importlib.util.find_spec("numpy") is not None
                and importlib.util.find_spec("scipy") is not None)


def digest(raw):
    return hashlib.sha256(raw).hexdigest()


def object_digest(value):
    return digest(json.dumps(value, sort_keys=True, separators=(",", ":"),
                             ensure_ascii=True, allow_nan=False).encode("ascii"))


def invoke(arguments):
    stdout, stderr = io.StringIO(), io.StringIO()
    stdout.buffer, stderr.buffer = io.BytesIO(), io.BytesIO()
    with redirect_stdout(stdout), redirect_stderr(stderr):
        code = cli.main(arguments)
    return (code, stdout.getvalue() + stdout.buffer.getvalue().decode(),
            stderr.getvalue() + stderr.buffer.getvalue().decode())


class DecompositionPublicCliTests(unittest.TestCase):
    def command(self, *arguments):
        return subprocess.run([sys.executable, "-S", "-B", str(TOOL), *arguments],
                              cwd=ROOT, capture_output=True, text=True,
                              check=False, timeout=10)

    def test_help_and_plan_without_site_packages_preserve_frozen_protocol(self):
        help_result = self.command("--help")
        self.assertEqual(help_result.returncode, 0, help_result.stderr)
        self.assertIn("--timeout-seconds", help_result.stdout)
        self.assertNotIn("--_worker", help_result.stdout)
        result = self.command("--plan")
        self.assertEqual(result.returncode, 0, result.stderr)
        plan = json.loads(result.stdout)
        self.assertEqual(plan.pop("plan_sha256"), object_digest(plan))
        self.assertEqual(plan["case_sha256"], CASE_SHA256)
        self.assertEqual(object_digest(plan["case"]), CASE_SHA256)
        self.assertEqual(plan["case"]["case_id"], "r08-chord-all73-u10")
        self.assertEqual(plan["reset_epochs"], [[0, 1023], [1023, 4096]])
        self.assertEqual(plan["identity"], "P-C=(P-S)+(S-N)+(N-T)+(T-C)")
        self.assertEqual(plan["reference_orders"], [16, 32, 64])
        self.assertEqual(plan["bookkeeping_limit_volts"], 1e-12)
        self.assertEqual(plan["gates"], {
            "comparison_a_peak_volts": 1e-12,
            "reference_refinement_peak_volts": 1e-9,
            "reference_refinement_rms_volts": 1e-10,
            "comparison_b_exploratory_peak_volts": 1e-4,
            "comparison_b_exploratory_rms_volts": 1e-5,
        })

    def test_missing_duplicate_or_changed_frozen_case_is_rejected(self):
        case = cli.frozen_case()
        for cases in ((), (case, case), (replace(case, frames=case.frames - 1),)):
            with self.subTest(count=len(cases)), mock.patch.object(
                    reference, "build_cases", return_value=cases):
                with self.assertRaisesRegex(ValueError, "frozen diagnostic case changed"):
                    cli.make_plan()

    def test_invalid_arguments_create_no_output(self):
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory) / "result"
            base = ("--probe", str(TOOL), "--output-dir", str(output))
            for arguments in ((), ("--probe", str(TOOL)),
                              *(base + ("--timeout-seconds", value)
                                for value in ("0", "1801", "nan"))):
                with self.subTest(arguments=arguments):
                    result = self.command(*arguments)
                    self.assertEqual(result.returncode, 2, result.stderr)
                    self.assertFalse(output.exists())

    def test_existing_directory_and_file_are_refused_without_overwrite(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            folder, regular = root / "folder", root / "regular"
            folder.mkdir()
            marker = folder / "summary.json"
            marker.write_bytes(b"retained summary\n")
            regular.write_bytes(b"retained file\n")
            for output in (folder, regular):
                with self.subTest(output=output.name):
                    result = self.command("--probe", str(TOOL), "--output-dir", str(output))
                    self.assertEqual(result.returncode, 3, result.stderr)
                    self.assertIn("Refusing existing output path", result.stderr)
            self.assertEqual(marker.read_bytes(), b"retained summary\n")
            self.assertEqual(regular.read_bytes(), b"retained file\n")
            self.assertEqual(list(folder.iterdir()), [marker])
            with self.assertRaises(FileExistsError):
                cli._write(marker, {"replacement": True})
            self.assertEqual(marker.read_bytes(), b"retained summary\n")

    def test_existing_and_dangling_output_symlinks_are_refused(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            target = root / "target"
            target.mkdir()
            for name, destination in (("live", target), ("dangling", root / "absent")):
                link = root / name
                try:
                    link.symlink_to(destination, target_is_directory=True)
                except (OSError, NotImplementedError) as error:
                    self.skipTest(f"Symlinks unavailable: {error}")
                result = self.command("--probe", str(TOOL), "--output-dir", str(link))
                self.assertEqual(result.returncode, 3, result.stderr)
                self.assertIn("Refusing existing output path", result.stderr)
                self.assertTrue(link.is_symlink())
            self.assertEqual(list(target.iterdir()), [])
            self.assertFalse((root / "absent").exists())


class DiagnosticFixture(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory(prefix="filtered-decomposition-")
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name)
        self.output = self.root / "result"
        self.probe = self.root / "probe"
        self.source = self.root / "source.py"
        self.probe.write_bytes(b"constructed probe identity\n")
        self.source.write_bytes(b"constructed source identity\n")
        self.enterContext(mock.patch.object(cli, "ROOT", self.root))
        self.enterContext(mock.patch.object(cli, "SOURCE_PATHS", (self.source.name,)))
        self.enterContext(mock.patch.object(cli, "_git_identity", return_value=None))

    def invoke(self, *extra):
        return invoke(["--probe", str(self.probe), "--output-dir", str(self.output), *extra])

    def read(self, name):
        return json.loads((self.output / name).read_bytes())

    def check_incomplete(self, result):
        code, stdout, stderr = result
        self.assertEqual(code, 3, stdout + stderr)
        summary = self.read("summary.json")
        self.assertFalse(summary["diagnostic_complete"])
        self.assertEqual(summary["exit_code"], 3)
        self.assertEqual(json.loads(stdout)["diagnostic_complete"], False)
        self.assertEqual(summary["manifest_sha256"], digest((self.output / "manifest.json").read_bytes()))
        return summary


class DecompositionFailureRetentionTests(DiagnosticFixture):
    def setUp(self):
        super().setUp()
        stub = SimpleNamespace(__version__="stdlib fault fixture")
        self.enterContext(mock.patch.object(reference, "numerical_libraries", return_value=(stub, stub)))
        self.curves = self.enterContext(mock.patch.object(
            reference.ReferenceBank, "curves", side_effect=AssertionError("unexpected numerical work")))

    def check_probe_bytes(self, stdout, stderr):
        prefix = self.output / "probe" / cli.CASE_ID
        self.assertEqual(prefix.with_suffix(".stdout.json").read_bytes(), stdout)
        self.assertEqual(prefix.with_suffix(".stderr.txt").read_bytes(), stderr)
        self.assertEqual(prefix.with_suffix(".stdin.txt").read_bytes(),
                         reference.probe_input(cli.frozen_case()))
        self.curves.assert_not_called()
        self.assertFalse((self.output / "case.json").exists())
        self.assertEqual(list((self.output / "raw").iterdir()), [])

    def test_malformed_probe_retains_exact_bytes_error_and_manifest(self):
        for number, (raw, error) in enumerate(((b"{invalid\n", "JSONDecodeError"),
                                              (b'{"schema_version":true}', "Unexpected probe schema_version"),
                                              (b'{"value":NaN}', "Nonfinite JSON token"))):
            with self.subTest(raw=raw):
                self.output = self.root / f"malformed-{number}"
                with mock.patch.object(reference, "run_probe", return_value=(raw, b"probe warning\n")):
                    summary = self.check_incomplete(self.invoke("--_worker"))
                self.assertIn(error, summary["error"])
                self.check_probe_bytes(raw, b"probe warning\n")
                manifest = self.read("manifest.json")
                self.assertEqual(manifest["source_sha256_before"], {self.source.name: digest(self.source.read_bytes())})
                self.assertEqual(manifest["probe_sha256_before"], digest(self.probe.read_bytes()))
                retained = {path: path.read_bytes() for path in self.output.rglob("*") if path.is_file()}
                self.assertEqual(self.invoke()[0], 3)
                self.assertEqual({path: path.read_bytes() for path in retained}, retained)

    def test_probe_nonzero_exit_and_timeout_retain_partial_stdout_and_stderr(self):
        stdout, stderr = b"partial probe result\x00", b"probe error\xff\n"
        outcomes = (subprocess.CompletedProcess([], 7, stdout, stderr),
                    subprocess.TimeoutExpired(["probe"], 120, output=stdout, stderr=stderr))
        for number, outcome in enumerate(outcomes):
            with self.subTest(outcome=type(outcome).__name__):
                self.output = self.root / f"probe-error-{number}"
                options = {"side_effect": outcome} if isinstance(outcome, Exception) else {"return_value": outcome}
                with mock.patch.object(reference.subprocess, "run", **options) as run:
                    summary = self.check_incomplete(self.invoke("--_worker"))
                self.assertIn("ProbeFailure", summary["error"])
                self.assertIn("failed (7)" if number == 0 else "120-second", summary["error"])
                self.assertEqual(run.call_args.kwargs["timeout"], 120)
                self.check_probe_bytes(stdout, stderr)


class DecompositionLauncherTests(DiagnosticFixture):
    def test_timeout_requests_group_kill_reaps_worker_and_retains_partial_output(self):
        process = mock.Mock(pid=12345)
        process.communicate.side_effect = [subprocess.TimeoutExpired(["worker"], 1), (b"partial", b"error")]

        def start(*args, **kwargs):
            self.output.mkdir()
            (self.output / "partial.bin").write_bytes(b"keep partial bytes")
            return process

        with mock.patch.object(cli.subprocess, "Popen", side_effect=start) as popen, \
                mock.patch.object(cli.os, "killpg", create=True) as kill_group:
            code, stdout, stderr = self.invoke("--timeout-seconds", "1")
        self.assertEqual(code, 3)
        self.assertEqual(stdout, "")
        self.assertIn("declared deadline", stderr)
        self.assertEqual(process.communicate.call_args_list, [mock.call(timeout=1), mock.call()])
        self.assertEqual(popen.call_args.kwargs["start_new_session"], os.name == "posix")
        if os.name == "posix":
            kill_group.assert_called_once_with(process.pid, signal.SIGKILL)
            process.kill.assert_not_called()
        else:
            process.kill.assert_called_once_with()
            kill_group.assert_not_called()
        record = self.read("timeout.json")
        self.assertEqual((record["status"], record["timeout_seconds"]), ("incomplete", 1))
        self.assertEqual(record["command"], popen.call_args.args[0])
        self.assertIn("--_worker", record["command"])
        self.assertEqual((self.output / "partial.bin").read_bytes(), b"keep partial bytes")

    def test_worker_exit_codes_and_both_streams_are_preserved(self):
        for worker_code, expected in ((0, 0), (1, 1), (2, 2), (3, 3), (7, 3), (-9, 3)):
            with self.subTest(worker_code=worker_code):
                process = mock.Mock(returncode=worker_code)
                process.communicate.return_value = (b"worker stdout\n", b"worker stderr\n")
                with mock.patch.object(cli.subprocess, "Popen", return_value=process):
                    self.assertEqual(self.invoke(), (expected, "worker stdout\n", "worker stderr\n"))
                process.communicate.assert_called_once_with(timeout=600)


@unittest.skipUnless(HAVE_SCIENCE, "Optional retained-array checks need NumPy/SciPy")
class DecompositionProvenanceTests(DiagnosticFixture):
    def setUp(self):
        super().setUp()
        import numpy as np
        self.np = np
        self.zeros = np.zeros(cli.frozen_case().frames)
        self.arrays = {name: self.zeros.copy() for name in (
            "decomposition_residual_volts", "endpoint_event_direct_residual_volts",
            "endpoint_event_attribution_residual_volts", "event_contribution_residual_volts",
            "event_jump_attribution_residual_internal_volts")}
        retained = json.loads(RETAINED.read_bytes())
        self.row = {name: deepcopy(retained[name]) for name in
                    ("comparison_a", "comparison_b", "reference_refinement")}
        self.row["raw_arrays_path"] = "raw/constructed.npz"

    def run_fixture(self, mutation=None):
        def execute(probe, output, case, bank, source_sha256):
            self.assertEqual(source_sha256, object_digest(self.read("manifest.json")["source_sha256_before"]))
            with (output / self.row["raw_arrays_path"]).open("xb") as stream:
                self.np.savez_compressed(stream, **{name: self.zeros for name in
                    ("production_volts", "same_rate_volts", "continuous_q64_volts", "source_internal_volts")})
            if mutation is not None:
                mutation()
            return self.row

        with mock.patch.object(reference, "execute_case", side_effect=execute), \
                mock.patch.object(reference, "quadrature", return_value=SimpleNamespace(normalizer=1.0)), \
                mock.patch.object(bridge, "one_sided_source", return_value=(self.zeros, self.zeros.copy(), [])), \
                mock.patch.object(bridge, "decompose_arrays", return_value=(self.arrays, {"fixture": "constructed"})):
            return self.invoke("--_worker")

    def test_failed_comparison_b_survives_perfect_bookkeeping_and_hashes_bind_retention(self):
        code, stdout, stderr = self.run_fixture()
        self.assertEqual(code, 1, stdout + stderr)
        summary = self.read("summary.json")
        self.assertTrue(summary["diagnostic_complete"])
        self.assertEqual(summary["decomposition_closure_error_volts"], 0.0)
        self.assertEqual(summary["comparison_b"]["status"], "fail")
        for name in ("comparison_a", "comparison_b", "reference_refinement"):
            self.assertEqual(summary[name], self.row[name])
            self.assertEqual(self.read("case.json")[name], self.row[name])
        for name, path in (("manifest_sha256", "manifest.json"), ("original_case_sha256", "case.json"),
                           ("events_sha256", "events.json"), ("raw_npz_sha256", "raw/decomposition.npz")):
            self.assertEqual(summary[name], digest((self.output / path).read_bytes()))
        with self.np.load(self.output / "raw/decomposition.npz", allow_pickle=False) as stored:
            self.assertEqual(set(stored.files), set(summary["array_sha256"]))
            for name in stored.files:
                self.assertEqual(summary["array_shapes"][name], list(stored[name].shape))
                self.assertEqual(summary["array_sha256"][name],
                                 digest(self.np.asarray(stored[name], dtype="<f8").tobytes(order="C")))

    def test_source_or_probe_mutation_prevents_diagnostic_completion(self):
        for number, changed in enumerate((self.source, self.probe)):
            with self.subTest(changed=changed.name):
                self.output = self.root / f"mutated-{number}"
                summary = self.check_incomplete(self.run_fixture(
                    lambda: changed.write_bytes(changed.read_bytes() + b"changed\n")))
                self.assertIn("Source or probe changed", summary["error"])
                manifest = self.read("manifest.json")
                before, after = ((manifest["source_sha256_before"], summary["source_sha256_after"])
                                 if changed == self.source else
                                 (manifest["probe_sha256_before"], summary["probe_sha256_after"]))
                self.assertNotEqual(before, after)
                self.assertEqual(summary["comparison_b"], self.row["comparison_b"])
                self.assertTrue((self.output / "raw/decomposition.npz").is_file())

    def test_unresolved_reference_and_resolved_pass_keep_distinct_exit_codes(self):
        for number, (resolved, expected) in enumerate(((False, 2), (True, 0))):
            with self.subTest(resolved=resolved):
                self.output = self.root / f"reference-{number}"
                self.row["comparison_b"]["status"] = "pass" if resolved else "inconclusive: reference not resolved"
                self.row["reference_refinement"]["resolved"] = resolved
                self.assertEqual(self.run_fixture()[0], expected)
                self.assertEqual(self.read("summary.json")["exit_code"], expected)
                self.assertTrue(self.read("summary.json")["diagnostic_complete"])


if __name__ == "__main__":
    unittest.main()
