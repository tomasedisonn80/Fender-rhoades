"""Stdlib protocol and failure-policy tests for the stationary alias driver.

All producer processes and numerical evaluations are mocked. Protocol documents
have the minimum frozen frame counts but deliberately zero motion; they are not
physical or numerical evidence. Tiny temporary files exercise real retention and
hashing. No native probe, compiler, four-case experiment or OS limits are run.
"""
from __future__ import annotations

from contextlib import ExitStack, redirect_stderr, redirect_stdout
import copy
from dataclasses import replace
import hashlib
import io
import json
import math
import os
from pathlib import Path
import signal
import struct
import subprocess
import sys
import tempfile
import time
from types import SimpleNamespace
import unittest
from unittest.mock import Mock, patch


ROOT = Path(__file__).resolve().parents[2]
TOOLS = ROOT / "tools"
with patch.object(sys, "path", [str(TOOLS), *sys.path]):
    import validate_direct_alias_toy as driver


def encode(value):
    return json.dumps(value, separators=(",", ":"), allow_nan=False).encode("utf-8")


def sha(raw):
    return hashlib.sha256(raw).hexdigest()


def document(index=0):
    """Independent wire fixture: structurally valid, deliberately zero motion."""
    case_ids = ("one_mode_48000", "one_mode_96000", "two_modes_48000", "two_modes_96000")
    rate = 48000 if index % 2 == 0 else 96000
    count = 1024 if index % 2 == 0 else 2048
    modes = [(13500, 0.0001, math.pi / 7)] if index < 2 else [
        (7500, 0.0001, math.pi / 7), (13500, 0.00006, -math.pi / 5)]
    return {
        "schema_version": 1, "schema": "rhodes.alias_primitive_probe.v1",
        "provenance": "synthetic_development_fixture", "case_id": case_ids[index],
        "settings": {
            "sample_rate_hz": rate, "frame_count": count, "modal_mass_kg": 0.001,
            "decay_rate_per_second": 0.0, "observation": "before_advance",
            "boundary": "single_pickup_voltage", "pickup_coefficients": {
                "a1_wb_turn_per_m": 0.4, "a2_wb_turn_per_m2": 800.0,
                "a3_wb_turn_per_m3": 400000.0}},
        "modes": [{"frequency_hz": f, "amplitude_m": a, "phase_rad": p} for f, a, p in modes],
        "frames": [{"index": n, "modal_q_m": [0.0] * len(modes),
                    "modal_v_m_per_s": [0.0] * len(modes), "q_m": 0.0,
                    "v_m_per_s": 0.0, "P_volts": 0.0} for n in range(count)],
        "status": "ok", "success": True,
    }


class DriverDeclarationTests(unittest.TestCase):
    def test_help_and_plan_import_only_stdlib_and_write_no_files(self):
        script = """
import builtins, sys
sys.dont_write_bytecode = True
sys.path.insert(0, sys.argv[1])
real_import = builtins.__import__
def guarded(name, *args, **kwargs):
    if name.split('.')[0] in {'numpy', 'scipy'}:
        raise AssertionError('numerical backend imported: ' + name)
    return real_import(name, *args, **kwargs)
builtins.__import__ = guarded
import validate_direct_alias_toy
raise SystemExit(validate_direct_alias_toy.main([sys.argv[2]]))
"""
        for argument in ("--help", "--plan"):
            with self.subTest(argument=argument), tempfile.TemporaryDirectory() as directory:
                result = subprocess.run([sys.executable, "-S", "-c", script, str(TOOLS), argument],
                                        cwd=directory, capture_output=True, text=True, timeout=10)
                self.assertEqual(result.returncode, 0, result.stderr)
                self.assertEqual(result.stderr, "")
                self.assertEqual(list(Path(directory).iterdir()), [])
                if argument == "--help":
                    self.assertIn("--build-record", result.stdout)
                    continue
                declared = json.loads(result.stdout)
                self.assertEqual(declared["total_samples"], 6144)
                self.assertEqual(declared["case_order"], [
                    "one_mode_48000", "one_mode_96000", "two_modes_48000", "two_modes_96000"])
                self.assertIsNone(declared["new_diagnostic_budgets"]["alias_acceptability_threshold"])
                self.assertFalse(declared["new_diagnostic_budgets"]["changes_historical_budgets_or_acceptance"])
                self.assertIn("RT-001 completion", declared["scope_exclusions"])
                self.assertIn("physical fidelity", declared["scope_exclusions"])

    def test_invalid_cli_combinations_fail_before_execute(self):
        forms = ([], ["--probe", "x"], ["--output-dir", "x"], ["--plan", "--probe", "x"],
                 ["--plan", "--build-record", "x"], ["--help", "--probe", "x"],
                 ["--plan", "--plan"], ["--probe=x", "--probe=y", "--output-dir=z"],
                 ["--pro", "x", "--output-dir", "z"])
        for arguments in forms:
            with self.subTest(arguments=arguments), patch.object(driver, "execute") as execute:
                with redirect_stderr(io.StringIO()), self.assertRaises(SystemExit) as caught:
                    driver.main(arguments)
                self.assertEqual(caught.exception.code, 2)
                execute.assert_not_called()

    def test_cli_failure_codes_are_distinct_from_success(self):
        result = {"status": "incomplete", "passed": False, "numerical_passed": True,
                  "reproducibility_complete": False, "output_dir": "unused",
                  "case_count_evaluated": 4, "sample_count_evaluated": 6144,
                  "artifact_bytes_including_manifest": 0}
        with patch.object(driver, "execute", return_value=result), redirect_stdout(io.StringIO()):
            self.assertEqual(driver.main(["--probe", "x", "--output-dir", "y"]), 1)
        with patch.object(driver, "execute", side_effect=FileExistsError("keep")), \
                redirect_stderr(io.StringIO()):
            self.assertEqual(driver.main(["--probe", "x", "--output-dir", "y"]), 2)


class ProbeSchemaTests(unittest.TestCase):
    def test_four_frozen_shapes_accept_only_structural_success(self):
        for index, fixture in enumerate(driver.oracle.fixtures()):
            with self.subTest(case=fixture.case_id):
                expected = document(index)
                parsed = driver.validate_probe_document(encode(expected), fixture)
                self.assertEqual(parsed, expected)
                self.assertEqual(len(parsed["frames"]), fixture.sample_count)
                self.assertNotIn("passed", parsed)
        # A finite incorrect voltage is a later numerical failure, not a parser verdict.
        row = document()
        row["frames"][0]["P_volts"] = 123.0
        self.assertEqual(driver.validate_probe_document(encode(row), driver.oracle.fixtures()[0])
                         ["frames"][0]["P_volts"], 123.0)

    def test_top_level_schema_status_and_exact_keys_are_required(self):
        mutations = {
            "version_boolean": lambda row: row.update(schema_version=True),
            "version_float": lambda row: row.update(schema_version=1.0),
            "version_unknown": lambda row: row.update(schema_version=999),
            "schema": lambda row: row.update(schema="other"),
            "provenance": lambda row: row.update(provenance="measured"),
            "case": lambda row: row.update(case_id="one_mode_96000"),
            "status": lambda row: row.update(status="failed"),
            "success_integer": lambda row: row.update(success=1),
            "success_false": lambda row: row.update(success=False),
            "extra": lambda row: row.update(acceptance=True),
            "missing": lambda row: row.pop("modes"),
        }
        self.reject_mutations(mutations)

    def test_frozen_settings_units_modes_and_sampling_contract_are_required(self):
        mutations = {
            "rate_float": lambda row: row["settings"].update(sample_rate_hz=48000.0),
            "count": lambda row: row["settings"].update(frame_count=1023),
            "mass": lambda row: row["settings"].update(modal_mass_kg=0.002),
            "decay_boolean": lambda row: row["settings"].update(decay_rate_per_second=False),
            "observation": lambda row: row["settings"].update(observation="after_advance"),
            "boundary": lambda row: row["settings"].update(boundary="loaded_harp"),
            "coefficient": lambda row: row["settings"]["pickup_coefficients"].update(a2_wb_turn_per_m2=799),
            "coefficient_key": lambda row: row["settings"]["pickup_coefficients"].update(extra=0),
            "mode_count": lambda row: row["modes"].append(copy.deepcopy(row["modes"][0])),
            "mode_frequency": lambda row: row["modes"][0].update(frequency_hz=13501),
            "mode_amplitude": lambda row: row["modes"][0].update(amplitude_m=-0.0001),
            "mode_phase": lambda row: row["modes"][0].update(phase_rad=0.0),
        }
        self.reject_mutations(mutations)

    def test_frames_require_finite_values_exact_indices_dimensions_and_sums(self):
        mutations = {
            "truncated": lambda row: row["frames"].pop(),
            "extra_frame": lambda row: row["frames"].append(copy.deepcopy(row["frames"][-1])),
            "index_boolean": lambda row: row["frames"][0].update(index=False),
            "index_duplicate": lambda row: row["frames"][1].update(index=0),
            "missing_modal": lambda row: row["frames"][0].update(modal_q_m=[]),
            "extra_modal": lambda row: row["frames"][0].update(modal_v_m_per_s=[0.0, 0.0]),
            "q_sum": lambda row: row["frames"][0].update(q_m=1e-20),
            "v_sum": lambda row: row["frames"][0].update(v_m_per_s=-1e-20),
            "voltage_boolean": lambda row: row["frames"][0].update(P_volts=False),
            "modal_string": lambda row: row["frames"][0].update(modal_q_m=["0"]),
            "unknown_frame_key": lambda row: row["frames"][0].update(time_s=0.0),
        }
        self.reject_mutations(mutations)
        for value in ("NaN", "Infinity", "-Infinity", "1e999"):
            raw = encode(document()).replace(b'"P_volts":0.0', b'"P_volts":' + value.encode(), 1)
            with self.subTest(value=value), self.assertRaises(driver.DiagnosticError):
                driver.validate_probe_document(raw, driver.oracle.fixtures()[0])

    def test_unknown_fixture_is_refused_even_with_matching_document(self):
        fixture = replace(driver.oracle.fixtures()[0], mass_kg=0.002)
        with self.assertRaisesRegex(driver.DiagnosticError, "frozen"):
            driver.validate_probe_document(encode(document()), fixture)

    def test_two_mode_ordered_sum_and_nonfinite_accumulation_are_checked(self):
        fixture = driver.oracle.fixtures()[2]
        row = document(2)
        row["frames"][0].update(modal_q_m=[0.125, -0.25], q_m=-0.125,
                                modal_v_m_per_s=[3.0, -2.0], v_m_per_s=1.0)
        self.assertEqual(driver.validate_probe_document(encode(row), fixture)["frames"][0]["q_m"], -0.125)
        row["frames"][0].update(modal_q_m=[1e308, 1e308], q_m=1e308)
        with self.assertRaisesRegex(driver.DiagnosticError, "nonfinite"):
            driver.validate_probe_document(encode(row), fixture)

    def reject_mutations(self, mutations):
        fixture = driver.oracle.fixtures()[0]
        for label, mutate in mutations.items():
            with self.subTest(mutation=label):
                row = document()
                mutate(row)
                with self.assertRaises(driver.DiagnosticError):
                    driver.validate_probe_document(encode(row), fixture)


class BoundedJsonTests(unittest.TestCase):
    def test_negative_zero_numeric_spelling_preserves_sign_but_is_not_an_index(self):
        value = driver._bounded_json(b'-0')
        self.assertEqual(math.copysign(1.0, value), -1.0)
        raw = encode(document()).replace(b'"P_volts":0.0', b'"P_volts":-0', 1)
        parsed = driver.validate_probe_document(raw, driver.oracle.fixtures()[0])
        self.assertEqual(math.copysign(1.0, parsed["frames"][0]["P_volts"]), -1.0)
        raw = encode(document()).replace(b'"index":0', b'"index":-0', 1)
        with self.assertRaises(driver.DiagnosticError):
            driver.validate_probe_document(raw, driver.oracle.fixtures()[0])

    def test_duplicate_keys_and_multiple_or_incomplete_documents_are_rejected(self):
        raws = (b'{"x":1,"x":2}', b'{"outer":{"x":1,"x":2}}', b'{}{}', b'{"x":',
                b'"unterminated', b'\xff', b'\xef\xbb\xbf{}', b'', b'NaN', b'Infinity')
        for raw in raws:
            with self.subTest(raw=raw), self.assertRaises(driver.DiagnosticError):
                driver._bounded_json(raw)

    def test_byte_depth_string_token_and_number_caps_precede_large_allocation(self):
        raws = (b'[' * 13 + b'0' + b']' * 13,
                encode("x" * (driver.MAX_JSON_STRING + 1)),
                b'9' * 65, b'9223372036854775808', b'-9223372036854775809',
                b'1.' + b'1' * 64,
                encode({str(index): 0 for index in range(33)}))
        for raw in raws:
            with self.subTest(prefix=raw[:30]), self.assertRaises(driver.DiagnosticError):
                driver._bounded_json(raw)
        with self.assertRaises(driver.DiagnosticError):
            driver._bounded_json(b'{} ', maximum=2)
        with patch.object(driver, "MAX_JSON_TOKENS", 4), self.assertRaises(driver.DiagnosticError):
            driver._bounded_json(b'[0,0,0,0]')
        self.assertEqual(driver._bounded_json(b'{"x":"[}]\\\"","n":-9223372036854775808}'),
                         {"x": '[}]"', "n": -(2**63)})


class TemporaryCase(unittest.TestCase):
    def setUp(self):
        self.stack = ExitStack()
        self.addCleanup(self.stack.close)
        self.directory = Path(self.stack.enter_context(tempfile.TemporaryDirectory())).resolve()


class ArtifactTests(TemporaryCase):
    def test_existing_output_directory_and_sentinel_are_preserved(self):
        output = self.directory / "existing"
        output.mkdir()
        sentinel = output / "keep"
        sentinel.write_bytes(b"original")
        with patch.object(driver, "run_probe") as probe, self.assertRaises(FileExistsError):
            driver.execute(self.directory / "absent", output)
        probe.assert_not_called()
        self.assertEqual(sentinel.read_bytes(), b"original")
        self.assertEqual(list(output.iterdir()), [sentinel])

    def test_duplicate_names_path_escape_and_count_cap_cannot_overwrite(self):
        store = driver.ArtifactStore(self.directory / "output")
        store.write_bytes("record.raw", b"original", 32)
        for name in ("record.raw", "../escape", "/absolute", ".", "..", "x/y", "a" * 97):
            with self.subTest(name=name), self.assertRaises(driver.DiagnosticError):
                store.write_bytes(name, b"replacement", 32)
        self.assertEqual((store.path / "record.raw").read_bytes(), b"original")
        with patch.object(driver, "MAX_ARTIFACTS", 1), self.assertRaises(driver.DiagnosticError):
            store.write_bytes("second.raw", b"x", 32)

    def test_partial_write_retains_exact_prefix_hash_and_incomplete_flag(self):
        store = driver.ArtifactStore(self.directory / "output")
        with self.assertRaises(driver.DiagnosticError):
            with store.open("partial.raw", 4) as stream:
                stream.write(b"abc")
                stream.write(b"de")
        self.assertEqual((store.path / "partial.raw").read_bytes(), b"abc")
        self.assertEqual(store.entries["partial.raw"], {
            "bytes": 3, "sha256": sha(b"abc"), "write_complete": False})
        self.assertEqual(store.total, 3)

    def test_final_metadata_reserve_is_unavailable_to_regular_artifacts(self):
        store = driver.ArtifactStore(self.directory / "output")
        with patch.object(driver, "MAX_ARTIFACT_BYTES", 10), \
                patch.object(driver, "FINAL_RESERVE_BYTES", 4):
            store.write_bytes("regular", b"123456", 10)
            with self.assertRaises(driver.DiagnosticError):
                store.write_bytes("too_much", b"7", 10)
            store.write_bytes("final", b"7890", 10, final=True)
            with self.assertRaises(driver.DiagnosticError):
                store.write_bytes("beyond_total", b"x", 10, final=True)
        self.assertEqual(store.total, 10)

    @unittest.skipUnless(hasattr(os, "mkfifo"), "FIFO creation unavailable")
    def test_fifo_hash_input_is_rejected_before_open_without_blocking(self):
        path = self.directory / "pipe"
        os.mkfifo(path)
        with patch.object(Path, "open", side_effect=AssertionError("FIFO must never open")) as opened, \
                patch.object(driver.os, "open", side_effect=AssertionError("FIFO must never open")) as opened_fd:
            with self.assertRaises((driver.DiagnosticError, OSError)):
                driver._hash_file(path, 32)
        opened.assert_not_called()
        opened_fd.assert_not_called()

    def test_retained_array_hashes_bind_binary64_endian_order_and_negative_zero(self):
        values = [[1.0, -0.0], [-2.5, 0.125]]
        row = driver._array(values, 2, "m", 2)
        self.assertEqual(row["shape"], [2, 2])
        self.assertEqual(row["units"], "m")
        self.assertEqual(row["sha256_f64le"], sha(struct.pack("<dddd", 1.0, -0.0, -2.5, 0.125)))
        self.assertNotEqual(row["sha256_f64le"], sha(struct.pack("<dddd", 1.0, 0.0, -2.5, 0.125)))
        for values, count, columns in (([1.0], 2, None), ([[1.0]], 1, 2),
                                       ([True], 1, None), ([math.inf], 1, None)):
            with self.subTest(values=values), self.assertRaises(driver.DiagnosticError):
                driver._array(values, count, "m", columns)

    def test_oversized_regular_hash_input_is_rejected_before_open(self):
        path = self.directory / "oversized"
        path.write_bytes(b"12345")
        with patch.object(driver.os, "open", side_effect=AssertionError("oversized file must not open")) as opened:
            with self.assertRaises(driver.DiagnosticError):
                driver._hash_file(path, 4)
        opened.assert_not_called()

    def test_replacement_between_stat_and_open_fails_descriptor_identity(self):
        path = self.directory / "input"
        replacement = self.directory / "replacement"
        path.write_bytes(b"original")
        replacement.write_bytes(b"changed!")
        real_open = os.open
        def changed_open(*args, **kwargs):
            replacement.replace(path)
            return real_open(*args, **kwargs)
        with patch.object(driver.os, "open", side_effect=changed_open), \
                self.assertRaisesRegex(driver.DiagnosticError, "identity/type changed"):
            driver._hash_file(path, 32)


class MockLimits:
    """Policy tests must not alter the test runner's actual resource limits."""
    def __init__(self, start):
        self.deadline = start + 27.0
        self.overall_deadline = start + 30.0
        self.record = {"test_only": "OS limits and alarms mocked; no enforcement evidence"}

    def __enter__(self):
        return self

    def __exit__(self, *_):
        return None

    def stop_alarm(self):
        return None

    def arm(self, *_):
        return None


class ExecutionTests(TemporaryCase):
    def setUp(self):
        super().setUp()
        for name in driver.SOURCE_PATHS:
            path = self.directory / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(("synthetic unexecuted source: " + name + "\n").encode())
        self.probe = self.directory / "mock-probe"
        self.probe.write_bytes(b"not a native binary; never executed\n")
        self.probe.chmod(0o700)
        self.compiler = self.directory / "mock-compiler"
        self.compiler.write_bytes(b"not a compiler; never executed\n")
        self.stack.enter_context(patch.object(driver, "ROOT", self.directory))
        self.stack.enter_context(patch.object(driver, "ORACLE_SHA256", sha(
            (self.directory / "tools/direct_alias_oracle.py").read_bytes())))
        self.stack.enter_context(patch.object(driver, "PRODUCER_SHA256", sha(
            (self.directory / "apps/alias_primitive_probe.cpp").read_bytes())))
        self.stack.enter_context(patch.object(driver, "_Limits", MockLimits))

    def build_record(self, mutate=None):
        receipt = {
            "compiler_path": str(self.compiler), "compiler_sha256": sha(self.compiler.read_bytes()),
            "compiler_version": "synthetic claimed version; no compiler run",
            "source_sha256_before": {name: sha((self.directory / name).read_bytes())
                                     for name in driver.RECEIPT_SOURCES},
            "compile_command": [str(self.compiler), "-std=c++20", "-O2", "-I", "include",
                                "apps/alias_primitive_probe.cpp", "src/research_engine.cpp",
                                "-o", str(self.probe)],
            "compile_returncode": 0, "compile_stdout": "", "compile_stderr": "",
            "binary_sha256_before": sha(self.probe.read_bytes()),
        }
        if mutate:
            mutate(receipt)
        path = self.directory / "build.json"
        path.write_bytes(encode(receipt))
        return path

    def execute(self, *, receipt=None, mutate=None, numerical=True, process=True, name="output", raised=None):
        calls = []
        def run(probe, fixture, output, deadline):
            calls.append(fixture.case_id)
            raw_name = fixture.case_id + ".stdout"
            output.write_bytes(raw_name, b'{"test":"mocked probe"}\n', 64)
            if raised is not None:
                raise raised
            if mutate:
                mutate(fixture, output)
            return {"succeeded": process, "returncode": 0 if process else 2, "stdout_file": raw_name}
        def evaluate(output, fixture, doc, deadline):
            return {"passed": numerical, "sample_count": fixture.sample_count,
                    "failures": [] if numerical else ["primitive_numerical_peak"]}
        with patch.object(driver, "run_probe", side_effect=run) as probe_call, \
                patch.object(driver, "validate_probe_document", return_value={"frames": [None]}) as parse, \
                patch.object(driver, "_evaluate_case", side_effect=evaluate) as numerical_call:
            result = driver.execute(self.probe, self.directory / name, receipt)
        return result, calls, probe_call, parse, numerical_call

    def metrics(self, result):
        return json.loads((Path(result["output_dir"]) / "metrics.json").read_bytes())

    def test_absent_receipt_keeps_numerical_result_but_reproducibility_incomplete(self):
        result, calls, *_ = self.execute()
        self.assertEqual(calls, list(driver.CASE_IDS))
        self.assertEqual(result["status"], "incomplete")
        self.assertFalse(result["passed"])
        self.assertTrue(result["numerical_passed"])
        self.assertFalse(result["reproducibility_complete"])
        self.assertEqual(result["sample_count_evaluated"], 6144)
        self.assertEqual(result["build_receipt"]["compiler_provenance"], "unavailable")
        self.assertFalse(result["build_receipt"]["compilation_proven"])
        self.assertIn("compiler/build provenance unavailable", result["incomplete_reasons"][0])

    def test_consistent_receipt_is_a_byte_claim_never_compilation_or_acceptance_proof(self):
        result, calls, *_ = self.execute(receipt=self.build_record())
        self.assertTrue(result["passed"], result["failures"])
        self.assertTrue(result["reproducibility_complete"])
        self.assertFalse(result["build_receipt"]["compilation_proven"])
        self.assertIn("no alias acceptability or acceptance promotion", result["scope"])
        self.assertFalse(self.metrics(result)["historical_acceptance_changed"])
        self.assertIsNone(self.metrics(result)["alias_acceptability_threshold"])
        self.assertEqual(calls, list(driver.CASE_IDS))
        output = Path(result["output_dir"])
        for name, record in result["artifacts"].items():
            raw = (output / name).read_bytes()
            self.assertEqual(record["bytes"], len(raw))
            self.assertEqual(record["sha256"], sha(raw))
        self.assertEqual(sum(path.stat().st_size for path in output.iterdir()),
                         result["artifact_bytes_including_manifest"])

    def test_malformed_receipts_stop_before_probe_and_retain_original_bytes(self):
        mutations = {
            "nonzero_compile": lambda row: row.update(compile_returncode=2),
            "bool_compile": lambda row: row.update(compile_returncode=False),
            "binary_hash": lambda row: row.update(binary_sha256_before="0" * 64),
            "compiler_hash": lambda row: row.update(compiler_sha256="0" * 64),
            "relative_compiler": lambda row: row.update(compiler_path="compiler"),
            "source_hash": lambda row: row["source_sha256_before"].update({driver.RECEIPT_SOURCES[0]: "0" * 64}),
            "extra_source": lambda row: row["source_sha256_before"].update({"unrelated.cpp": "0" * 64}),
            "wrong_output": lambda row: row["compile_command"].__setitem__(-1, str(self.directory / "elsewhere")),
            "response_file": lambda row: row["compile_command"].insert(1, "@hidden.rsp"),
            "extra_cpp": lambda row: row["compile_command"].insert(1, "unknown.cpp"),
        }
        for index, (label, mutate) in enumerate(mutations.items()):
            with self.subTest(mutation=label):
                receipt = self.build_record(mutate)
                original = receipt.read_bytes()
                result, calls, *_ = self.execute(receipt=receipt, name=f"invalid-{index}")
                self.assertEqual(result["status"], "failed")
                self.assertEqual(calls, [])
                self.assertFalse(result["numerical_passed"])
                self.assertEqual((Path(result["output_dir"]) / "build_record.raw.json").read_bytes(), original)

    def test_numeric_failure_dominates_missing_receipt_and_remaining_cases(self):
        result, calls, *_ = self.execute(numerical=False)
        self.assertEqual(result["status"], "failed")
        self.assertFalse(result["passed"])
        self.assertFalse(result["numerical_passed"])
        self.assertEqual(calls, ["one_mode_48000"])
        cases = self.metrics(result)["cases"]
        self.assertEqual([case["status"] for case in cases], ["failed", "not_run", "not_run", "not_run"])
        self.assertEqual(cases[0]["metrics"]["failures"], ["primitive_numerical_peak"])
        self.assertTrue(result["incomplete_reasons"])

    def test_process_failure_blocks_parsing_and_numerical_work_and_later_cases(self):
        result, calls, _, parse, numerical = self.execute(process=False)
        self.assertEqual(result["status"], "failed")
        self.assertEqual(calls, ["one_mode_48000"])
        parse.assert_not_called()
        numerical.assert_not_called()
        self.assertEqual(self.metrics(result)["cases"][0]["process"]["returncode"], 2)

    def test_deadline_failure_preserves_partial_artifact_and_stops_later_cases(self):
        result, calls, _, parse, numerical = self.execute(
            raised=driver.DeadlineExceeded("injected diagnostic deadline"))
        self.assertEqual(result["status"], "failed")
        self.assertEqual(calls, ["one_mode_48000"])
        parse.assert_not_called()
        numerical.assert_not_called()
        self.assertEqual(result["case_count_evaluated"], 0)
        self.assertEqual(result["failures"][0]["type"], "DeadlineExceeded")
        self.assertEqual((Path(result["output_dir"]) / "one_mode_48000.stdout").read_bytes(),
                         b'{"test":"mocked probe"}\n')

    def test_changed_source_and_probe_bytes_fail_final_provenance(self):
        for index, path in enumerate((self.directory / "src/research_engine.cpp", self.probe)):
            with self.subTest(path=path.name):
                receipt = self.build_record()
                original = path.read_bytes()
                def mutate(fixture, output):
                    if fixture.case_id == "one_mode_48000":
                        path.write_bytes(original + b"changed\n")
                result, _, *_ = self.execute(receipt=receipt, mutate=mutate, name=f"changed-{index}")
                self.assertEqual(result["status"], "failed")
                self.assertTrue(result["numerical_passed"])
                self.assertFalse(result["reproducibility_complete"])
                key = "probe" if path == self.probe else "src/research_engine.cpp"
                self.assertEqual(result["inputs_before"][key]["sha256"], sha(original))
                self.assertEqual(result["inputs_after"][key]["sha256"], sha(path.read_bytes()))
                self.assertIn("input_integrity", [error["phase"] for error in result["failures"]])
                path.write_bytes(original)

    def test_changed_retained_plan_fails_even_when_metrics_and_receipt_pass(self):
        def mutate(fixture, output):
            if fixture.case_id == "one_mode_48000":
                with (output.path / "plan.json").open("ab") as stream:
                    stream.write(b" ")
        result, *_ = self.execute(receipt=self.build_record(), mutate=mutate)
        self.assertEqual(result["status"], "failed")
        self.assertFalse(result["reproducibility_complete"])
        self.assertNotEqual(result["plan_sha256_before"], result["plan_sha256_after"])

    def test_changed_retained_probe_bytes_cannot_leave_a_successful_archive(self):
        def mutate(fixture, output):
            if fixture.case_id == "two_modes_96000":
                path = output.path / "one_mode_48000.stdout"
                path.write_bytes(b"changed after consumption\n")
        result, *_ = self.execute(receipt=self.build_record(), mutate=mutate)
        self.assertEqual(result["status"], "failed")
        self.assertFalse(result["passed"])
        self.assertFalse(result["reproducibility_complete"])
        self.assertTrue(result["failures"])


class ScriptedQueue:
    def __init__(self, messages):
        self.messages = iter(messages)

    def get(self, timeout):
        value = next(self.messages)
        if isinstance(value, BaseException):
            raise value
        return value


class ProbeCaptureTests(TemporaryCase):
    def capture(self, messages, *, code=0, cap=1024, popen_error=None):
        fixture = driver.oracle.fixtures()[0]
        store = driver.ArtifactStore(self.directory / "output")
        child = Mock(pid=123456)
        child.poll.return_value = code
        child.wait.return_value = code
        child.stdout, child.stderr = Mock(), Mock()
        thread = Mock()
        thread.is_alive.return_value = False
        with patch.object(driver.queue, "Queue", return_value=ScriptedQueue(messages)), \
                patch.object(driver.threading, "Thread", return_value=thread), \
                patch.object(driver.subprocess, "Popen", side_effect=popen_error, return_value=child) as popen, \
                patch.object(driver.os, "killpg", create=True) as killpg, \
                patch.object(driver, "MAX_STREAM_BYTES", cap):
            record = driver.run_probe(self.directory / "never-executed", fixture, store, time.monotonic() + 60)
        return record, store, popen, child, killpg

    def test_capture_retains_exact_stream_bytes_and_separately_records_process_success(self):
        record, store, popen, child, _ = self.capture([
            ("stdout", b'{"arbitrary":true}\n', None), ("stderr", b"diagnostic\x00\n", None),
            ("stdout", b"", None), ("stderr", b"", None)])
        self.assertTrue(record["succeeded"])
        self.assertTrue(record["reaped"])
        self.assertFalse(record["kill_requested"])
        self.assertEqual((store.path / record["stdout_file"]).read_bytes(), b'{"arbitrary":true}\n')
        self.assertEqual((store.path / record["stderr_file"]).read_bytes(), b"diagnostic\x00\n")
        self.assertEqual(record["stdout_bytes_observed"], record["stdout_bytes_retained"])
        self.assertFalse(popen.call_args.kwargs["shell"])
        self.assertEqual(popen.call_args.kwargs["stdin"], subprocess.DEVNULL)
        self.assertEqual(popen.call_args.kwargs["env"]["LC_ALL"], "C")
        self.assertEqual(popen.call_args.kwargs["start_new_session"], os.name == "posix")
        child.stdout.close.assert_called_once()
        child.stderr.close.assert_called_once()

    def test_nonzero_exit_cannot_be_repaired_by_success_json(self):
        raw = b'{"status":"ok","success":true}\n'
        record, store, _, _, _ = self.capture([
            ("stdout", raw, None), ("stdout", b"", None), ("stderr", b"", None)], code=2)
        self.assertFalse(record["succeeded"])
        self.assertEqual(record["returncode"], 2)
        self.assertTrue(record["kill_requested"])
        self.assertEqual((store.path / record["stdout_file"]).read_bytes(), raw)
        self.assertIn("probe exited 2", record["errors"][0]["message"])

    def test_output_cap_kills_and_retains_only_exact_prefix(self):
        record, store, _, _, killpg = self.capture([("stdout", b"123456789", None)], cap=8)
        self.assertFalse(record["succeeded"])
        self.assertTrue(record["kill_requested"])
        self.assertTrue(record["reaped"])
        self.assertEqual(record["stdout_bytes_observed"], 9)
        self.assertEqual(record["stdout_bytes_retained"], 8)
        self.assertFalse(record["stdout_capture_complete"])
        self.assertEqual((store.path / record["stdout_file"]).read_bytes(), b"12345678")
        if os.name == "posix":
            killpg.assert_called_once_with(123456, signal.SIGKILL)

    def test_timeout_preserves_partial_bytes_and_blocks_success(self):
        record, store, _, _, _ = self.capture([
            ("stdout", b'{"partial":', None), driver.DeadlineExceeded("injected deadline")])
        self.assertFalse(record["succeeded"])
        self.assertTrue(record["kill_requested"])
        self.assertFalse(record["stdout_capture_complete"])
        self.assertEqual((store.path / record["stdout_file"]).read_bytes(), b'{"partial":')
        self.assertEqual(record["errors"][0]["type"], "DeadlineExceeded")

    def test_spawn_failure_still_retains_empty_streams_and_process_record(self):
        record, store, _, _, _ = self.capture([], popen_error=OSError("injected spawn failure"))
        self.assertFalse(record["succeeded"])
        self.assertIsNone(record["returncode"])
        self.assertFalse(record["kill_requested"])
        self.assertEqual((store.path / record["stdout_file"]).read_bytes(), b"")
        self.assertEqual((store.path / record["stderr_file"]).read_bytes(), b"")
        self.assertEqual(json.loads((store.path / "one_mode_48000.process.json").read_bytes()), record)


if __name__ == "__main__":
    unittest.main()
