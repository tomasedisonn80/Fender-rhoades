"""Q6 study protocol checks; these do not run the full waveform matrix.

Plan, parser and publication tests use only the standard library. The parser's
array conversion and short studies' numerical work are isolated test doubles.
The actual numerical assessment/NPZ tests explicitly skip without NumPy/SciPy.
"""
from __future__ import annotations

from collections import Counter
from contextlib import ExitStack, redirect_stderr, redirect_stdout
import importlib.util
import io
import json
import math
from pathlib import Path
import subprocess
import sys
import tempfile
from types import SimpleNamespace
import unittest
from unittest import mock


ROOT = Path(__file__).resolve().parents[2]
TOOL = ROOT / "tools/validate_quadrature_audio.py"
SPEC = importlib.util.spec_from_file_location("quadrature_audio_validation_tests", TOOL)
if SPEC is None or SPEC.loader is None:
    raise RuntimeError("Cannot load quadrature audio validation")
study = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = study
SPEC.loader.exec_module(study)
HAVE_SCIENCE = (importlib.util.find_spec("numpy") is not None
                and importlib.util.find_spec("scipy") is not None)


def small_case(identifier="protocol-check", frames=120, rate=48000.0):
    return study.Case(identifier, "selfcheck", rate, frames, (0,), 1.0,
                      (study.Event(0, "on", 0, 1.0),))


def probe_document(case):
    return {"schema_version": 1, "model": "synthetic-quadrature-audio",
            "sample_rate_hz": case.sample_rate_hz, "quadrature_nodes": 6,
            "observation_scheme": "gauss-legendre-host-cell", "latency_frames": 48,
            "frames": case.frames, "event_count": len(case.events),
            "samples_volts": [0.0] * case.frames}


class QuadraturePlanTests(unittest.TestCase):
    def test_full_matrix_retains_frozen_legacy_cases_and_gates(self):
        before = study.canonical_bytes(study.reference.make_plan("full"))
        plan = study.make_plan("full")
        self.assertEqual((plan["case_count"], plan["host_frame_count"]), (2097, 2534400))
        self.assertEqual(Counter(case["family"] for case in plan["cases"]),
                         {"attack": 1971, "events": 72, "chord": 54})
        self.assertEqual(plan["legacy_filtered_plan_sha256"],
                         "21d84b3f99d80a9d54d852f7bcd9719d8520625d2cc70cb24c1478a5e007af0a")
        self.assertEqual(plan["case_matrix_sha256"],
                         "f3d75b829f214959cc24f8490403c912d4d58758be4f2514796a77d6e23a14a1")
        self.assertEqual(plan["cases"], json.loads(before)["cases"])
        self.assertEqual(plan["gates"], json.loads(before)["gates"])
        self.assertEqual(plan["implementation_quadrature_nodes"], 6)
        self.assertEqual(plan["quadrature_orders_per_unit_cell"], [6, 16, 32, 64])
        self.assertEqual(before, study.canonical_bytes(study.reference.make_plan("full")))
        digest = plan.pop("plan_sha256")
        self.assertEqual(digest, study.object_hash(plan))
        self.assertNotEqual(digest, plan["legacy_filtered_plan_sha256"])

    def test_pilot_is_frozen_subset_and_plans_keep_adjacent_binary64_rates(self):
        full = {case.id: case.document() for case in study.build_cases("full")}
        pilot = study.make_plan("pilot")
        self.assertEqual((pilot["case_count"], pilot["host_frame_count"]), (36, 92160))
        self.assertEqual(pilot["legacy_filtered_plan_sha256"],
                         "ecfd0d875e906a6b4f562aadd55fd09daba5d45411f29a3c30e34d2deb0f8662")
        self.assertEqual(pilot["case_matrix_sha256"],
                         "8460d80e539b7c8806d9bb10340f61801ba324f23109ea32ebed1d05093469c9")
        for case in pilot["cases"]:
            self.assertEqual(case, full[case["case_id"]])
        rates = {case["sample_rate_hz"] for case in full.values()}
        self.assertEqual(len(rates), 9)
        for boundary in (48000.0, 96000.0):
            self.assertIn(boundary, rates)
            self.assertIn(math.nextafter(boundary, math.inf), rates)
        for case in full.values():
            self.assertEqual(float.fromhex(case["sample_rate_binary64_hex"]), case["sample_rate_hz"])

    def test_help_and_plans_without_site_packages(self):
        for arguments in (("--help",), ("--plan", "--suite", "full"),
                          ("--plan", "--suite", "pilot")):
            with self.subTest(arguments=arguments):
                result = subprocess.run([sys.executable, "-S", str(TOOL), *arguments],
                                        capture_output=True, text=True, check=False)
                self.assertEqual(result.returncode, 0, result.stderr)
                self.assertEqual(result.stderr, "")
                if "--plan" in arguments:
                    self.assertEqual(study.object_hash(json.loads(result.stdout)),
                                     study.object_hash(study.make_plan(arguments[-1])))

    def test_cli_rejects_ambiguous_plan_and_missing_run_arguments(self):
        for arguments in ([], ["--plan", "--probe", "unused"],
                          ["--plan", "--output", "unused"], ["--suite", "unknown"]):
            with self.subTest(arguments=arguments), redirect_stderr(io.StringIO()):
                with self.assertRaises(SystemExit) as error:
                    study.main(arguments)
                self.assertEqual(error.exception.code, 2)

    def test_source_snapshot_detects_test_additions_and_portable_path_keys(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / "tests/python").mkdir(parents=True)
            (root / "tests/cpp").mkdir()
            paths = ("core.cpp", "tests/python/test_quadrature_case.py",
                     "tests/cpp/example_quadrature_test.cpp")
            for name in paths:
                (root / name).write_bytes(name.encode())
            (root / "tests/python/test_unrelated.py").write_bytes(b"not captured")
            with mock.patch.object(study, "ROOT", root), \
                    mock.patch.object(study, "SOURCE_PATHS", ("core.cpp",)):
                before = study.source_snapshot()
                self.assertEqual(list(before), sorted(paths))
                for name in paths:
                    self.assertEqual(before[name], study.file_hash(root / name))
                extra = root / "tests/python/test_quadrature_added.py"
                extra.write_bytes(b"first")
                after_addition = study.source_snapshot()
                self.assertEqual(set(after_addition) - set(before), {"tests/python/test_quadrature_added.py"})
                extra.write_bytes(b"changed")
                self.assertNotEqual(after_addition, study.source_snapshot())


class QuadratureProbeParserTests(unittest.TestCase):
    def setUp(self):
        self.case = small_case(frames=4, rate=math.nextafter(48000.0, math.inf))
        # Parsing/validation is stdlib; isolate only the final array conversion.
        self.array_conversion = mock.Mock(side_effect=lambda values, dtype: tuple(values))
        array_module = SimpleNamespace(asarray=self.array_conversion, float64=float)
        patch = mock.patch.object(study, "numerical_libraries", return_value=(array_module, None))
        patch.start()
        self.addCleanup(patch.stop)

    def reject(self, document):
        with self.assertRaises(ValueError):
            study.parse_probe_output(json.dumps(document).encode(), self.case)

    def test_valid_record_preserves_exact_rate_samples_and_signed_zero(self):
        valid = probe_document(self.case)
        valid["samples_volts"] = [0, -0.0, 1e-300, -2.5]
        values, metadata = study.parse_probe_output(study.canonical_bytes(valid), self.case)
        self.assertEqual(values, (0.0, -0.0, 1e-300, -2.5))
        self.assertEqual(math.copysign(1.0, values[1]), -1.0)
        self.assertEqual(metadata["sample_rate_hz"].hex(), self.case.sample_rate_hz.hex())
        self.assertNotIn("samples_volts", metadata)
        self.array_conversion.assert_called_once_with([0.0, -0.0, 1e-300, -2.5], dtype=float)

    def test_required_integer_fields_are_never_coerced(self):
        valid = probe_document(self.case)
        for name in ("schema_version", "frames", "event_count", "quadrature_nodes", "latency_frames"):
            for value in (None, True, str(valid[name]), float(valid[name]), valid[name] + 1):
                with self.subTest(name=name, value=value):
                    self.reject({**valid, name: value})
            missing = dict(valid)
            del missing[name]
            with self.subTest(missing=name):
                self.reject(missing)
        self.array_conversion.assert_not_called()

    def test_identity_rate_and_uniform_sampling_claims_are_rejected(self):
        valid = probe_document(self.case)
        for name, value in (("model", "synthetic-filtered-audio"), ("model", None),
                            ("observation_scheme", "uniform"), ("observation_scheme", None),
                            ("sample_rate_hz", 48000.0), ("sample_rate_hz", True),
                            ("sample_rate_hz", str(self.case.sample_rate_hz)),
                            ("sample_rate_hz", math.inf), ("sample_rate_hz", math.nan),
                            ("oversampling_factor", None), ("internal_sample_rate_hz", None)):
            with self.subTest(name=name, value=value):
                self.reject({**valid, name: value})
        self.array_conversion.assert_not_called()

    def test_invalid_samples_nonfinite_tokens_and_duplicate_fields(self):
        valid = probe_document(self.case)
        for samples in (None, "0000", {}, [0] * 3, [0] * 5,
                        [True] * 4, [None] * 4, ["0"] * 4,
                        [math.nan] * 4, [math.inf] * 4, [-math.inf] * 4, [10 ** 400] * 4):
            with self.subTest(samples=str(samples)[:50]):
                self.reject({**valid, "samples_volts": samples})
        raw = study.canonical_bytes(valid)
        for duplicate in (b'"schema_version":1,', b'"samples_volts":[0,0,0,0],'):
            with self.subTest(duplicate=duplicate), self.assertRaisesRegex(ValueError, "Duplicate"):
                study.parse_probe_output(b"{" + duplicate + raw[1:], self.case)
        for raw in (b"[]", b"null", b"true", b"{} {}", b"NaN", b"Infinity", b"-Infinity",
                    b" " * (1024 * 1024 + 1)):
            with self.subTest(raw=raw[:30]), self.assertRaises(ValueError):
                study.parse_probe_output(raw, self.case)
        self.array_conversion.assert_not_called()


class QuadratureRunProtocolTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        self.probe = self.root / "probe.bin"
        self.probe.write_bytes(b"retained fake executable identity\n")
        self.output = self.root / "result"
        self.cases = (small_case("first"), small_case("second"))
        self.stack = ExitStack()
        self.addCleanup(self.stack.close)
        self.stack.enter_context(redirect_stdout(io.StringIO()))
        self.stack.enter_context(redirect_stderr(io.StringIO()))
        plan = {"plan_sha256": "stub-plan", "legacy_filtered_plan_sha256": "stub-legacy",
                "case_matrix_sha256": "stub-matrix"}
        for owner, name, value in (
            (study, "numerical_libraries", (SimpleNamespace(__version__="protocol-stub"),
                                            SimpleNamespace(__version__="protocol-stub"))),
            (study, "make_plan", plan), (study, "build_cases", self.cases),
            (study, "source_snapshot", {"source": "fixed"}),
            (study.reference, "kernel_normalizer_report", {"adaptive_cell_quad": 1.0}),
            (study, "quadrature_q6", SimpleNamespace(normalizer=1.0)),
            (study.reference, "quadrature", SimpleNamespace(normalizer=1.0)),
        ):
            self.stack.enter_context(mock.patch.object(owner, name, return_value=value))

    def row(self, case, a="pass", b="pass"):
        metric = {"maximum_absolute_error_volts": 0.0, "rms_error_volts": 0.0}
        return {**case.document(), "comparison_a": {"status": a, "metrics": dict(metric)},
                "comparison_b": {"status": b, "metrics": None if b.startswith("inconclusive") else dict(metric)},
                "reference_refinement": {"metrics": {"q16_q32": dict(metric), "q32_q64": dict(metric)}}}

    def run_rows(self, rows):
        with mock.patch.object(study, "execute_case", side_effect=rows):
            code = study.run_study(self.probe, self.output, "pilot")
        return code, json.loads((self.output / "summary.json").read_text())

    def test_complete_run_retains_hashes_and_refuses_overwrite(self):
        code, summary = self.run_rows([self.row(case) for case in self.cases])
        self.assertEqual(code, 0)
        self.assertEqual(summary["status"], "complete")
        self.assertEqual((summary["expected_cases"], summary["completed_cases"]), (2, 2))
        self.assertTrue(summary["source_and_binary_bytes_unchanged"])
        self.assertEqual(summary["cases_jsonl_sha256"], study.file_hash(self.output / "cases.jsonl"))
        manifest = json.loads((self.output / "manifest.json").read_text())
        self.assertEqual(manifest["probe_binary_sha256"], study.file_hash(self.probe))
        self.assertEqual(manifest["source_snapshot_sha256"], study.object_hash({"source": "fixed"}))
        saved = {path.name: path.read_bytes() for path in self.output.iterdir() if path.is_file()}
        self.assertEqual(set(saved), {"plan.json", "manifest.json", "cases.jsonl", "summary.json"})
        with self.assertRaises(ValueError):
            study.run_study(self.probe, self.output, "pilot")
        with self.assertRaises(FileExistsError):
            study.reference.write_exclusive(self.output / "summary.json", b"overwrite")
        self.assertEqual(saved, {path.name: path.read_bytes() for path in self.output.iterdir() if path.is_file()})

    def test_numerical_failure_and_inconclusive_exit_precedence(self):
        unresolved = "inconclusive: reference not resolved"
        for index, (a, b, expected) in enumerate((("fail", "pass", 1),
                                                 ("pass", unresolved, 2),
                                                 ("fail", unresolved, 1),
                                                 ("pass", "fail", 1))):
            with self.subTest(a=a, b=b):
                self.output = self.root / f"result-{index}"
                code, summary = self.run_rows([self.row(self.cases[0], a, b), self.row(self.cases[1])])
                self.assertEqual(code, expected)
                self.assertEqual(summary["status"], "complete")
                self.assertEqual(summary["counts"]["comparison_a"].get(a), 2 if a == "pass" else 1)
                attention = summary["case_ids_requiring_attention"]
                self.assertEqual(attention["comparison_a"], ["first"] if a == "fail" else [])
                self.assertEqual(attention["comparison_b"], ["first"] if b == "fail" else [])
                self.assertEqual(attention["inconclusive"], ["first"] if b == unresolved else [])

    def test_interrupt_and_runtime_failure_preserve_completed_case(self):
        for index, failure in enumerate((KeyboardInterrupt("stop"), RuntimeError("probe failed"))):
            with self.subTest(failure=type(failure).__name__):
                self.output = self.root / f"interrupted-{index}"
                code, summary = self.run_rows([self.row(self.cases[0]), failure])
                self.assertEqual(code, 3)
                self.assertEqual(summary["status"], "incomplete")
                self.assertEqual(summary["completed_cases"], 1)
                self.assertIn(type(failure).__name__, summary["operational_error"])
                rows = (self.output / "cases.jsonl").read_text().splitlines()
                self.assertEqual([json.loads(row)["case_id"] for row in rows], ["first"])

    def test_changed_source_or_binary_invalidates_otherwise_complete_run(self):
        for kind in ("source", "binary"):
            with self.subTest(kind=kind):
                self.output = self.root / kind
                before = study.file_hash(self.probe)
                def execute(_probe, _output, case, _bank, _hash):
                    if kind == "binary" and case.id == "second":
                        self.probe.write_bytes(b"changed binary\n")
                    return self.row(case)
                snapshots = [{"source": "fixed"}, {"source": "changed" if kind == "source" else "fixed"}]
                with mock.patch.object(study, "source_snapshot", side_effect=snapshots), \
                        mock.patch.object(study, "execute_case", side_effect=execute):
                    code = study.run_study(self.probe, self.output, "pilot")
                summary = json.loads((self.output / "summary.json").read_text())
                manifest = json.loads((self.output / "manifest.json").read_text())
                self.assertEqual((code, summary["completed_cases"]), (3, 2))
                self.assertFalse(summary["source_and_binary_bytes_unchanged"])
                self.assertIn("changed", summary["operational_error"])
                self.assertEqual(manifest["probe_binary_sha256"], before)


@unittest.skipUnless(HAVE_SCIENCE, "Optional numerical assessment/NPZ checks need NumPy/SciPy")
class QuadratureNumericalAssessmentTests(unittest.TestCase):
    def setUp(self):
        import numpy as np
        self.np = np
        self.case = small_case()

    def curves(self, frames=120):
        return {name: self.np.zeros(frames) for name in
                ("quadrature_q6_volts", "continuous_q16_volts", "continuous_q32_volts", "continuous_q64_volts")}

    def test_a_b_and_refinement_are_independent_and_preserve_inputs(self):
        np = self.np
        before = study.canonical_bytes(study.reference.make_plan("pilot"))
        scenarios = ((0.0, 0.0, None, "pass", "pass"),
                     (2e-12, 0.0, None, "fail", "pass"),
                     (2e-5, 2e-5, None, "pass", "fail"),
                     (0.5, 0.5, "continuous_q16_volts", "pass", "inconclusive: reference not resolved"),
                     (0.5, 0.5, "continuous_q32_volts", "pass", "inconclusive: reference not resolved"),
                     (0.5, 0.5, "continuous_q64_volts", "pass", "inconclusive: reference not resolved"))
        for production_value, q6_value, broken, a, b in scenarios:
            with self.subTest(production=production_value, q6=q6_value, broken=broken):
                production = np.full(self.case.frames, production_value)
                curves = self.curves()
                curves["quadrature_q6_volts"][:] = q6_value
                if broken:
                    curves[broken][:] = 5e-10  # Peak passes; RMS must still reject refinement.
                originals = {name: value.copy() for name, value in curves.items()}
                result = study.assess_case(self.case, production, curves)
                self.assertEqual((result["comparison_a"]["status"], result["comparison_b"]["status"]), (a, b))
                self.assertEqual(result["comparison_a"]["reference"], "quadrature_q6_volts")
                self.assertEqual(result["comparison_b"]["reference"], "continuous_q64_volts")
                for field in ("raw_array_sha256_little_endian_float64", "raw_array_peak_absolute_volts"):
                    self.assertEqual(set(result[field]), {"production_volts", *curves})
                    self.assertNotIn("same_rate_volts", result[field])
                self.assertEqual(set(curves), set(originals))
                for name, values in originals.items():
                    np.testing.assert_array_equal(values, curves[name])
                if broken:
                    self.assertIsNone(result["comparison_b"]["metrics"])
                    self.assertIsNone(result["event_windows"][0]["metrics"]["comparison_b"])
        self.assertEqual(before, study.canonical_bytes(study.reference.make_plan("pilot")))

    def test_execute_case_retains_exact_five_arrays_even_when_unresolved(self):
        case = small_case(frames=20)
        curves = self.curves(20)
        curves["continuous_q16_volts"][19] = 1e-6
        bank = mock.Mock()
        bank.curves.return_value = curves
        raw = study.canonical_bytes(probe_document(case))
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory) / "result"
            study.reference.create_output(output)
            with mock.patch.object(study.reference, "run_probe", return_value=(raw, b"diagnostic\n")):
                row = study.execute_case(Path(sys.executable), output, case, bank, "source-identity")
            self.assertTrue(row["comparison_b"]["status"].startswith("inconclusive"))
            self.assertEqual((output / "probe" / f"{case.id}.stdout.json").read_bytes(), raw)
            self.assertEqual((output / "probe" / f"{case.id}.stderr.txt").read_bytes(), b"diagnostic\n")
            arrays = output / row["raw_arrays_path"]
            self.assertEqual(study.file_hash(arrays), row["raw_npz_file_sha256"])
            with self.np.load(arrays, allow_pickle=False) as retained:
                self.assertEqual(set(retained.files), {"production_volts", *curves})
                for name in retained.files:
                    self.assertEqual(retained[name].dtype, self.np.dtype("float64"))
                    self.assertEqual(study.array_hash(retained[name]),
                                     row["raw_array_sha256_little_endian_float64"][name])
                self.np.testing.assert_array_equal(retained["continuous_q16_volts"], curves["continuous_q16_volts"])

    def test_execute_case_retains_malformed_stdout_and_probe_failure_streams(self):
        case = small_case(frames=20)
        for index, behavior in enumerate((dict(return_value=(b"invalid json\n", b"bad data\n")),
                dict(side_effect=study.reference.ProbeFailure("failed probe", b"partial\n", b"failure\n")))):
            with self.subTest(index=index), tempfile.TemporaryDirectory() as directory:
                output = Path(directory) / "result"
                study.reference.create_output(output)
                with mock.patch.object(study.reference, "run_probe", **behavior):
                    with self.assertRaises((ValueError, study.reference.ProbeFailure)):
                        study.execute_case(Path(sys.executable), output, case, mock.Mock(), "source")
                self.assertEqual((output / "probe" / f"{case.id}.stdin.txt").read_bytes(),
                                 study.reference.probe_input(case))
                self.assertTrue((output / "probe" / f"{case.id}.stdout.json").read_bytes())
                self.assertTrue((output / "probe" / f"{case.id}.stderr.txt").read_bytes())
                self.assertEqual(list((output / "raw").iterdir()), [])


if __name__ == "__main__":
    unittest.main()
