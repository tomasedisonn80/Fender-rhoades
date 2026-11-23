"""Explicit peak-sweep provenance and schema checks, not extrema acceptance."""
from __future__ import annotations

from contextlib import redirect_stderr, redirect_stdout
import hashlib
import io
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch

from test_contact_sweep import ROOT, payload, sweep


def peaks():
    result = {
        "coverage": "uncertified_sign_change_search",
        "evaluation_method": "fixed_interval_anchor_replay_two_rk4_half_steps_or_certified_free_motion",
    }
    for contact in ("hammer", "damper"):
        for quantity, unit in (("compression", "value_m"), ("force", "value_n")):
            result[contact + "_" + quantity] = {
                unit: 0.0, "time_s": 0.0, "bracket_lo_s": 0.0, "bracket_hi_s": 0.0,
                "origin": "sample" if contact == "hammer" else "disabled"}
    return result


class ContactPeakSweepTests(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory(prefix="rhodes peak sweep ")
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name)
        self.executable = self.root / "synthetic candidate.py"
        self.executable.write_text("# synthetic transport fixture\n", encoding="utf-8")
        self.output = self.root / "outputs"
        self.item = sweep.plan(["conservative_impact"], locate_peak_candidates=True)[0]

    def cli(self, *extra):
        stdout, stderr = io.StringIO(), io.StringIO()
        argv = ["run_contact_sweep.py", "--executable", str(self.executable),
                "--output-dir", str(self.output), "--fixture", "conservative_impact", *extra]
        with patch.object(sys, "argv", argv), redirect_stdout(stdout), redirect_stderr(stderr):
            status = sweep.main()
        return status, stdout.getvalue(), stderr.getvalue()

    def child(self, argv, *, stdout, stderr, **unused):
        options = dict(zip(argv[1::2], argv[2::2]))
        item = {"fixture": options["--fixture"],
                "maximum_step_s": float(options["--max-step"]),
                "event_tolerance_s": float(options["--event-tolerance"]),
                "tolerance_scale": float(options["--tolerance-scale"]),
                "output_step_s": float(options["--output-step"])}
        document = payload(item)
        document["runs"][0]["diagnostics"]["located_peak_candidates"] = peaks()
        stdout.write(json.dumps(document).encode())
        stderr.write(b"synthetic peak diagnostic\n")
        return subprocess.CompletedProcess(argv, 0)

    def parse(self, document, item=None, duration=0.002):
        path = self.root / "candidate.json"
        path.write_text(json.dumps(document), encoding="utf-8")
        return sweep.candidate(path, item or self.item, duration)

    def document(self):
        document = payload(self.item)
        document["runs"][0]["diagnostics"]["located_peak_candidates"] = peaks()
        return document

    def test_default_plan_matches_archived_inputs_and_explicit_false(self):
        historical = json.loads((ROOT / "docs/validation/contact-generation-manifest-20261123.json").read_text())
        names = sorted(sweep.fixtures())
        default = sweep.plan(names)
        self.assertEqual(default, historical["metadata"]["runs"])
        self.assertEqual(default, sweep.plan(names, locate_peak_candidates=False))
        self.assertTrue(all("locate_peak_candidates" not in item for item in default))
        for depth, expected_count in ((0, 208), (1, 338), (2, 364)):
            requested = sweep.plan(names, depth, locate_peak_candidates=True)
            self.assertEqual(len(requested), expected_count)
            self.assertTrue(all(item.pop("locate_peak_candidates") is True for item in requested))
            self.assertEqual(requested, sweep.plan(names, depth))

    def test_plan_option_is_explicit_without_dependencies_or_output(self):
        self.executable.unlink()
        status, default, errors = self.cli("--plan")
        self.assertEqual((status, errors), (0, ""))
        status, requested, errors = self.cli("--plan", "--locate-peak-candidates")
        self.assertEqual((status, errors), (0, ""))
        before, after = json.loads(default), json.loads(requested)
        metadata = after.pop("peak_candidates")
        self.assertIs(metadata["requested"], True)
        self.assertEqual(metadata["coverage"], "uncertified_sign_change_search")
        self.assertIn("independent continuous-extrema gate remains open", metadata["acceptance"])
        for item in after["runs"]:
            self.assertIs(item.pop("locate_peak_candidates"), True)
        self.assertEqual(before, after)
        self.assertFalse(self.output.exists())

    def test_success_binds_request_raw_streams_and_unchanged_audit_invocation(self):
        with patch.object(sweep.subprocess, "run", side_effect=self.child):
            status, stdout, _ = self.cli("--locate-peak-candidates")
        self.assertEqual(status, 0, stdout)
        manifest = json.loads((self.output / "manifest.json").read_text())
        aggregate = json.loads((self.output / "candidates.json").read_text())
        self.assertEqual(manifest["acceptance"], "not evaluated")
        self.assertEqual(manifest["status"], "candidate_generation_complete")
        self.assertIs(manifest["metadata"]["peak_candidates"]["requested"], True)
        self.assertIn("docs/specs/CONTACT_PEAK_DIAGNOSTICS_20261123.md", manifest["source_sha256"])
        self.assertEqual(manifest["input_metadata_sha256"],
                         hashlib.sha256(sweep.canonical(manifest["metadata"])).hexdigest())
        self.assertNotIn("--locate-peak-candidates", manifest["audit_argv"])
        self.assertTrue(aggregate["generation_only"])
        self.assertEqual(len(aggregate["runs"]), 8)
        for record, run in zip(manifest["results"], aggregate["runs"]):
            self.assertIs(record["locate_peak_candidates"], True)
            self.assertEqual(record["argv"].count("--locate-peak-candidates"), 1)
            self.assertEqual(run["diagnostics"]["located_peak_candidates"], peaks())
            for stream in ("stdout", "stderr"):
                self.assertEqual(sweep.sha256(self.output / record[stream]), record[stream + "_sha256"])

    def test_default_command_and_parsing_do_not_infer_peak_request(self):
        self.output.mkdir()
        item = sweep.plan(["conservative_impact"])[0]
        with patch.object(sweep.subprocess, "run", side_effect=self.child):
            record = sweep.execute(self.executable, self.output, item, 10.0, 0.002)
        self.assertEqual(record["status"], "candidate_generated")
        self.assertEqual(record["argv"], [str(self.executable), "--fixture", "conservative_impact",
                         "--max-step", "2e-05", "--event-tolerance", "2.5e-09",
                         "--tolerance-scale", "1.0", "--output-step", "2e-05"])
        document = payload(item)
        document["runs"][0]["diagnostics"]["located_peak_candidates"] = {"coverage": "unrequested"}
        self.assertEqual(self.parse(document, item), document["runs"][0])
        with self.assertRaisesRegex(ValueError, "requested peak candidates"):
            self.parse(document)

    def test_missing_requested_peaks_fail_and_preserve_all_child_outputs(self):
        def missing(argv, *, stdout, stderr, **unused):
            options = dict(zip(argv[1::2], argv[2::2]))
            item = {"fixture": options["--fixture"],
                    "maximum_step_s": float(options["--max-step"]),
                    "event_tolerance_s": float(options["--event-tolerance"]),
                    "tolerance_scale": float(options["--tolerance-scale"]),
                    "output_step_s": float(options["--output-step"])}
            stdout.write(json.dumps(payload(item)).encode())
            stderr.write(b"diagnostic from incompatible candidate\n")
            return subprocess.CompletedProcess(argv, 0)
        with patch.object(sweep.subprocess, "run", side_effect=missing):
            status, _, _ = self.cli("--locate-peak-candidates")
        self.assertEqual(status, 1)
        self.assertFalse((self.output / "candidates.json").exists())
        manifest = json.loads((self.output / "manifest.json").read_text())
        self.assertEqual(manifest["status"], "failed")
        for record in manifest["results"]:
            self.assertEqual(record["exit_code"], 0)
            self.assertEqual(record["status"], "failed")
            self.assertIn("requested peak candidates are missing", record["errors"][0])
            self.assertEqual((self.output / record["stderr"]).read_bytes(), b"diagnostic from incompatible candidate\n")
            self.assertEqual(sweep.sha256(self.output / record["stdout"]), record["stdout_sha256"])

    def test_wrong_coverage_method_containers_and_missing_records_are_rejected(self):
        variants = [None, [], {}, {**peaks(), "coverage": "certified"},
                    {**peaks(), "evaluation_method": "sampled_only"},
                    {**peaks(), "extra_unversioned": 0}]
        for name in ("hammer_compression", "hammer_force", "damper_compression", "damper_force"):
            variant = peaks()
            del variant[name]
            variants.append(variant)
            variants.append({**peaks(), name: []})
        for index, variant in enumerate(variants):
            with self.subTest(case=index):
                document = self.document()
                document["runs"][0]["diagnostics"]["located_peak_candidates"] = variant
                with self.assertRaisesRegex(ValueError, "peak candidate"):
                    self.parse(document)

    def test_invalid_units_numbers_brackets_origins_and_disabled_contacts_reject(self):
        edits = [("value_m", value) for value in (True, "0", -1, float("nan"), float("inf"))]
        edits += [("time_s", -1), ("time_s", 0.003), ("time_s", True),
                  ("bracket_lo_s", 1e-6), ("bracket_hi_s", -1e-6),
                  ("bracket_hi_s", 1e-6), ("origin", "maximum"),
                  ("origin", []), ("origin", "disabled"), ("origin", "action_after")]
        for field, value in edits:
            with self.subTest(field=field, value=value):
                document = self.document()
                document["runs"][0]["diagnostics"]["located_peak_candidates"]["hammer_compression"][field] = value
                with self.assertRaises(ValueError):
                    self.parse(document)
        document = self.document()
        record = document["runs"][0]["diagnostics"]["located_peak_candidates"]["hammer_compression"]
        record["value_n"] = record.pop("value_m")
        with self.assertRaisesRegex(ValueError, "invalid SI fields"):
            self.parse(document)
        for field, value in (("value_n", 1), ("origin", "sample")):
            document = self.document()
            document["runs"][0]["diagnostics"]["located_peak_candidates"]["damper_force"][field] = value
            with self.assertRaises(ValueError):
                self.parse(document)

    def test_stationary_bracket_limit_is_applied_without_widening_it(self):
        document = self.document()
        peak = document["runs"][0]["diagnostics"]["located_peak_candidates"]["hammer_force"]
        peak.update(value_n=1.0, time_s=1e-9, bracket_lo_s=0.0,
                    bracket_hi_s=self.item["event_tolerance_s"], origin="stationary_candidate")
        self.parse(document)
        peak["bracket_hi_s"] *= 1.01
        with self.assertRaisesRegex(ValueError, "bracket exceeds event tolerance"):
            self.parse(document)

    def test_action_origins_require_a_declared_action_and_enabled_damper_records(self):
        item = sweep.plan(["evolved_relaunch"], locate_peak_candidates=True)[0]
        document = payload(item, duration=0.020)
        records = peaks()
        document["runs"][0]["diagnostics"]["located_peak_candidates"] = records
        for origin in ("action_before", "action_after"):
            records["hammer_force"].update(time_s=0.0037, bracket_lo_s=0.0037,
                                           bracket_hi_s=0.0037, origin=origin)
            self.parse(document, item, 0.020)
        records["hammer_force"].update(time_s=0.00371, bracket_lo_s=0.00371, bracket_hi_s=0.00371)
        with self.assertRaisesRegex(ValueError, "declared action time"):
            self.parse(document, item, 0.020)
        item = sweep.plan(["moving_damper"], locate_peak_candidates=True)[0]
        document = payload(item, duration=0.024)
        document["runs"][0]["diagnostics"]["located_peak_candidates"] = peaks()
        with self.assertRaisesRegex(ValueError, "enabled contact"):
            self.parse(document, item, 0.024)
        for name in ("damper_compression", "damper_force"):
            document["runs"][0]["diagnostics"]["located_peak_candidates"][name]["origin"] = "sample"
        self.parse(document, item, 0.024)

    def test_flag_rejects_values_and_nonboolean_programmatic_requests(self):
        for value in (0, 1, None, "true", [], {}):
            with self.subTest(value=value), self.assertRaisesRegex(ValueError, "must be a boolean"):
                sweep.plan(["conservative_impact"], locate_peak_candidates=value)
        with patch.object(sweep.subprocess, "run") as run, self.assertRaises(SystemExit) as failure:
            self.cli("--locate-peak-candidates", "false")
        self.assertEqual(failure.exception.code, 2)
        run.assert_not_called()
        self.assertFalse(self.output.exists())

    def test_nonzero_child_exit_retains_valid_optional_diagnostics_without_success(self):
        self.output.mkdir()
        def failed(argv, **kwargs):
            self.child(argv, **kwargs)
            return subprocess.CompletedProcess(argv, 7)
        with patch.object(sweep.subprocess, "run", side_effect=failed):
            record = sweep.execute(self.executable, self.output, self.item, 10.0, 0.002)
        self.assertEqual(record["status"], "failed")
        self.assertEqual(record["exit_code"], 7)
        self.assertEqual(record["errors"], ["candidate exited 7"])
        self.assertEqual(record["sample_count"], 2)
        self.assertEqual(sweep.sha256(self.output / record["stdout"]), record["stdout_sha256"])


if __name__ == "__main__":
    unittest.main()
