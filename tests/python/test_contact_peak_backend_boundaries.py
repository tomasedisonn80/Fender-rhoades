"""Optional timing transport failures must preserve independent peak values."""
import copy
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch


ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools"))
import contact_peak_oracles as oracle


HAMMER_KEYS = ("hammer_compression", "hammer_force")


def successful_worker_payload():
    refinements = {}
    for key, value in zip(HAMMER_KEYS, (0.00034505973796, 0.00017769056226)):
        refinements[key] = []
        for index, (epsabs, epsrel) in enumerate(oracle.QUAD_LEVELS):
            level = {"epsabs": epsabs, "epsrel": epsrel, "limit": 128,
                     "value_s": value, "estimated_absolute_error_s": 1e-17,
                     "evaluations": 21, "reported_evaluations": 21,
                     "subintervals": 1, "convergence_message": ""}
            if index:
                level.update(successive_difference_s=0.0,
                             required_maximum_difference_s=0.1 * (1e-7 + 1e-4 * value))
            refinements[key].append(level)
    return {"status": "resolved_under_declared_refinement", "backend": "SciPy",
            "backend_version": "controlled-test-worker", "evaluations": 126,
            "worker_elapsed_s": 0.01, "refinements": refinements,
            "error_interpretation": "Test estimates are not certificates."}


class ContactPeakBackendBoundaryTests(unittest.TestCase):
    def assert_values_preserved(self, result):
        baseline = oracle.reference_peaks("damped_impact")
        for key in HAMMER_KEYS:
            with self.subTest(peak=key):
                record = result["peaks"][key]
                self.assertEqual(record["value_interval"], baseline["peaks"][key]["value_interval"])
                self.assertEqual(record["value_status"], "resolved_under_declared_refinement")
                self.assertEqual(record["time_status"], "inconclusive_reference")
                self.assertEqual(record["maximizer_intervals_s"], [])
                self.assertEqual(record["details"]["root_refinements"],
                                 baseline["peaks"][key]["details"]["root_refinements"])
        json.dumps(result, allow_nan=False)

    def test_process_launch_failure_preserves_values(self):
        with patch.object(oracle.subprocess, "run",
                          side_effect=OSError("controlled process launch failure")):
            result = oracle.reference_peaks("damped_impact", damped_time=True)
        self.assert_values_preserved(result)
        self.assertIn("launch failure", result["peaks"][HAMMER_KEYS[0]]["details"]["timing"]["reason"])

    def test_real_nonzero_garbled_and_undecodable_workers_preserve_values(self):
        workers = {
            "nonzero_unicode": "import sys\nsys.stderr.buffer.write(('SciPy 加载失败 Ω\\n' * 400).encode('utf-8'))\nsys.exit(23)\n",
            "garbled_stdout": "print('not JSON')\n",
            "undecodable_stderr": "import sys\nsys.stderr.buffer.write(bytes([255]))\nsys.exit(24)\n",
        }
        with tempfile.TemporaryDirectory() as directory:
            for name, source in workers.items():
                with self.subTest(worker=name):
                    worker = Path(directory) / (name + ".py")
                    worker.write_text(source, encoding="utf-8")
                    with patch.object(oracle, "__file__", str(worker)):
                        result = oracle.reference_peaks("damped_impact", damped_time=True)
                    self.assert_values_preserved(result)
                    timing = result["peaks"][HAMMER_KEYS[0]]["details"]["timing"]
                    self.assertTrue(timing["reason"])
                    if name == "nonzero_unicode":
                        self.assertEqual(timing["returncode"], 23)
                        self.assertIn("SciPy 加载失败 Ω", timing["stderr"])

    def test_worker_utf8_survives_ascii_parent_locale(self):
        # Run the real pipe boundary with Python UTF-8 mode and locale coercion
        # disabled. POSIX C locales use ASCII; Windows retains its host locale.
        source = "import sys\nsys.stderr.buffer.write(bytes.fromhex('e58aa0'))\nsys.exit(23)\n"
        with tempfile.TemporaryDirectory() as directory:
            worker = Path(directory) / "worker.py"
            worker.write_text(source, encoding="ascii")
            script = (
                "import json,sys; sys.path.insert(0,sys.argv[1]); "
                "import contact_peak_oracles as o; o.__file__=sys.argv[2]; "
                "r=o.reference_peaks('damped_impact',damped_time=True); "
                "print(json.dumps(r,ensure_ascii=True,allow_nan=False))"
            )
            environment = dict(os.environ, LC_ALL="C", LANG="C", PYTHONUTF8="0",
                               PYTHONCOERCECLOCALE="0", PYTHONIOENCODING="ascii")
            process = subprocess.run(
                [sys.executable, "-c", script, str(ROOT / "tools"), str(worker)],
                capture_output=True, env=environment, timeout=15, check=False,
            )
            self.assertEqual(process.returncode, 0, process.stderr)
            result = json.loads(process.stdout)
            self.assert_values_preserved(result)
            timing = result["peaks"][HAMMER_KEYS[0]]["details"]["timing"]
            self.assertEqual(timing["returncode"], 23)
            self.assertEqual(timing["stderr"], "\u52a0")

    def test_malformed_earlier_refinement_does_not_claim_resolved_timing(self):
        original = successful_worker_payload()
        for invalid_level in (None, {**original["refinements"][HAMMER_KEYS[0]][0], "value_s": -1.0}):
            with self.subTest(level=invalid_level):
                payload = copy.deepcopy(original)
                payload["refinements"][HAMMER_KEYS[0]][0] = invalid_level
                completed = subprocess.CompletedProcess([], 0, json.dumps(payload), "")
                with patch.object(oracle.subprocess, "run", return_value=completed):
                    result = oracle.reference_peaks("damped_impact", damped_time=True)
                self.assert_values_preserved(result)

    def test_unrepresentable_worker_numbers_preserve_values_and_json_safety(self):
        earlier_overflow = successful_worker_payload()
        earlier_overflow["refinements"][HAMMER_KEYS[0]][0]["value_s"] = "OVERFLOW"
        final_integer = successful_worker_payload()
        final_integer["refinements"][HAMMER_KEYS[0]][-1]["value_s"] = 10 ** 400
        cases = {
            "earlier_float_overflow": json.dumps(earlier_overflow).replace('"OVERFLOW"', "1e999"),
            "final_integer_overflow": json.dumps(final_integer),
        }
        for name, raw in cases.items():
            with self.subTest(case=name):
                completed = subprocess.CompletedProcess([], 0, raw, "")
                with patch.object(oracle.subprocess, "run", return_value=completed):
                    result = oracle.reference_peaks("damped_impact", damped_time=True)
                self.assert_values_preserved(result)


if __name__ == "__main__":
    unittest.main()
