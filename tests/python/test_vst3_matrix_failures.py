"""Bounded launcher failure/retention fixtures; no native benchmark is launched."""
from __future__ import annotations

import copy
from contextlib import ExitStack, contextmanager, redirect_stderr, redirect_stdout
import importlib.util
import io
import json
from pathlib import Path
import subprocess
import tarfile
import tempfile
from types import SimpleNamespace
import unittest
from unittest import mock


ROOT = Path(__file__).resolve().parents[2]
SPEC = importlib.util.spec_from_file_location("matrix_failures", ROOT / "tools/run_vst3_matrix.py")
matrix = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(matrix)


class ControlledProcess:
    def __init__(self, returncode=0, timeout_then_zero=False, after_wait=None):
        self.returncode = returncode
        self.timeout_then_zero = timeout_then_zero
        self.after_wait = after_wait
        self.waits = 0
        self.killed = False

    def wait(self, timeout=None):
        self.waits += 1
        if self.timeout_then_zero and self.waits == 1:
            raise subprocess.TimeoutExpired("controlled-child", timeout)
        if self.after_wait is not None:
            self.after_wait()
            self.after_wait = None
        return self.returncode

    def kill(self):
        self.killed = True


class LauncherFailureTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.original_plan, _ = matrix.load_plan()
        with tarfile.open(ROOT / "docs/validation/vst3-matrix-20261124.tar.gz") as archive:
            cls.valid_stdout = archive.extractfile("r1-p01-direct.stdout.json").read()

    @contextmanager
    def fixture(self, *, mode="normal", after_snapshot_error=False):
        with tempfile.TemporaryDirectory() as temporary, ExitStack() as stack:
            root = Path(temporary) / "source"
            build = root / "build/vst3"
            build.mkdir(parents=True)
            (root / "docs").mkdir()
            (root / "docs/BENCHMARKING.md").write_text("controlled fixture", encoding="ascii")
            for name in ("CMakeCache.txt", "compile_commands.json", "build.ninja"):
                (build / name).write_text("controlled fixture", encoding="ascii")
            compiled = root / "input.hpp"
            compiled.write_text("original input", encoding="ascii")
            plan = copy.deepcopy(self.original_plan)
            plan["profiles"] = plan["profiles"][:1]
            plan["variants"] = plan["variants"][:1]
            plan["runs"] = [run for run in plan["runs"]
                            if run["profile_id"] == "p01" and run["variant"] == "direct"]
            for run in plan["runs"]:
                (root / run["argv"][0]).write_bytes(b"non-executed fixture binary")
            output = Path(temporary) / "evidence"
            state = SimpleNamespace(root=root, output=output, compiled=compiled,
                                    plan=plan, calls=[], processes=[])

            def popen(argv, **kwargs):
                index = len(state.calls)
                state.calls.append(argv)
                affinity = Path(argv[4])
                if mode == "bad_affinity" and index == 0:
                    affinity.write_bytes(b'{"affinity":')
                elif mode == "nonobject_affinity" and index == 0:
                    affinity.write_bytes(b"[]")
                elif mode != "rejected_affinity":
                    affinity.write_text(json.dumps({"affinity": [0]}), encoding="ascii")
                kwargs["stdout"].write(b"" if mode in {"empty_output", "rejected_affinity"}
                                       else self.valid_stdout)
                kwargs["stderr"].write(b"controlled stderr\x00\xff\n")
                after_wait = None
                if index == 0 and mode == "source_drift":
                    after_wait = lambda: compiled.write_text("changed input", encoding="ascii")
                elif index == 0 and mode == "source_removed":
                    after_wait = compiled.unlink
                process = ControlledProcess(
                    returncode=23 if mode in {"nonzero", "rejected_affinity"} else 0,
                    timeout_then_zero=mode == "timeout_race" and index == 0,
                    after_wait=after_wait,
                )
                state.processes.append(process)
                return process

            stack.enter_context(mock.patch.object(matrix, "ROOT", root))
            stack.enter_context(mock.patch.object(matrix.sys, "platform", "linux"))
            stack.enter_context(mock.patch.object(matrix.os, "sched_setaffinity", create=True))
            stack.enter_context(mock.patch.object(matrix.os, "sched_getaffinity", return_value={0}, create=True))
            stack.enter_context(mock.patch.object(matrix, "load_plan", return_value=(plan, json.dumps(plan).encode())))
            stack.enter_context(mock.patch.object(matrix, "source_paths", return_value=[compiled]))
            stack.enter_context(mock.patch.object(matrix, "build_preflight", return_value={"fixture": True}))
            snapshots = [{"fixture": "before"}, OSError("controlled after-snapshot failure")]
            stack.enter_context(mock.patch.object(matrix, "machine_snapshot",
                               side_effect=snapshots if after_snapshot_error else None,
                               return_value={"fixture": "no host measurement"}))
            stack.enter_context(mock.patch.object(matrix, "command", return_value={
                "returncode": 0, "stdout": "controlled metadata", "stderr": ""}))
            stack.enter_context(mock.patch.object(matrix.subprocess, "Popen", side_effect=popen))
            yield state

    def run_fixture(self, state):
        with redirect_stdout(io.StringIO()), redirect_stderr(io.StringIO()):
            code = matrix.main(["--output", str(state.output), "--sdk", str(state.root),
                                "--competing-activity", "bounded two-run unit fixture"])
        manifest = json.loads((state.output / "manifest.json").read_bytes())
        return code, manifest

    def test_timeout_race_cannot_be_marked_complete_after_zero_exit(self):
        with self.fixture(mode="timeout_race") as state:
            code, manifest = self.run_fixture(state)
            self.assertEqual(code, 1)
            self.assertEqual(manifest["status"], "integrity_failure")
            self.assertEqual(manifest["attempted_runs"], 2)
            self.assertEqual(manifest["unattempted_run_ids"], [])
            first, second = manifest["results"]
            self.assertTrue(first["timed_out"])
            self.assertEqual(first["returncode"], 0)
            self.assertEqual(first["status"], "incomplete")
            self.assertEqual(second["status"], "complete")
            self.assertTrue(state.processes[0].killed)

    def test_malformed_affinity_retains_the_executed_run(self):
        for mode in ("bad_affinity", "nonobject_affinity"):
            with self.subTest(mode=mode), self.fixture(mode=mode) as state:
                code, manifest = self.run_fixture(state)
                self.assertEqual(code, 3)
                self.assertEqual(len(state.calls), 1)
                self.assertEqual(manifest["attempted_runs"], 1)
                self.assertEqual(manifest["unattempted_run_ids"], ["r2-p01-direct"])
                first = manifest["results"][0]
                self.assertEqual(first["status"], "incomplete")
                self.assertIn("affinity_error", first)
                self.assertEqual((state.output / first["stdout"]).read_bytes(), self.valid_stdout)
                self.assertEqual(json.loads((state.output / state.plan["runs"][0]["record_file"]).read_bytes()), first)

    def test_after_snapshot_failure_preserves_manifest_and_original_error(self):
        with self.fixture(mode="source_drift", after_snapshot_error=True) as state:
            code, manifest = self.run_fixture(state)
            self.assertEqual(code, 3)
            self.assertEqual(manifest["status"], "operational_failure")
            self.assertIn("compiled input", manifest["operational_error"])
            self.assertIn("controlled after-snapshot failure", manifest["final_machine_error"])
            self.assertFalse(manifest["input_hashes_unchanged"])
            self.assertEqual(manifest["attempted_runs"], 1)

    def test_after_snapshot_failure_revokes_otherwise_complete_status(self):
        with self.fixture(after_snapshot_error=True) as state:
            code, manifest = self.run_fixture(state)
            self.assertEqual(code, 3)
            self.assertEqual(manifest["status"], "operational_failure")
            self.assertEqual(manifest["attempted_runs"], 2)
            self.assertTrue(all(item["status"] == "complete" for item in manifest["results"]))
            self.assertTrue(manifest["input_hashes_unchanged"])
            self.assertIn("controlled after-snapshot failure", manifest["final_machine_error"])

    def test_missing_and_nonzero_outputs_retain_raw_bytes_without_completion(self):
        for mode in ("empty_output", "nonzero"):
            with self.subTest(mode=mode), self.fixture(mode=mode) as state:
                code, manifest = self.run_fixture(state)
                self.assertEqual(code, 1)
                self.assertEqual(manifest["status"], "integrity_failure")
                self.assertEqual(manifest["attempted_runs"], 2)
                self.assertTrue(all(item["status"] == "incomplete" for item in manifest["results"]))
                for item in manifest["results"]:
                    raw = (state.output / item["stdout"]).read_bytes()
                    self.assertEqual(raw, b"" if mode == "empty_output" else self.valid_stdout)
                    stderr = (state.output / item["stderr"]).read_bytes()
                    self.assertEqual(stderr, b"controlled stderr\x00\xff\n")
                    self.assertEqual(matrix.digest(stderr), item["stderr_sha256"])

    def test_rejected_affinity_stops_later_runs_and_keeps_failure_stream(self):
        with self.fixture(mode="rejected_affinity") as state:
            code, manifest = self.run_fixture(state)
            self.assertEqual(code, 3)
            self.assertEqual(len(state.calls), 1)
            self.assertEqual(manifest["attempted_runs"], 1)
            self.assertEqual(manifest["unattempted_run_ids"], ["r2-p01-direct"])
            first = manifest["results"][0]
            self.assertEqual(first["returncode"], 23)
            self.assertEqual((state.output / first["stderr"]).read_bytes(), b"controlled stderr\x00\xff\n")

    def test_input_removal_is_an_operational_failure_with_final_hash_error(self):
        with self.fixture(mode="source_removed") as state:
            code, manifest = self.run_fixture(state)
            self.assertEqual(code, 3)
            self.assertEqual(manifest["attempted_runs"], 1)
            self.assertIn("final_hash_error", manifest)
            self.assertEqual(manifest["unattempted_run_ids"], ["r2-p01-direct"])

    def test_nonlinux_protocol_rejection_keeps_exclusive_manifest(self):
        with self.fixture() as state, mock.patch.object(matrix.sys, "platform", "win32"):
            code, manifest = self.run_fixture(state)
            self.assertEqual(code, 3)
            self.assertEqual(state.calls, [])
            self.assertEqual(manifest["attempted_runs"], 0)
            self.assertIn("requires Linux", manifest["operational_error"])


class BuildFreshnessFailureTests(unittest.TestCase):
    def test_stale_failed_and_unavailable_ninja_checks_do_not_pass_preflight(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            build = root / "build/vst3"
            build.mkdir(parents=True)
            sdk = root / "sdk"
            cache = (f"RHODES_VST3_SDK_ROOT:PATH={sdk}\nCMAKE_BUILD_TYPE:STRING=Release\n"
                     "CMAKE_GENERATOR:INTERNAL=Ninja\nCMAKE_CXX_COMPILER:FILEPATH=fixture-cxx\n")
            (build / "CMakeCache.txt").write_text(cache, encoding="utf-8")
            for result in ({"returncode": 0, "stdout": "[1/1] rebuilding"},
                           {"returncode": 1, "stdout": "ninja: no work to do."},
                           {"unavailable": "controlled launch failure"}):
                with self.subTest(result=result), mock.patch.object(matrix, "ROOT", root), \
                        mock.patch.object(matrix, "command", return_value=result) as command:
                    with self.assertRaisesRegex(RuntimeError, "stale or cannot be checked"):
                        matrix.build_preflight(sdk, [build / "fixture"])
                    self.assertEqual(command.call_count, 1)


if __name__ == "__main__":
    unittest.main()
