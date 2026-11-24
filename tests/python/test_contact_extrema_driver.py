"""Stdlib driver policy tests; synthetic records are not numerical evidence.

No test integrates a trajectory, launches the native producer, or constructs the
26-run numerical experiment. Worker processes and dense evaluation are mocked;
real temporary files exercise bounded reads, hashes, and retained failure data.
"""
from __future__ import annotations

from contextlib import ExitStack, redirect_stderr
import copy
import hashlib
import io
import json
import math
import os
from pathlib import Path
import signal
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import Mock, call, patch


ROOT = Path(__file__).resolve().parents[2]
TOOLS = ROOT / "tools"
with patch.object(sys, "path", [str(TOOLS), *sys.path]):
    import contact_extrema_reference as reference
    import validate_contact_extrema as driver


COVERAGE = "uncertified_independent_search"
FIXTURES = ("supported_impact", "supported_impact_scaled")
LEVELS = ("R0", "R1", "R2")
PEAKS = ("hammer_compression", "hammer_force", "damper_compression", "damper_force")
FIXTURE_HASHES = {
    "supported_impact": "7a8778c66a118885c001d3d7c11c343dc1b93e555bc3cb17a485d084af7033b8",
    "supported_impact_scaled": "f4c3599e63f34854d6d67a04dcd0ed76554fd128a9fe6f52e6026b4e9bb483fe",
}
BASE_ATOL = [1e-16, 1e-16, 1e-13, 1e-13, 1e-16, 1e-13,
             1e-16, 1e-16, 1e-16, 1e-16, 1e-16, 1e-16, 1e-16]
SETTINGS = {
    name: {"maximum_step_s": step, "relative_tolerance": relative,
           "absolute_vector_multiplier": multiplier,
           "absolute_tolerance_vector": [value * multiplier for value in BASE_ATOL]}
    for name, step, relative, multiplier in (
        ("R0", 5e-6, 1e-10, 10.0), ("R1", 2.5e-6, 1e-12, 1.0),
        ("R2", 1.25e-6, 1e-13, 0.1))
}


def encode(value):
    return json.dumps(value, separators=(",", ":"), allow_nan=False).encode("utf-8")


def digest(raw):
    return hashlib.sha256(raw).hexdigest()


def values():
    return {"hammer_compression_m": 0.001, "hammer_force_n": 20.0,
            "damper_compression_m": 0.0, "damper_force_n": 0.0}


def candidate_document(fixture="supported_impact", *, peaks=True):
    """Two schema observations with an exact initial state, not an ODE solution."""
    initial = {
        "time_s": 0.0, "q_m": [0.0, 0.0], "v_m_per_s": [0.0, 0.0],
        "hammer_position_m": -0.0002, "hammer_velocity_m_per_s": 1.0,
        "hammer_force_n": 0.0, "damper_force_n": 0.0,
        "hammer_gap_m": -0.0002, "damper_gap_m": 0.0,
        "hammer_relative_velocity_m_per_s": 1.0,
        "damper_relative_velocity_m_per_s": 0.0,
        "hammer_impulse_ns": 0.0, "damper_impulse_ns": 0.0,
        "modal_loss_j": 0.0, "hammer_loss_j": 0.0, "damper_loss_j": 0.0,
        "hammer_clipped_loss_j": 0.0, "damper_clipped_loss_j": 0.0,
        "actuator_work_j": 0.0, "launch_work_j": 0.0, "energy_j": 0.004,
        "modal_acceleration_m_per_s2": [0.0, 0.0],
        "hammer_acceleration_m_per_s2": 0.0,
        "instantaneous_power_residual_w": 0.0, "actuator_power_w": 0.0,
        "pickup_voltage_v": 0.0,
    }
    sampled = {"scope": "accepted endpoints and checked interior samples; not continuous-time extrema"}
    located = {
        "coverage": "uncertified_sign_change_search",
        "evaluation_method": "fixed_interval_anchor_replay_two_rk4_half_steps_or_certified_free_motion",
    }
    for name in PEAKS:
        unit = "m" if name.endswith("compression") else "n"
        disabled = name.startswith("damper_")
        time_s = 0.0 if disabled else 0.004
        sampled.update({name + "_" + unit: 0.0, name + "_time_s": 0.0})
        located[name] = {"value_" + unit: values()[name + "_" + unit],
                         "time_s": time_s, "bracket_lo_s": time_s,
                         "bracket_hi_s": time_s,
                         "origin": "disabled" if disabled else "sample"}
    diagnostics = {
        "max_abs_cumulative_residual_j": 0.0, "sum_abs_interval_residual_j": 0.0,
        "maximum_power_residual_w": 0.0, "initial_energy_j": 0.004,
        "minimum_accepted_step_s": 20e-6, "maximum_accepted_step_s": 20e-6,
        "accepted_steps": 1000, "rejected_steps": 0, "rhs_evaluations": 12000,
        "analytic_free_evaluations": 0, "event_subdivisions": 0,
        "root_iterations": 0, "grazing_candidates": 0, "sampled_maxima": sampled,
    }
    if peaks:
        diagnostics["located_peak_candidates"] = located
    run = {
        "fixture": fixture, "success": True, "failure_code": 0,
        "maximum_step_s": 20e-6, "event_tolerance_s": 2.5e-9, "output_step_s": 20e-6,
        "relative_tolerance": 1e-8, "absolute_tolerance": 1e-12,
        "velocity_absolute_tolerance": 1e-9, "energy_absolute_tolerance": 1e-13,
        "impulse_absolute_tolerance": 1e-13,
        "samples": [initial, {**copy.deepcopy(initial), "time_s": 0.020}],
        "events": [], "relaunch_snapshots": [], "diagnostics": diagnostics,
    }
    return {"schema_version": 1, "model": "synthetic-contact-p1", "runs": [run]}


def reference_record(fixture="supported_impact", level="R2", *, raw_path=None):
    """Independent protocol-shaped test record, with no retained numerical arrays."""
    peaks = {}
    for name in PEAKS:
        unit = "m" if name.endswith("compression") else "n"
        disabled = name.startswith("damper_")
        peaks[name] = {
            "value_" + unit: values()[name + "_" + unit],
            "classification": "disabled" if disabled else "stationary",
            "kind": "disabled" if disabled else "isolated",
            "maximizer_intervals_s": [[0.0, 0.020]] if disabled else [[0.004, 0.004]],
        }
    result = {
        "fixture": fixture, "fixture_sha256": FIXTURE_HASHES[fixture], "level": level,
        "settings": copy.deepcopy(SETTINGS[level]), "coverage": COVERAGE,
        "source_sha256": {"synthetic-protocol-source": "a" * 64},
        "status": "completed", "complete_duration": True, "final_time_s": 0.020,
        "peaks": peaks, "unresolved": [], "counts": {"accepted_steps": 0},
        "branches": [{"branch": name} for name in ("separated", "positive_compression", "separated")],
        "resolved_peak_clusters": [{"time_s": 0.004, "crossing": "positive_to_negative"}],
        "search_statistics": {"maximum_physical_dense_derivative_discrepancy_m_per_s": 0.0},
    }
    if raw_path is not None:
        raw = b"synthetic protocol bytes, deliberately not a numerical NPZ"
        raw_path.write_bytes(raw)
        result["raw"] = {"path": str(raw_path), "sha256": digest(raw)}
    return result


class ContactExtremaDeclarationTests(unittest.TestCase):
    def test_help_and_plan_need_only_stdlib_and_leave_no_artifacts(self):
        script = """
import builtins, sys
sys.dont_write_bytecode = True
sys.path.insert(0, sys.argv[1])
original = builtins.__import__
def guarded(name, *args, **kwargs):
    if name.split('.')[0] in {'numpy', 'scipy'}:
        raise AssertionError('numerical backend imported: ' + name)
    return original(name, *args, **kwargs)
builtins.__import__ = guarded
import validate_contact_extrema
raise SystemExit(validate_contact_extrema.main([sys.argv[2]]))
"""
        for argument in ("--help", "--plan"):
            with self.subTest(argument=argument), tempfile.TemporaryDirectory() as directory:
                result = subprocess.run([sys.executable, "-S", "-c", script, str(TOOLS), argument],
                                        cwd=directory, capture_output=True, text=True, timeout=10)
                self.assertEqual(result.returncode, 0, result.stderr)
                self.assertEqual(result.stderr, "")
                self.assertEqual(list(Path(directory).iterdir()), [])
                if argument == "--help":
                    self.assertIn("--producer-executable", result.stdout)
                    continue
                declared = json.loads(result.stdout)
                self.assertEqual(declared["expected_candidate_count"], 26)
                self.assertEqual(len({row["slot"] for row in declared["slots"]}), 26)
                self.assertEqual(declared["settings_sha256"],
                                 "802f018edbad9396df0af0099cd1d0bf6b0d0a9621b0a5ae95688a283d31e17b")
                self.assertEqual(declared["reference_count"], 6)
                self.assertEqual({(row["fixture"], row["level"]) for row in declared["references"]},
                                 {(fixture, level) for fixture in FIXTURES for level in LEVELS})
                for row in declared["references"]:
                    self.assertEqual(row["settings"], SETTINGS[row["level"]])
                self.assertEqual(declared["reference_limits"], {
                    "accepted_steps": 200000, "rhs_evaluations": 5000000,
                    "search_evaluations": 1000000, "candidates": 100000,
                    "wall_seconds": 300.0, "parent_wall_seconds": 300.0})
                self.assertEqual(declared["input_limits"], {
                    "file_bytes": 32 * 1024 * 1024, "collection_bytes": 128 * 1024 * 1024, "files": 64})
                self.assertEqual(declared["coverage"], COVERAGE)
                self.assertEqual(declared["phy002_acceptance"], "incomplete")
                self.assertEqual(declared["PHY002_acceptance"], "incomplete")

    def test_plan_rejects_experiment_arguments_before_running(self):
        for option in ("--candidate", "--output-dir", "--producer-executable"):
            with self.subTest(option=option), patch.object(driver, "run_experiment") as run:
                with redirect_stderr(io.StringIO()), self.assertRaises(SystemExit) as caught:
                    driver.main(["--plan", option, "unused"])
                self.assertEqual(caught.exception.code, 2)
                run.assert_not_called()


class ContactExtremaBoundedReadTests(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory()
        self.addCleanup(temporary.cleanup)
        self.directory = Path(temporary.name).resolve()
        self.path = self.directory / "input.raw"
        self.path.write_bytes(b"abcdef")

    def test_exact_cap_prefix_and_zero_budget_never_read_a_sentinel_byte(self):
        regular_reader = driver._regular_reader
        for limit in (0, 3, 6, 9):
            requests = []
            def open_tracked(path):
                handle, metadata = regular_reader(path)
                original_read = handle.read
                def read(size):
                    requests.append(size)
                    return original_read(size)
                handle.read = read
                return handle, metadata
            with self.subTest(limit=limit), patch.object(driver, "_regular_reader", side_effect=open_tracked):
                raw, record = driver.bounded_read(self.path, limit)
            self.assertEqual(raw, b"abcdef"[:limit])
            self.assertEqual(record["read_bytes"], len(raw))
            self.assertEqual(record["retained_sha256"], digest(raw))
            self.assertEqual(record["complete"], limit >= 6)
            if limit <= 6:
                self.assertEqual(requests, [limit] if limit else [])
            self.assertTrue(all(request <= limit for request in requests))

    def test_bad_read_limits_fail_before_open(self):
        with patch.object(driver, "_regular_reader") as reader:
            for limit in (-1, True, 1.5, math.inf, None):
                with self.subTest(limit=limit), self.assertRaises(ValueError):
                    driver.bounded_read(self.path, limit)
            reader.assert_not_called()

    def test_symlink_leaf_and_ancestor_are_refused_before_open(self):
        leaf = self.directory / "leaf-link"
        ancestor = self.directory / "directory-link"
        try:
            leaf.symlink_to(self.path)
            ancestor.symlink_to(self.directory, target_is_directory=True)
        except (OSError, NotImplementedError) as exc:
            self.skipTest("symlinks unavailable: " + str(exc))
        with patch.object(driver.os, "open", side_effect=AssertionError("unsafe open")):
            for path in (leaf, ancestor / self.path.name):
                with self.subTest(path=path):
                    raw, record = driver.bounded_read(path, 6)
                    self.assertEqual(raw, b"")
                    self.assertFalse(record["complete"])
                    self.assertTrue(record["errors"])

    @unittest.skipUnless(hasattr(os, "mkfifo"), "FIFO creation requires a POSIX-like platform")
    def test_fifo_is_rejected_without_any_open_or_blocking_read(self):
        fifo = self.directory / "input.fifo"
        os.mkfifo(fifo)
        with patch.object(driver.os, "open", side_effect=AssertionError("FIFO was opened")):
            raw, record = driver.bounded_read(fifo, 6)
        self.assertEqual(raw, b"")
        self.assertFalse(record["complete"])
        self.assertIn("regular file", " ".join(record["errors"]))

    def test_source_change_during_read_invalidates_whole_file_identity(self):
        handle, metadata = driver._regular_reader(self.path)
        original_read = handle.read
        def changing_read(size):
            result = original_read(size)
            self.path.write_bytes(b"UVWXYZ")
            os.utime(self.path, ns=(metadata.st_atime_ns, metadata.st_mtime_ns + 1000000))
            return result
        handle.read = changing_read
        with patch.object(driver, "_regular_reader", return_value=(handle, metadata)):
            raw, record = driver.bounded_read(self.path, 6)
        self.assertEqual(raw, b"abcdef")
        self.assertFalse(record["complete"])
        self.assertIn("source changed during bounded read", record["errors"])

    def test_read_error_preserves_only_the_observed_prefix(self):
        handle, metadata = driver._regular_reader(self.path)
        handle.read = Mock(side_effect=[b"abc", OSError("injected read failure")])
        with patch.object(driver, "_regular_reader", return_value=(handle, metadata)):
            raw, record = driver.bounded_read(self.path, 6)
        self.assertEqual(raw, b"abc")
        self.assertFalse(record["complete"])
        self.assertEqual(record["retained_sha256"], digest(b"abc"))
        self.assertIn("injected read failure", " ".join(record["errors"]))


class DriverFilesCase(unittest.TestCase):
    def setUp(self):
        stack = ExitStack()
        self.addCleanup(stack.close)
        self.directory = Path(stack.enter_context(tempfile.TemporaryDirectory())).resolve()
        self.source = self.directory / "source.py"
        self.source.write_bytes(b"synthetic driver provenance source\n")
        worker = self.directory / "tools/contact_extrema_reference.py"
        worker.parent.mkdir()
        worker.write_bytes(b"# synthetic worker source; never executed\n")
        self.worker_path = worker
        stack.enter_context(patch.object(driver, "ROOT", self.directory))
        stack.enter_context(patch.object(driver, "SOURCE_PATHS", ("source.py",)))
        # Never inspect Git or shared mutable repository files in these tests.
        stack.enter_context(patch.object(driver, "_git_identity", return_value={"test": "not queried"}))
        stack.enter_context(patch.object(driver, "_versions", return_value={"test": "stdlib synthetic protocol"}))
        stack.enter_context(patch.object(reference, "_numpy", side_effect=AssertionError("numerical backend reached")))

    def candidate(self, name="candidate.json", document=None):
        path = self.directory / name
        path.write_bytes(encode(candidate_document() if document is None else document))
        return path

    def launcher(self, *, code=0, final=True, partial=False, mutate=None):
        def launch(command, **kwargs):
            arguments = dict(zip(command[2::2], command[3::2]))
            output, raw = Path(arguments["--output"]), Path(arguments["--raw"])
            kwargs["stdout"].write(b"retained synthetic stdout\n")
            kwargs["stderr"].write(b"retained synthetic stderr\n")
            if final:
                record = reference_record(arguments["--fixture"], arguments["--level"], raw_path=raw)
                if mutate:
                    mutate(record)
                output.write_bytes(encode(record))
            if partial:
                output.with_suffix(".partial.json").write_bytes(b'{"status":"incomplete"}')
                raw.with_suffix(".partial.npz").write_bytes(b"partial synthetic array bytes")
            process = Mock(returncode=code)
            process.poll.return_value = code
            return process
        return launch

    def experiment(self, paths, *, name="output", launcher=None, complete=False, producer=None):
        with ExitStack() as stack:
            popen = stack.enter_context(patch.object(driver.subprocess, "Popen",
                                                    side_effect=launcher or self.launcher()))
            stack.enter_context(patch.object(reference, "evaluate_reference", return_value=values()))
            if complete:
                # Inject completed upstream collection/scaling gates to exercise
                # final report policy with one input, never a 26-run experiment.
                stack.enter_context(patch.object(driver, "EXPECTED_COUNT", 1))
                stack.enter_context(patch.object(driver, "validate_collection", return_value={
                    "matrix_complete": True, "duplicate_slots": [], "failed_runs": [],
                    "unavailable_peaks": []}))
                stack.enter_context(patch.object(driver, "coordinate_pairs", return_value={"status": "pass"}))
            report = driver.run_experiment(paths, self.directory / name, producer_executable=producer)
        return report, popen


class ContactExtremaInputRetentionTests(DriverFilesCase):
    def test_existing_output_is_refused_without_modifying_its_contents(self):
        output = self.directory / "existing"
        output.mkdir()
        sentinel = output / "keep.raw"
        sentinel.write_bytes(b"original evidence")
        with patch.object(driver, "bounded_read") as reader, patch.object(driver, "run_reference_worker") as worker:
            with self.assertRaises(FileExistsError):
                driver.run_experiment([self.directory / "absent.json"], output)
        self.assertEqual(sentinel.read_bytes(), b"original evidence")
        self.assertEqual(list(output.iterdir()), [sentinel])
        reader.assert_not_called()
        worker.assert_not_called()

    def test_output_symlink_ancestor_cannot_receive_artifacts(self):
        link = self.directory / "output-link"
        try:
            link.symlink_to(self.directory, target_is_directory=True)
        except (OSError, NotImplementedError) as exc:
            self.skipTest("symlinks unavailable: " + str(exc))
        with self.assertRaisesRegex(OSError, "real directories"):
            driver.run_experiment([], link / "new")
        self.assertFalse((self.directory / "new").exists())

    def test_strict_invalid_inputs_are_retained_byte_for_byte(self):
        valid = encode(candidate_document())
        malformed_schema = candidate_document()
        malformed_schema["runs"][0]["success"] = 1
        malformed_peak = candidate_document()
        malformed_peak["runs"][0]["diagnostics"]["located_peak_candidates"]["hammer_force"]["value_n"] = -1
        raw_inputs = [b"\xff\xfe", b"{", b'{"schema_version":1,"schema_version":1}',
                      valid.replace(b'"energy_j":0.004', b'"energy_j":NaN', 1),
                      encode(malformed_schema), encode(malformed_peak)]
        paths = []
        for index, raw in enumerate(raw_inputs):
            path = self.directory / (str(index) + ".json")
            path.write_bytes(raw)
            paths.append(path)
        report, popen = self.experiment(paths)
        popen.assert_not_called()
        self.assertEqual((report["status"], report["input_status"], report["exit_code"]), ("fail", "fail", 1))
        self.assertEqual(len(report["references"]), 6)
        self.assertTrue(all(row["status"] == "not_run_no_comparable_candidates" for row in report["references"]))
        for row, raw in zip(report["inputs"], raw_inputs):
            self.assertEqual(row["status"], "invalid_input")
            self.assertTrue(row["errors"])
            self.assertTrue(row["raw_complete"])
            self.assertEqual(Path(row["raw_path"]).read_bytes(), raw)
            self.assertEqual(row["raw_sha256"], digest(raw))
        manifest = json.loads((self.directory / "output/manifest.json").read_bytes())
        for artifact in manifest["artifacts"]:
            self.assertEqual(artifact["status"], "recorded")
            self.assertEqual(artifact["sha256"], digest(Path(artifact["path"]).read_bytes()))

    def test_collection_budget_is_shared_and_prefix_hash_is_not_a_full_hash(self):
        paths = []
        for index in range(3):
            path = self.directory / (str(index) + ".raw")
            path.write_bytes(b"abcdefgh")
            paths.append(path)
        with patch.object(driver, "MAX_FILE_BYTES", 4), patch.object(driver, "MAX_COLLECTION_BYTES", 7):
            with patch.object(driver, "bounded_read", wraps=driver.bounded_read) as reader:
                report, _ = self.experiment(paths)
        self.assertEqual([item.args[1] for item in reader.call_args_list], [4, 3, 0])
        self.assertEqual(report["matrix"]["bounded_input_bytes"], 7)
        for row, expected in zip(report["inputs"], (b"abcd", b"abc", b"")):
            self.assertFalse(row["raw_complete"])
            self.assertIsNone(row["raw_sha256"])
            self.assertEqual(row["retained_sha256"], digest(expected))
            self.assertEqual(Path(row["raw_path"]).read_bytes(), expected)

    def test_sixty_fifth_input_is_recorded_but_never_read(self):
        path = self.directory / "invalid.raw"
        path.write_bytes(b"{")
        with patch.object(driver, "bounded_read", wraps=driver.bounded_read) as reader:
            report, popen = self.experiment([path] * 65)
        self.assertEqual(reader.call_count, 64)
        popen.assert_not_called()
        self.assertEqual(len(report["inputs"]), 65)
        self.assertEqual(report["matrix"]["bounded_input_bytes"], 64)
        last = report["inputs"][-1]
        self.assertEqual(last["index"], 65)
        self.assertIsNone(last["raw_path"])
        self.assertIn("64-file limit", " ".join(last["errors"]))

    def test_failed_producer_and_missing_peaks_remain_distinct(self):
        failed = candidate_document()
        failed["runs"][0].update(success=False, failure_code=9)
        failed["runs"][0]["samples"][-1]["time_s"] = 0.001
        for name in ("hammer_compression", "hammer_force"):
            failed["runs"][0]["diagnostics"]["located_peak_candidates"][name].update(
                time_s=0.0005, bracket_lo_s=0.0005, bracket_hi_s=0.0005)
        cases = [(failed, "producer_failure", "fail", 1),
                 (candidate_document(peaks=False), "missing_peak_diagnostics", "incomplete", 2)]
        for index, (document, validity, status, exit_code) in enumerate(cases):
            with self.subTest(validity=validity):
                path = self.candidate(str(index) + ".json", document)
                report, popen = self.experiment([path], name="output-" + str(index))
                popen.assert_not_called()
                self.assertEqual((report["status"], report["exit_code"]), (status, exit_code))
                self.assertEqual(report["comparisons"][0]["validity"], validity)
                self.assertEqual(Path(report["inputs"][0]["raw_path"]).read_bytes(), path.read_bytes())
                self.assertEqual(report["inputs"][0]["producer_success"], document["runs"][0]["success"])

    def test_partial_raw_write_is_retained_and_consumed_bytes_are_not_refunded(self):
        path = self.candidate()
        write_new = driver._write_new
        def short_write(destination, raw):
            if destination.name == "candidate-0001.raw":
                destination.write_bytes(raw[:9])
                raise OSError("injected short raw write")
            return write_new(destination, raw)
        with patch.object(driver, "_write_new", side_effect=short_write):
            report, popen = self.experiment([path])
        popen.assert_not_called()
        self.assertEqual(report["exit_code"], 3)
        self.assertEqual(report["status"], "fail")
        self.assertEqual(report["matrix"]["bounded_input_bytes"], len(path.read_bytes()))
        self.assertTrue(report["operational_errors"])
        row = report["inputs"][0]
        self.assertFalse(row["raw_complete"])
        self.assertEqual(row["retention_artifact"]["sha256"], digest(path.read_bytes()[:9]))
        self.assertEqual((self.directory / "output/raw/candidate-0001.raw").read_bytes(), path.read_bytes()[:9])


class ContactExtremaWorkerTests(DriverFilesCase):
    def test_one_candidate_schedules_six_distinct_bounded_commands_without_producer(self):
        candidate = self.candidate()
        producer = self.directory / "producer-must-not-run"
        producer.write_bytes(b"declared identity only")
        report, popen = self.experiment([candidate], producer=producer)
        self.assertEqual(popen.call_count, 6)
        commands = [item.args[0] for item in popen.call_args_list]
        self.assertEqual(len({tuple(command) for command in commands}), 6)
        self.assertEqual({(command[3], command[5]) for command in commands},
                         {(fixture, level) for fixture in FIXTURES for level in LEVELS})
        for item in popen.call_args_list:
            command, options = item.args[0], item.kwargs
            self.assertEqual(command[:2], [sys.executable, str(self.worker_path)])
            self.assertEqual(command[2::2], ["--fixture", "--level", "--output", "--raw"])
            self.assertNotIn(str(candidate), command)
            self.assertNotIn(str(producer), command)
            self.assertEqual(options["stdin"], subprocess.DEVNULL)
            self.assertEqual(options["cwd"], self.directory)
            self.assertEqual(options["env"]["PYTHONDONTWRITEBYTECODE"], "1")
            self.assertEqual(options["start_new_session"], os.name == "posix")
        self.assertTrue(all(row["parent_wall_limit_s"] == 300.0 for row in report["references"]))
        self.assertEqual(report["reference_execution_status"], "pass")
        self.assertEqual((report["input_status"], report["exit_code"]), ("incomplete", 2))
        for filename in ("provenance-before.json", "provenance-after.json"):
            identity = json.loads((self.directory / "output" / filename).read_bytes())["producer"]
            self.assertEqual(identity["sha256"], digest(producer.read_bytes()))
            self.assertFalse(identity["executed"])
            self.assertFalse(identity["candidate_production_attested"])

    def test_worker_deadline_cannot_be_disabled_or_raised(self):
        with patch.object(driver.subprocess, "Popen") as popen:
            for timeout in (0, -1, True, 301, math.inf, math.nan, "300"):
                with self.subTest(timeout=timeout), self.assertRaises(ValueError):
                    driver.run_reference_worker("supported_impact", "R0", self.directory / "unused",
                                                timeout_seconds=timeout)
            popen.assert_not_called()
        self.assertFalse((self.directory / "unused").exists())

    def test_parent_timeout_kills_worker_and_retains_output_and_checkpoints(self):
        process = Mock(returncode=None)
        process.poll.return_value = None
        process.wait.side_effect = subprocess.TimeoutExpired("mock worker", 1)
        def launch(*args, **kwargs):
            self.launcher(final=True, partial=True)(*args, **kwargs)
            return process
        def kill(target):
            target.returncode = -9
            target.poll.return_value = -9
        with patch.object(driver.subprocess, "Popen", side_effect=launch):
            with patch.object(driver.time, "monotonic", side_effect=[100, 100, 399.5, 400.001, 400.1]):
                with patch.object(driver, "_kill_worker", side_effect=kill) as killer:
                    record, retained = driver.run_reference_worker("supported_impact", "R0", self.directory / "timeout")
        killer.assert_called_once_with(process)
        self.assertEqual(process.wait.call_args_list, [call(timeout=1.0), call(timeout=0.5)])
        self.assertTrue(record["timed_out"])
        self.assertEqual(record["exit_code"], -9)
        self.assertEqual(record["status"], "inconclusive_reference")
        self.assertIsNotNone(retained)
        names = {Path(row["path"]).name for row in record["artifacts"]}
        self.assertTrue({"stdout.raw", "stderr.raw", "reference.json", "reference.npz",
                         "reference.partial.json", "reference.partial.npz"}.issubset(names))
        self.assertEqual((self.directory / "timeout/stdout.raw").read_bytes(), b"retained synthetic stdout\n")

    def test_kill_worker_targets_process_group_on_posix_and_reaps(self):
        process = Mock(pid=12345)
        process.poll.return_value = None
        if os.name == "posix":
            with patch.object(driver.os, "killpg") as killpg:
                driver._kill_worker(process)
            killpg.assert_called_once_with(12345, signal.SIGKILL)
            process.kill.assert_not_called()
        else:
            driver._kill_worker(process)
            process.kill.assert_called_once_with()
        process.wait.assert_called_once_with(timeout=5)

    def test_symlink_worker_is_refused_before_launch(self):
        link = self.directory / "worker-link.py"
        try:
            link.symlink_to(self.worker_path)
        except (OSError, NotImplementedError) as exc:
            self.skipTest("symlinks unavailable: " + str(exc))
        with patch.object(driver.subprocess, "Popen") as popen:
            record, retained = driver.run_reference_worker("supported_impact", "R0", self.directory / "bad-worker",
                                                           worker_path=link)
        popen.assert_not_called()
        self.assertFalse(record["worker_executed"])
        self.assertEqual(record["worker_identity"]["status"], "unavailable")
        self.assertIsNone(retained)

    def test_wrong_worker_identity_settings_or_raw_provenance_cannot_complete(self):
        mutations = {
            "fixture": lambda row: row.update(fixture="wrong"),
            "level": lambda row: row.update(level="R2"),
            "coverage": lambda row: row.update(coverage="certified"),
            "settings": lambda row: row["settings"].update(maximum_step_s=1.0),
            "fixture_hash": lambda row: row.update(fixture_sha256="0" * 64),
            "raw_hash": lambda row: row["raw"].update(sha256="0" * 64),
            "raw_path": lambda row: row["raw"].update(path="different.npz"),
            "duration_flag": lambda row: row.update(complete_duration=False),
            "final_time": lambda row: row.update(final_time_s=0.019),
        }
        for name, mutate in mutations.items():
            with self.subTest(name=name), patch.object(driver.subprocess, "Popen", side_effect=self.launcher(mutate=mutate)):
                record, _ = driver.run_reference_worker("supported_impact", "R0", self.directory / name)
                self.assertEqual(record["status"], "inconclusive_reference")
                self.assertTrue(record["errors"])
                self.assertTrue((self.directory / name / "reference.json").is_file())
                self.assertTrue((self.directory / name / "reference.npz").is_file())

    def test_missing_final_reference_never_promotes_partial_checkpoints(self):
        report, popen = self.experiment([self.candidate()], launcher=self.launcher(final=False, partial=True))
        self.assertEqual(popen.call_count, 6)
        self.assertEqual((report["reference_execution_status"], report["exit_code"]), ("incomplete", 2))
        self.assertEqual(report["reference_refinement_status"], "inconclusive_reference")
        self.assertTrue(all("worker result JSON is absent" in row["errors"] for row in report["references"]))
        manifest = json.loads((self.directory / "output/manifest.json").read_bytes())
        partials = [row for row in manifest["artifacts"] if ".partial." in row["relative_path"]]
        self.assertEqual(len(partials), 12)
        self.assertTrue(all(row["status"] == "recorded" for row in partials))
        self.assertTrue(all(row["sha256"] == digest(Path(row["path"]).read_bytes()) for row in partials))

    def test_nonzero_worker_preserves_final_records_but_blocks_refinement(self):
        report, popen = self.experiment([self.candidate()], launcher=self.launcher(code=7, partial=True))
        self.assertEqual(popen.call_count, 6)
        self.assertTrue(all(row["exit_code"] == 7 and row["status"] == "inconclusive_reference"
                            for row in report["references"]))
        self.assertEqual(report["reference_refinement_status"], "inconclusive_reference")
        self.assertEqual(report["value_status"], "inconclusive")
        self.assertEqual(report["exit_code"], 2)
        for row in report["references"]:
            raw_reference = json.loads(Path(row["expected_output_path"]).read_bytes())
            self.assertEqual(raw_reference["status"], "completed")
            self.assertTrue(raw_reference["complete_duration"])
        for refinement in report["reference_refinement"].values():
            self.assertTrue(any("incomplete reference execution" in issue for issue in refinement["issues"]))

    def test_known_value_failure_dominates_missing_slots_and_references(self):
        document = candidate_document()
        document["runs"][0]["samples"][-1]["hammer_force_n"] = 21.0
        report, _ = self.experiment([self.candidate(document=document)],
                                    launcher=self.launcher(final=False, partial=True))
        self.assertEqual(report["input_status"], "incomplete")
        self.assertEqual(report["reference_execution_status"], "incomplete")
        self.assertEqual((report["value_status"], report["time_status"]), ("fail", "inconclusive"))
        self.assertEqual((report["diagnostic_status"], report["status"], report["exit_code"]), ("fail", "fail", 1))
        self.assertEqual(report["phy002_acceptance"], "incomplete")
        self.assertEqual(report["PHY002_acceptance"], "incomplete")


class ContactExtremaProvenanceTests(DriverFilesCase):
    def test_source_mutation_blocks_an_otherwise_passing_scoped_result(self):
        before = self.source.read_bytes()
        def mutate(record):
            self.source.write_bytes(before + b"changed during experiment\n")
        report, _ = self.experiment([self.candidate()], launcher=self.launcher(mutate=mutate), complete=True)
        self.assertEqual(report["provenance"]["status"], "fail")
        self.assertEqual(report["provenance"]["changed_or_unavailable_sources"], ["source.py"])
        self.assertEqual(report["provenance"]["source_sha256_before"]["source.py"], digest(before))
        self.assertEqual(report["provenance"]["source_sha256_after"]["source.py"], digest(self.source.read_bytes()))
        self.assertEqual((report["diagnostic_status"], report["status"], report["exit_code"]), ("fail", "fail", 1))

    def test_producer_mutation_is_retained_as_provenance_failure_without_execution(self):
        producer = self.directory / "producer.bin"
        producer.write_bytes(b"before")
        def mutate(record):
            producer.write_bytes(b"after")
        report, popen = self.experiment([self.candidate()], producer=producer,
                                        launcher=self.launcher(mutate=mutate), complete=True)
        self.assertFalse(report["provenance"]["producer_identity_stable"])
        self.assertEqual(report["exit_code"], 1)
        self.assertTrue(all(str(producer) not in item.args[0] for item in popen.call_args_list))
        for filename, raw in (("provenance-before.json", b"before"), ("provenance-after.json", b"after")):
            identity = json.loads((self.directory / "output" / filename).read_bytes())["producer"]
            self.assertEqual(identity["sha256"], digest(raw))
            self.assertFalse(identity["executed"])
            self.assertFalse(identity["candidate_production_attested"])

    def test_unavailable_source_or_producer_is_a_provenance_failure(self):
        candidate = self.candidate()
        self.source.unlink()
        report, _ = self.experiment([candidate], producer=self.directory / "absent-producer", complete=True)
        self.assertEqual(report["provenance"]["status"], "fail")
        self.assertEqual(report["provenance"]["changed_or_unavailable_sources"], ["source.py"])
        self.assertFalse(report["provenance"]["producer_identity_stable"])
        self.assertEqual(report["exit_code"], 1)

    def test_incoherent_reference_sources_do_not_resolve_refinement(self):
        def mutate(record):
            if record["level"] == "R1":
                record["source_sha256"]["synthetic-protocol-source"] = "b" * 64
        report, _ = self.experiment([self.candidate()], launcher=self.launcher(mutate=mutate))
        self.assertEqual(report["reference_execution_status"], "pass")
        self.assertEqual(report["reference_refinement_status"], "inconclusive_reference")
        self.assertEqual(report["exit_code"], 2)
        for row in report["reference_refinement"].values():
            self.assertIn("source identities differ", " ".join(row["issues"]))

    def test_scoped_pass_still_cannot_complete_phy002(self):
        report, _ = self.experiment([self.candidate()], complete=True)
        self.assertEqual((report["diagnostic_status"], report["exit_code"]), ("pass", 0))
        self.assertEqual((report["value_status"], report["time_status"]), ("pass", "pass"))
        self.assertEqual(report["status"], "incomplete")
        for result in (report, json.loads((self.directory / "output/manifest.json").read_bytes())):
            self.assertEqual(result["coverage"], COVERAGE)
            self.assertEqual(result["phy002_acceptance"], "incomplete")
            self.assertEqual(result["PHY002_acceptance"], "incomplete")
        self.assertEqual(report["unevaluated_gates"]["general_auditor_convergence_and_energy"], "not_evaluated")
        self.assertIn("not evaluated or waived", report["unevaluated_gates"]["original_0_8_decrease_rule"])


class ContactExtremaComparisonPolicyTests(unittest.TestCase):
    def references(self):
        return {level: reference_record(level=level) for level in LEVELS}

    def test_independent_value_budgets_keep_failure_thresholds(self):
        for name, target, budget in (("hammer_compression", 0.001, 1.1e-7),
                                     ("hammer_force", 20.0, 0.0041)):
            refined = {"values": {"R2": target}, "value_status": "resolved_under_declared_refinement"}
            with self.subTest(name=name):
                self.assertAlmostEqual(driver.value_budget(name, target), budget, places=16)
                self.assertEqual(driver._global_value(name, target + 0.9 * budget, refined)["status"], "pass")
                self.assertEqual(driver._global_value(name, target + 1.1 * budget, refined)["status"], "fail")

    def test_known_global_failure_survives_missing_and_malformed_evaluations(self):
        document = candidate_document()
        document["runs"][0]["diagnostics"]["located_peak_candidates"]["hammer_compression"]["value_m"] = 0.002
        parsed = driver.parse_candidate(encode(document))
        refs = self.references()
        refined = reference.compare_refinement(list(refs.values()))
        for retained, evaluator in (({}, Mock()), (refs, Mock(return_value={})),
                                     (refs, Mock(side_effect=RuntimeError("synthetic evaluation failed")))):
            with self.subTest(retained=bool(retained), evaluator=repr(evaluator)):
                result = driver.compare_candidate(parsed, retained, refined, evaluator=evaluator)
                self.assertEqual((result["value_status"], result["status"]), ("fail", "fail"))
                peak = result["peaks"]["hammer_compression"]
                self.assertEqual(peak["global_value_comparison"]["status"], "fail")
                self.assertEqual(peak["reference_at_candidate_time"]["status"], "inconclusive")
                self.assertEqual(result["phy002_acceptance"], "incomplete")

    def test_sample_lower_bound_failure_survives_absent_reference(self):
        for force in (21.0, -1.0):
            document = candidate_document()
            document["runs"][0]["samples"][-1]["hammer_force_n"] = force
            with self.subTest(force=force):
                parsed = driver.parse_candidate(encode(document))
                result = driver.compare_candidate(parsed, {}, None, evaluator=Mock())
                self.assertEqual((result["value_status"], result["time_status"], result["status"]),
                                 ("fail", "inconclusive", "fail"))
                sampled = result["peaks"]["hammer_force"]["sample_lower_bound"]
                self.assertEqual(sampled["status"], "fail")
                self.assertEqual(sampled["negative_exported_force"], force < 0)

    def test_passed_values_and_disabled_intervals_do_not_certify_coverage(self):
        parsed = driver.parse_candidate(encode(candidate_document()))
        refs = self.references()
        result = driver.compare_candidate(parsed, refs, reference.compare_refinement(list(refs.values())),
                                          evaluator=Mock(return_value=values()))
        self.assertEqual((result["diagnostic_status"], result["status"]), ("pass", "incomplete"))
        self.assertEqual(result["coverage"], COVERAGE)
        self.assertEqual(result["PHY002_acceptance"], "incomplete")
        for peak in result["peaks"].values():
            self.assertFalse(peak["time_comparison"]["candidate_bracket_used_as_oracle"])
            self.assertEqual(peak["coverage"]["continuous_extrema"], "incomplete")

    def test_coordinate_value_failure_survives_ambiguous_time_pairing(self):
        base = driver.parse_candidate(encode(candidate_document()))
        scaled_document = candidate_document("supported_impact_scaled")
        scaled_document["runs"][0]["diagnostics"]["located_peak_candidates"]["hammer_force"]["value_n"] = 20.001
        scaled = driver.parse_candidate(encode(scaled_document))
        result = driver.coordinate_pairs([base, scaled], [{"peaks": {}}, {"peaks": {}}], {})
        self.assertEqual((result["value_status"], result["time_status"], result["status"]),
                         ("fail", "inconclusive", "fail"))
        pair = next(row for row in result["pairs"] if row["peaks"])
        self.assertEqual(pair["peaks"]["hammer_force"]["value_comparison"]["status"], "fail")
        self.assertEqual(pair["peaks"]["hammer_force"]["time_comparison"]["status"], "inconclusive_pairing")


if __name__ == "__main__":
    unittest.main()
