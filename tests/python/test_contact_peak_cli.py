"""CLI retention regressions; constructed payloads are not physics evidence."""
from __future__ import annotations

import hashlib
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

from test_contact_peak_schema import ROOT, encode, payload


CLI = ROOT / "tools" / "validate_contact_peaks.py"
PEAKS = ("hammer_compression", "hammer_force", "damper_compression", "damper_force")


def forced_clamp_payload(*, peaks=True):
    """Transport fixture with the declared 1e-4 m initial compression maximum."""
    document = payload("forced_clamp", peaks=peaks)
    run = document["runs"][0]
    run["samples"][0]["hammer_gap_m"] = 1e-4
    run["diagnostics"]["sampled_maxima"]["hammer_compression_m"] = 1e-4
    if peaks:
        run["diagnostics"]["located_peak_candidates"]["hammer_compression"]["value_m"] = 1e-4
    return document


class ContactPeakCliTests(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory(prefix="contact peak cli ")
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name)
        self.attempts = 0

    def write_candidate(self, name, raw):
        path = self.root / name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(raw)
        return path

    def cli(self, candidates, *, output=None, extra=()):
        self.attempts += 1
        output = output or self.root / ("evidence-" + str(self.attempts))
        command = [sys.executable, str(CLI), "--output", str(output)]
        for path in candidates:
            command.extend(("--candidate", str(path)))
        command.extend(extra)
        completed = subprocess.run(command, cwd=ROOT, capture_output=True, text=True,
                                   check=False, timeout=30)
        return completed, output

    def inspect_attempt(self, candidates, completed, output, expected_code):
        """Read each retained case/result, including failures, before asserting."""
        self.assertEqual(completed.returncode, expected_code, completed.stdout + completed.stderr)
        self.assertEqual(completed.stderr, "")
        summary = json.loads(completed.stdout)
        plan_bytes = (output / "plan.json").read_bytes()
        plan = json.loads(plan_bytes)
        manifest = json.loads((output / "manifest.json").read_bytes())
        expected_status = {0: "pass", 1: "fail", 2: "incomplete", 3: "operational_failure"}[expected_code]
        for report in (summary, manifest):
            self.assertEqual(report["exit_code"], expected_code)
            self.assertEqual(report["status"], expected_status)
        for report in (summary, plan, manifest):
            self.assertEqual(report["phy002_acceptance"], "incomplete")
        self.assertEqual(summary["cases"], len(candidates))
        self.assertEqual(summary["output"], str(output))
        self.assertEqual(plan["candidates"], list(map(str, candidates)))
        self.assertEqual(plan["exit_precedence"], [3, 1, 2, 0])
        self.assertEqual(manifest["unattempted_inputs"], [])
        self.assertEqual(len(manifest["cases"]), len(candidates))
        self.assertEqual(manifest["plan_sha256"], hashlib.sha256(plan_bytes).hexdigest())
        self.assertTrue(manifest["source_unchanged"])
        self.assertEqual(manifest["source_sha256_before"], manifest["source_sha256_after"])
        for name, digest in manifest["source_sha256_after"].items():
            self.assertEqual(hashlib.sha256((ROOT / name).read_bytes()).hexdigest(), digest)
        self.assertIn("python", manifest["versions"])

        comparisons = []
        retained_bytes = 0
        for index, (path, record) in enumerate(zip(candidates, manifest["cases"]), 1):
            stem = f"candidate-{index:04d}"
            self.assertEqual(record["index"], index)
            self.assertEqual(record["input_path"], str(path))
            self.assertEqual(json.loads((output / (stem + ".record.json")).read_bytes()), record)
            if "raw_file" in record:
                self.assertEqual(record["raw_file"], stem + ".raw")
                retained = (output / record["raw_file"]).read_bytes()
                self.assertEqual(retained, path.read_bytes())
                self.assertTrue(record["raw_complete"])
                self.assertEqual(record["retained_bytes"], len(retained))
                digest = hashlib.sha256(retained).hexdigest()
                self.assertEqual(record["retained_sha256"], digest)
                if "raw_sha256" in record:
                    self.assertEqual(record["raw_sha256"], digest)
                retained_bytes += len(retained)
            else:
                self.assertFalse((output / (stem + ".raw")).exists())
            if "comparison_file" in record:
                self.assertEqual(record["comparison_file"], stem + ".comparison.json")
                comparison = json.loads((output / record["comparison_file"]).read_bytes())
                self.assertEqual(comparison["candidate_sha256"], record["raw_sha256"])
                self.assertEqual(comparison["exit_code"], record["exit_code"])
                self.assertEqual(comparison["settings"], record["settings"])
                self.assertEqual(comparison["phy002_acceptance"], "incomplete")
                for peak in comparison["peaks"].values():
                    self.assertEqual(peak["coverage"]["general_continuous_extrema"], "incomplete")
            else:
                self.assertFalse((output / (stem + ".comparison.json")).exists())
                comparison = None
            comparisons.append(comparison)
        self.assertEqual(manifest["retained_candidate_bytes"], retained_bytes)
        return manifest, comparisons, plan

    def test_valid_forced_clamp_retains_reference_and_incomplete_acceptance(self):
        document = forced_clamp_payload()
        candidate = self.write_candidate("valid.json", encode(document))
        completed, output = self.cli([candidate])
        manifest, comparisons, _ = self.inspect_attempt([candidate], completed, output, 0)
        self.assertEqual(manifest["operational_errors"], [])
        self.assertEqual(manifest["cases"][0]["status"], "pass")
        comparison = comparisons[0]
        self.assertEqual(comparison["availability"], "available")
        self.assertEqual(comparison["validity"], "pass")
        reference = json.loads((output / "reference-forced_clamp.json").read_bytes())
        canonical_definition = json.dumps(reference["definition"], sort_keys=True,
                                          separators=(",", ":"), allow_nan=False).encode("utf-8")
        self.assertEqual(reference["definition_sha256"], hashlib.sha256(canonical_definition).hexdigest())
        self.assertEqual(comparison["definition_sha256"], reference["definition_sha256"])
        self.assertEqual(reference["peaks"]["hammer_compression"]["value_interval"], [1e-4, 1e-4])
        self.assertEqual(reference["peaks"]["hammer_force"]["kind"], "plateau")
        for name in PEAKS:
            peak = comparison["peaks"][name]
            self.assertEqual(peak["candidate"], document["runs"][0]["diagnostics"]["located_peak_candidates"][name])
            self.assertEqual(peak["reference"], reference["peaks"][name])
            self.assertEqual(peak["value_status"], "pass")
            self.assertEqual(peak["time_status"], "pass")
            self.assertEqual(peak["observation_consistency_status"], "pass")

    def test_existing_output_directory_is_refused_without_changes(self):
        candidate = self.write_candidate("valid.json", encode(forced_clamp_payload()))
        for occupied in (False, True):
            with self.subTest(occupied=occupied):
                output = self.root / ("existing-" + str(occupied))
                output.mkdir()
                if occupied:
                    (output / "manifest.json").write_bytes(b"prior evidence\x00\xff\n")
                    (output / "keep.raw").write_bytes(b"must not overwrite")
                before = {path.name: path.read_bytes() for path in output.iterdir()}
                completed, _ = self.cli([candidate], output=output)
                self.assertEqual(completed.returncode, 3)
                self.assertEqual(completed.stdout, "")
                self.assertIn("Cannot create exclusive output", completed.stderr)
                self.assertEqual({path.name: path.read_bytes() for path in output.iterdir()}, before)

    def test_valid_and_malformed_raw_bytes_and_hashes_are_retained(self):
        raw_files = [b" \t\r\n" + encode(forced_clamp_payload()) + b"\r\n",
                     b'{"schema_version":1,"runs":[\xff\n',
                     b'{"schema_version":1,"runs":[\n']
        candidates = [self.write_candidate(f"input-{index}.json", raw)
                      for index, raw in enumerate(raw_files)]
        completed, output = self.cli(candidates)
        manifest, comparisons, _ = self.inspect_attempt(candidates, completed, output, 1)
        self.assertEqual([record["status"] for record in manifest["cases"]],
                         ["pass", "malformed_candidate", "malformed_candidate"])
        self.assertIsNotNone(comparisons[0])
        self.assertEqual(comparisons[1:], [None, None])
        for record in manifest["cases"][1:]:
            self.assertEqual(record["exit_code"], 1)
            self.assertIn("CandidateFormatError", record["error"])

    def test_mixed_cases_use_operational_failure_incomplete_pass_precedence(self):
        failed = forced_clamp_payload()
        failed["runs"][0]["diagnostics"]["located_peak_candidates"]["hammer_compression"]["value_m"] = 0.001
        unsupported = forced_clamp_payload()
        unsupported["runs"][0]["fixture"] = "outside_analytic_scope"
        documents = {"pass": forced_clamp_payload(), "fail": failed,
                     "missing": forced_clamp_payload(peaks=False), "unsupported": unsupported}
        paths = {name: self.write_candidate(name + ".json", encode(document))
                 for name, document in documents.items()}
        paths["absent"] = self.root / "absent.json"
        case_codes = {"pass": 0, "fail": 1, "missing": 2, "unsupported": 2, "absent": 3}
        cases = [(('missing', 'unsupported', 'pass'), 2),
                 (('pass', 'missing', 'fail', 'unsupported'), 1),
                 (('absent', 'pass', 'fail', 'missing', 'unsupported'), 3),
                 (('unsupported', 'missing', 'fail', 'pass', 'absent'), 3)]
        for names, expected in cases:
            with self.subTest(order=names, exit_code=expected):
                candidates = [paths[name] for name in names]
                completed, output = self.cli(candidates)
                manifest, comparisons, _ = self.inspect_attempt(candidates, completed, output, expected)
                self.assertEqual([record["exit_code"] for record in manifest["cases"]],
                                 [case_codes[name] for name in names])
                for name, record, comparison in zip(names, manifest["cases"], comparisons):
                    if name == "absent":
                        self.assertEqual(record["status"], "operational_failure")
                        self.assertIn("FileNotFoundError", record["error"])
                        self.assertIsNone(comparison)
                    elif name in {"missing", "unsupported"}:
                        self.assertEqual(record["status"], "incomplete")
                        self.assertEqual(comparison["availability"],
                                         "not_available" if name == "missing" else "unsupported_fixture")
                        self.assertEqual(comparison["peaks"], {})
                    elif name == "fail":
                        self.assertEqual(record["status"], "fail")
                        self.assertEqual(comparison["peaks"]["hammer_compression"]["value_status"], "fail")

    def test_failed_producer_partial_data_remains_failure_with_or_without_peaks(self):
        candidates = []
        for peaks in (True, False):
            document = forced_clamp_payload(peaks=peaks)
            run = document["runs"][0]
            run.update(success=False, failure_code=7)
            run["samples"][-1]["time_s"] *= 0.5
            for setting in ("minimum_accepted_step_s", "maximum_accepted_step_s"):
                run["diagnostics"][setting] *= 0.5
            candidates.append(self.write_candidate(f"failed-producer-{peaks}.json", encode(document)))
        completed, output = self.cli(candidates)
        manifest, comparisons, _ = self.inspect_attempt(candidates, completed, output, 1)
        self.assertEqual([record["status"] for record in manifest["cases"]], ["fail", "fail"])
        self.assertEqual([comparison["availability"] for comparison in comparisons], ["available", "not_available"])
        for comparison in comparisons:
            self.assertEqual(comparison["validity"], "producer_failure")
            self.assertEqual(comparison["failure_code"], 7)
            self.assertEqual(comparison["exit_code"], 1)
            self.assertEqual(comparison["peaks"], {})
            self.assertIn("partial trajectory is not success", comparison["reason"])
        self.assertEqual(list(output.glob("reference-*.json")), [])

    def test_declared_producer_is_hashed_and_never_executed(self):
        marker = self.root / "producer-was-executed"
        producer = self.root / "declared producer.py"
        producer_bytes = (f"#!{sys.executable}\nfrom pathlib import Path\n"
                          f"Path({str(marker)!r}).write_bytes(b'unexpected execution\\n')\n").encode("utf-8")
        producer.write_bytes(producer_bytes)
        producer.chmod(0o700)
        candidate = self.write_candidate("valid.json", encode(forced_clamp_payload()))
        completed, output = self.cli([candidate], extra=("--producer-binary", str(producer)))
        manifest, _, plan = self.inspect_attempt([candidate], completed, output, 0)
        self.assertFalse(marker.exists(), "the declared producer file must never be executed")
        self.assertEqual(producer.read_bytes(), producer_bytes)
        self.assertEqual(plan["producer_binary"], str(producer))
        self.assertEqual(manifest["declared_producer"], {
            "declared_path": str(producer), "bytes": len(producer_bytes),
            "sha256": hashlib.sha256(producer_bytes).hexdigest(),
            "executed": False, "candidate_production_attested": False,
        })

    def test_missing_declared_producer_keeps_candidate_results_and_exit_three(self):
        candidate = self.write_candidate("valid.json", encode(forced_clamp_payload()))
        producer = self.root / "missing-producer"
        completed, output = self.cli([candidate], extra=("--producer-binary", str(producer)))
        manifest, comparisons, plan = self.inspect_attempt([candidate], completed, output, 3)
        self.assertEqual(plan["producer_binary"], str(producer))
        self.assertEqual(manifest["cases"][0]["status"], "pass")
        self.assertEqual(comparisons[0]["exit_code"], 0)
        self.assertEqual(len(manifest["operational_errors"]), 1)
        self.assertIn("producer identity: FileNotFoundError", manifest["operational_errors"][0])
        self.assertNotIn("declared_producer", manifest)

    def test_numbered_storage_preserves_candidates_with_identical_basenames(self):
        failed = forced_clamp_payload()
        failed["runs"][0]["diagnostics"]["located_peak_candidates"]["hammer_compression"]["value_m"] = 0.001
        candidates = [self.write_candidate("left/candidate.json", encode(forced_clamp_payload())),
                      self.write_candidate("right/candidate.json", encode(failed))]
        self.assertEqual(candidates[0].name, candidates[1].name)
        completed, output = self.cli(candidates)
        manifest, comparisons, _ = self.inspect_attempt(candidates, completed, output, 1)
        self.assertEqual([record["raw_file"] for record in manifest["cases"]],
                         ["candidate-0001.raw", "candidate-0002.raw"])
        self.assertEqual([record["status"] for record in manifest["cases"]], ["pass", "fail"])
        self.assertNotEqual(comparisons[0]["candidate_sha256"], comparisons[1]["candidate_sha256"])
        self.assertEqual(len(list(output.glob("*.raw"))), 2)
        self.assertEqual(len(list(output.glob("*.comparison.json"))), 2)
        self.assertEqual(len(list(output.glob("reference-*.json"))), 1)


if __name__ == "__main__":
    unittest.main()
