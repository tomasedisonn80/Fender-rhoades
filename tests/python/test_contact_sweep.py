"""Candidate-generation safety tests; synthetic payloads are not physics evidence."""
from __future__ import annotations

from contextlib import redirect_stderr, redirect_stdout
import copy
import hashlib
import importlib.util
import io
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch


ROOT = Path(__file__).resolve().parents[2]
SPEC = importlib.util.spec_from_file_location(
    "rhodes_contact_sweep_test_target", ROOT / "tools/run_contact_sweep.py")
if SPEC is None or SPEC.loader is None:
    raise RuntimeError("Cannot load contact sweep runner")
sweep = importlib.util.module_from_spec(SPEC)
with patch.object(sys, "path", [str(ROOT / "tools"), *sys.path]):
    SPEC.loader.exec_module(sweep)
REAL_RUN = subprocess.run


def payload(item, duration=0.002):
    """Minimum declared export shape, with deliberately nonphysical zero data."""
    def sample(time):
        return {"time_s": time, "q_m": [0.0, 0.0], "v_m_per_s": [0.0, 0.0],
                "hammer_position_m": 0.0, "hammer_velocity_m_per_s": 0.0,
                "hammer_force_n": 0.0, "damper_force_n": 0.0,
                "hammer_impulse_ns": 0.0, "damper_impulse_ns": 0.0,
                "modal_loss_j": 0.0, "hammer_loss_j": 0.0, "damper_loss_j": 0.0,
                "actuator_work_j": 0.0, "launch_work_j": 0.0, "energy_j": 0.0}
    return {"schema_version": 1, "model": "synthetic-contact-p1", "runs": [{
        "fixture": item["fixture"], "success": True, "failure_code": 0,
        "maximum_step_s": item["maximum_step_s"],
        "event_tolerance_s": item["event_tolerance_s"],
        "output_step_s": item["output_step_s"],
        "relative_tolerance": 1e-8 * item["tolerance_scale"],
        "absolute_tolerance": 1e-12 * item["tolerance_scale"],
        "samples": [sample(0.0), sample(duration)], "events": [], "diagnostics": {}}]}


class ContactSweepTests(unittest.TestCase):
    def setUp(self):
        directory = tempfile.TemporaryDirectory(prefix="rhodes sweep ")
        self.addCleanup(directory.cleanup)
        self.root = Path(directory.name)
        self.executable = self.root / "fake candidate.py"
        self.executable.write_text("# Synthetic test candidate\n", encoding="utf-8")
        self.output = self.root / "outputs"
        self.item = sweep.plan(["conservative_impact"])[0]

    def cli(self, *arguments):
        output, errors = io.StringIO(), io.StringIO()
        args = ["run_contact_sweep.py", "--executable", str(self.executable),
                "--output-dir", str(self.output), "--fixture", "conservative_impact",
                *arguments]
        with patch.object(sys, "argv", args), redirect_stdout(output), redirect_stderr(errors):
            result = sweep.main()
        return result, output.getvalue(), errors.getvalue()

    def valid_child(self, argv, *, stdout, stderr, timeout, check):
        self.assertEqual(argv[0], str(self.executable.resolve()))
        self.assertFalse(check)
        self.assertGreater(timeout, 0)
        options = dict(zip(argv[1::2], argv[2::2]))
        item = {"fixture": options["--fixture"],
                "maximum_step_s": float(options["--max-step"]),
                "event_tolerance_s": float(options["--event-tolerance"]),
                "tolerance_scale": float(options["--tolerance-scale"]),
                "output_step_s": float(options["--output-step"])}
        stdout.write(json.dumps(payload(item)).encode())
        stderr.write(b"synthetic diagnostic\n")
        return subprocess.CompletedProcess(argv, 0)

    def failed_child(self, argv, *, stdout, stderr, **unused):
        stdout.write(b"partial candidate output\n")
        stderr.write(b"specific child failure\n")
        return subprocess.CompletedProcess(argv, 7)

    def test_plan_is_deterministic_deduplicated_and_covers_independent_axes(self):
        names = sorted(sweep.fixtures())
        items = sweep.plan(names)
        self.assertEqual(items, sweep.plan(list(reversed(names)) + names))
        self.assertEqual(len(items), 208)
        self.assertEqual(len({item["id"] for item in items}), 208)
        for name in names:
            group = [item for item in items if item["fixture"] == name]
            self.assertEqual(len(group), 8)
            self.assertEqual(len({(x["maximum_step_s"], x["event_tolerance_s"],
                                  x["tolerance_scale"]) for x in group}), 8)
            step = [x for x in group if "maximum_step" in x["axes"]]
            event = [x for x in group if "event_tolerance" in x["axes"]]
            local = [x for x in group if "local_tolerance" in x["axes"]]
            self.assertEqual([x["maximum_step_s"] for x in step], [20e-6, 10e-6, 5e-6, 2.5e-6])
            self.assertEqual({(x["event_tolerance_s"], x["tolerance_scale"]) for x in step}, {(2.5e-9, 1.0)})
            self.assertEqual({x["event_tolerance_s"] for x in event}, {10e-9, 5e-9, 2.5e-9})
            self.assertEqual({x["tolerance_scale"] for x in local}, {1.0, 0.1, 0.01})
            self.assertTrue(all(x["maximum_step_s"] == 2.5e-6 for x in event + local))
            self.assertTrue(all(x["tolerance_scale"] == 1.0 for x in event))
            self.assertTrue(all(x["event_tolerance_s"] == 2.5e-9 for x in local))
            self.assertTrue(all(x["output_step_s"] == 20e-6 for x in group))

    def test_plan_only_does_not_require_executable_or_write_output(self):
        self.executable.unlink()
        result, output, errors = self.cli("--plan")
        self.assertEqual((result, errors), (0, ""))
        self.assertEqual(len(json.loads(output)["runs"]), 8)
        self.assertFalse(self.output.exists())

    def test_existing_directory_is_refused_without_mutation_or_child_execution(self):
        self.output.mkdir()
        sentinel = self.output / "keep-me.txt"
        sentinel.write_bytes(b"unrelated data")
        with patch.object(sweep.subprocess, "run") as run:
            result, output, errors = self.cli()
        self.assertEqual((result, output), (1, ""))
        self.assertIn("already exists", errors)
        run.assert_not_called()
        self.assertEqual(list(self.output.iterdir()), [sentinel])
        self.assertEqual(sentinel.read_bytes(), b"unrelated data")

    def test_success_creates_hash_bound_manifest_and_generation_only_aggregate(self):
        with patch.object(sweep.subprocess, "run", side_effect=self.valid_child) as run:
            result, output, _ = self.cli()
        self.assertEqual(result, 0, output)
        self.assertEqual(run.call_count, 8)
        manifest = json.loads((self.output / "manifest.json").read_text())
        aggregate = json.loads((self.output / "candidates.json").read_text())
        self.assertEqual(manifest["status"], "candidate_generation_complete")
        self.assertEqual(manifest["acceptance"], "not evaluated")
        self.assertTrue(aggregate["generation_only"])
        self.assertEqual(len(aggregate["runs"]), 8)
        self.assertTrue(manifest["executable_unchanged"])
        expected_sources = (
            "tools/run_contact_sweep.py", "tools/validate_contact_reference.py",
            "docs/specs/CONTACT_ACCEPTANCE_20261123.md",
            "docs/specs/CONTACT_REFINEMENT_EXTENSION_20261123.md",
            "docs/agents/development-20261123/W1-A005.md",
            "docs/agents/development-20261123/W1-A007.md",
        )
        self.assertEqual(manifest["source_sha256"], {
            name: hashlib.sha256((ROOT / name).read_bytes()).hexdigest()
            for name in expected_sources
        })
        for record in manifest["results"]:
            self.assertEqual(record["status"], "candidate_generated")
            for kind in ("stdout", "stderr"):
                raw = (self.output / record[kind]).read_bytes()
                self.assertEqual(hashlib.sha256(raw).hexdigest(), record[kind + "_sha256"])
        self.assertEqual(sweep.sha256(self.output / "candidates.json"), manifest["aggregate"]["sha256"])

    def test_failed_forced_rerun_preserves_unrelated_files_and_removes_stale_aggregate(self):
        self.output.mkdir()
        (self.output / "keep-me.txt").write_bytes(b"unrelated data")
        (self.output / "candidates.json").write_text('{"stale":true}', encoding="utf-8")
        with patch.object(sweep.subprocess, "run", side_effect=self.failed_child):
            result, output, _ = self.cli("--force")
        self.assertEqual(result, 1, output)
        self.assertEqual((self.output / "keep-me.txt").read_bytes(), b"unrelated data")
        self.assertFalse((self.output / "candidates.json").exists())
        manifest = json.loads((self.output / "manifest.json").read_text())
        self.assertEqual(manifest["status"], "failed")
        self.assertEqual(len(manifest["results"]), 8)
        for record in manifest["results"]:
            self.assertEqual(record["exit_code"], 7)
            self.assertEqual(record["status"], "failed")
            self.assertIn("candidate exited 7", record["errors"])
            self.assertEqual((self.output / record["stdout"]).read_bytes(), b"partial candidate output\n")
            self.assertEqual((self.output / record["stderr"]).read_bytes(), b"specific child failure\n")

    def test_candidate_rejects_malformed_nonfinite_settings_and_incomplete_samples(self):
        good = payload(self.item)
        cases = [b"{invalid", b"[]", json.dumps(good).replace('"diagnostics": {}', '"diagnostics": {"x": NaN}').encode(),
                 json.dumps(good).replace('"diagnostics": {}', '"diagnostics": {"x": 1e999}').encode()]
        for field, value in (("relative_tolerance", 2e-8), ("fixture", "different"),
                             ("success", False), ("failure_code", True)):
            bad = copy.deepcopy(good)
            bad["runs"][0][field] = value
            cases.append(json.dumps(bad).encode())
        for edit in ("incomplete", "duplicate_time", "bool_number", "bad_vector"):
            bad = copy.deepcopy(good)
            sample = bad["runs"][0]["samples"][-1]
            if edit == "incomplete": sample["time_s"] = 0.001
            elif edit == "duplicate_time": sample["time_s"] = 0.0
            elif edit == "bool_number": sample["energy_j"] = True
            else: sample["q_m"] = [0.0]
            cases.append(json.dumps(bad).encode())
        path = self.root / "candidate.json"
        for index, raw in enumerate(cases):
            with self.subTest(case=index):
                path.write_bytes(raw)
                with self.assertRaises(ValueError):
                    sweep.candidate(path, self.item, 0.002)

    def test_timeout_preserves_partial_streams_and_marks_failure(self):
        self.output.mkdir()
        def timeout(argv, *, stdout, stderr, **unused):
            stdout.write(b"partial timeout output")
            stderr.write(b"partial timeout diagnostic")
            raise subprocess.TimeoutExpired(argv, 0.25)
        with patch.object(sweep.subprocess, "run", side_effect=timeout):
            record = sweep.execute(self.executable, self.output, self.item, 0.25, 0.002)
        self.assertEqual(record["status"], "failed")
        self.assertIsNone(record["exit_code"])
        self.assertIn("candidate exceeded 0.25 second timeout", record["errors"])
        self.assertEqual((self.output / record["stdout"]).read_bytes(), b"partial timeout output")
        self.assertEqual((self.output / record["stderr"]).read_bytes(), b"partial timeout diagnostic")

    def test_real_python_child_uses_file_streams_and_preserves_argv(self):
        expected = payload(self.item)
        self.executable.write_text(
            "import json, sys\n"
            "print(" + repr(json.dumps(expected)) + ")\n"
            "print(json.dumps(sys.argv[1:]), file=sys.stderr)\n", encoding="utf-8")
        self.output.mkdir()
        def portable_python(argv, **kwargs):
            return REAL_RUN([sys.executable, *argv], **kwargs)
        with patch.object(sweep.subprocess, "run", side_effect=portable_python):
            record = sweep.execute(self.executable, self.output, self.item, 10.0, 0.002)
        self.assertEqual(record["status"], "candidate_generated", record["errors"])
        self.assertEqual(json.loads((self.output / record["stderr"]).read_text()), record["argv"][1:])

    def test_output_and_managed_symlinks_are_refused_without_touching_targets(self):
        target = self.root / "target"
        target.mkdir()
        sentinel = target / "sentinel.txt"
        sentinel.write_bytes(b"protected")
        try:
            self.output.symlink_to(target, target_is_directory=True)
        except (OSError, NotImplementedError) as error:
            self.skipTest(f"Symlink creation unavailable: {error}")
        with patch.object(sweep.subprocess, "run") as run:
            result, _, errors = self.cli("--force")
        self.assertEqual(result, 1)
        self.assertIn("must not be a symlink", errors)
        run.assert_not_called()
        self.output.unlink()
        self.output.mkdir()
        for name in ("manifest.json", "candidates.json", self.item["id"] + ".json",
                     self.item["id"] + ".stderr.txt"):
            with self.subTest(name=name):
                link = self.output / name
                link.symlink_to(sentinel)
                with patch.object(sweep.subprocess, "run") as run:
                    result, _, errors = self.cli("--force")
                self.assertEqual(result, 1)
                self.assertIn("managed output is a symlink", errors)
                run.assert_not_called()
                self.assertEqual(sentinel.read_bytes(), b"protected")
                link.unlink()

    def test_invalid_timeout_is_rejected_before_execution(self):
        for value in ("nan", "inf", "0", "-1", "601"):
            with self.subTest(value=value), patch.object(sweep.subprocess, "run") as run:
                with self.assertRaises(SystemExit) as error:
                    self.cli("--timeout", value)
                self.assertEqual(error.exception.code, 2)
                run.assert_not_called()
                self.assertFalse(self.output.exists())


if __name__ == "__main__":
    unittest.main()
