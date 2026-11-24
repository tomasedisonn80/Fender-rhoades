"""Failure-preserving CI capture and source-binding regression fixtures."""
from __future__ import annotations

import hashlib
import importlib.util
import json
import os
from pathlib import Path
import platform
import shutil
import subprocess
import sys
import tempfile
import unittest
import xml.etree.ElementTree as ET


ROOT = Path(__file__).resolve().parents[2]
HELPER = ROOT / "tools/capture_ci_diagnostics.py"
SPEC = importlib.util.spec_from_file_location("ci_diagnostics", HELPER)
diagnostics = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(diagnostics)


class CIDiagnosticsTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix="rhodes-ci-diagnostics-")
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        self.output = self.root / "evidence"

    def cli(self, *arguments, env=None):
        return subprocess.run([sys.executable, str(HELPER), *map(str, arguments)],
                              cwd=self.root, capture_output=True, env=env, check=False)

    def read_json(self, name):
        return json.loads((self.output / name).read_text(encoding="utf-8"))

    def repository(self):
        root = self.root / "source"
        root.mkdir()
        for args in (("init", "-q"), ("config", "core.autocrlf", "false")):
            subprocess.run(["git", "-C", str(root), *args], capture_output=True, check=True)
        (root / "input.cpp").write_bytes(b"// retained input\n")
        subprocess.run(["git", "-C", str(root), "add", "input.cpp"], check=True,
                       capture_output=True)
        subprocess.run(["git", "-C", str(root), "-c", "user.name=CI fixture",
                        "-c", "user.email=fixture@example.invalid", "commit", "-qm", "fixture"],
                       check=True, capture_output=True)
        head = subprocess.check_output(["git", "-C", str(root), "rev-parse", "HEAD"],
                                       text=True).strip()
        return root, head

    def test_complete_combined_log_and_nonzero_child_status_are_preserved(self):
        payload = b"a" * 150000 + b"\nstdout marker\xff\n" + b"stderr marker\xfe\n"
        child = ("import os,sys; os.write(1,b'a'*150000+b'\\nstdout marker\\xff\\n'); "
                 "os.write(2,b'stderr marker\\xfe\\n'); sys.exit(23)")
        result = self.cli("run", "--output-dir", self.output, "--label", "failure", "--",
                          sys.executable, "-c", child)
        self.assertEqual(result.returncode, 23, result.stderr)
        self.assertEqual((self.output / "failure.log").read_bytes(), payload)
        self.assertEqual(result.stdout, payload)
        record = self.read_json("failure.command.json")
        self.assertEqual(record["returncode"], 23)
        self.assertEqual(record["wrapper_returncode"], 23)
        self.assertEqual(record["log_sha256"], hashlib.sha256(payload).hexdigest())
        self.assertEqual(record["log_bytes"], len(payload))

    def test_missing_executable_retains_start_failure(self):
        result = self.cli("run", "--output-dir", self.output, "--label", "missing", "--",
                          self.root / "no-such-executable")
        self.assertEqual(result.returncode, 127)
        record = self.read_json("missing.command.json")
        self.assertEqual(record["status"], "could_not_start")
        self.assertEqual(record["returncode"], 127)
        self.assertTrue((self.output / "missing.log").read_bytes())

    def test_duplicate_label_cannot_erase_a_first_failure(self):
        args = ("run", "--output-dir", self.output, "--label", "unique", "--", sys.executable)
        first = self.cli(*args, "-c", "print('first failure'); raise SystemExit(9)")
        self.assertEqual(first.returncode, 9)
        original = (self.output / "unique.command.json").read_bytes()
        second = self.cli(*args, "-c", "print('replacement')")
        self.assertEqual(second.returncode, 2)
        self.assertEqual((self.output / "unique.command.json").read_bytes(), original)
        self.assertEqual((self.output / "unique.log").read_text(), "first failure\n")

    @unittest.skipUnless(os.name == "posix", "POSIX signal exit convention")
    def test_signal_status_is_retained_and_forwarded_as_shell_status(self):
        result = self.cli("run", "--output-dir", self.output, "--label", "signal", "--",
                          sys.executable, "-c", "import os,signal; os.kill(os.getpid(),signal.SIGTERM)")
        self.assertEqual(result.returncode, 143)
        self.assertEqual(self.read_json("signal.command.json")["returncode"], -15)

    @unittest.skipUnless(shutil.which("ctest"), "CTest is required for a real wrapper fixture")
    def test_ctest_failure_retains_inventory_junit_raw_log_and_skip(self):
        build = self.root / "fixture-build"
        build.mkdir()
        executable = Path(sys.executable).as_posix()
        (build / "CTestTestfile.cmake").write_text(
            f'add_test(fixture_pass "{executable}" "-c" "print(123)")\n'
            f'add_test(fixture_fail "{executable}" "-c" "print(456); raise SystemExit(7)")\n'
            f'add_test(fixture_skip "{executable}" "-c" "raise SystemExit(77)")\n'
            'set_tests_properties(fixture_skip PROPERTIES SKIP_RETURN_CODE "77")\n'
            f'add_test(excluded "{executable}" "-c" "raise SystemExit(99)")\n',
            encoding="utf-8")
        result = self.cli("ctest", "--output-dir", self.output, "--label", "ctest", "--",
                          shutil.which("ctest"), "--test-dir", build, "-C", "Release",
                          "-R", "^fixture_", "--output-on-failure", "--no-tests=error")
        self.assertEqual(result.returncode, 8, result.stderr)
        self.assertEqual(self.read_json("ctest.command.json")["returncode"], 8)
        inventory = self.read_json("ctest-inventory.json")
        self.assertEqual({test["name"] for test in inventory["tests"]},
                         {"fixture_pass", "fixture_fail", "fixture_skip"})
        junit = ET.parse(self.output / "ctest-junit.xml")
        cases = {case.attrib["name"]: case for case in junit.findall(".//testcase")}
        self.assertEqual(set(cases), {"fixture_pass", "fixture_fail", "fixture_skip"})
        self.assertIsNotNone(cases["fixture_fail"].find("failure"))
        self.assertIsNotNone(cases["fixture_skip"].find("skipped"))
        self.assertIn("456", (self.output / "ctest.log").read_text())
        self.assertIn("fixture_fail", (self.output / "ctest-output.log").read_text())
        self.assertTrue((build / "Testing/Temporary/LastTest.log").is_file())

    @unittest.skipUnless(shutil.which("git"), "Git is required for source binding")
    def test_source_sha_mismatch_fails_after_retaining_observed_metadata(self):
        source, head = self.repository()
        result = self.cli("metadata", "--phase", "before", "--source-root", source,
                          "--expected-sha", "0" * 40, "--output-dir", self.output)
        self.assertEqual(result.returncode, 1, result.stderr)
        record = self.read_json("metadata-before.json")
        self.assertFalse(record["valid"])
        self.assertEqual(record["source"]["commit"], head)
        self.assertIn("Source commit mismatch", " ".join(record["errors"]))

    @unittest.skipUnless(shutil.which("git"), "Git is required for source binding")
    def test_source_mutation_cannot_inherit_a_clean_commit_label(self):
        source, head = self.repository()
        args = ("metadata", "--source-root", source, "--expected-sha", head,
                "--output-dir", self.output)
        self.assertEqual(self.cli(*args, "--phase", "before").returncode, 0)
        (source / "input.cpp").write_text("// changed after snapshot\n", encoding="utf-8")
        result = self.cli(*args, "--phase", "after")
        self.assertEqual(result.returncode, 1, result.stderr)
        before, after = self.read_json("metadata-before.json"), self.read_json("metadata-after.json")
        self.assertEqual(before["source"]["commit"], after["source"]["commit"])
        self.assertNotEqual(before["source"]["files_sha256"], after["source"]["files_sha256"])
        self.assertFalse(after["source"]["tracked_clean"])
        self.assertIn("Source snapshot changed", " ".join(after["errors"]))

    @unittest.skipUnless(shutil.which("git"), "Git is required for source binding")
    def test_clean_source_hashes_and_allowlisted_run_metadata_are_retained(self):
        source, head = self.repository()
        env = dict(os.environ, GITHUB_RUN_ID="1234", GITHUB_RUN_ATTEMPT="2",
                   RUNNER_ARCH="fixture-arch", GITHUB_TOKEN="must-not-appear")
        args = ("metadata", "--source-root", source, "--expected-sha", head,
                "--output-dir", self.output)
        self.assertEqual(self.cli(*args, "--phase", "before", env=env).returncode, 0)
        (source / "untracked-build-output").write_text("generated", encoding="utf-8")
        self.assertEqual(self.cli(*args, "--phase", "after", env=env).returncode, 0)
        record = self.read_json("metadata-after.json")
        self.assertTrue(record["valid"])
        self.assertEqual(record["source"]["files"][0]["sha256"],
                         hashlib.sha256(b"// retained input\n").hexdigest())
        self.assertEqual(record["environment"]["github"]["GITHUB_RUN_ATTEMPT"], "2")
        self.assertEqual(record["environment"]["github"]["GITHUB_RUN_ID"], "1234")
        self.assertNotIn("must-not-appear", (self.output / "metadata-after.json").read_text())

    def make_build(self, name, version="12.2.0"):
        root = self.root / name
        definitions = root / "CMakeFiles/3.30.0"
        definitions.mkdir(parents=True)
        (root / "CMakeCache.txt").write_text(
            "CMAKE_GENERATOR:INTERNAL=Ninja\nCMAKE_BUILD_TYPE:STRING=Release\n"
            "CMAKE_CXX_COMPILER:FILEPATH=/fixture/c++\n", encoding="utf-8")
        (definitions / "CMakeCXXCompiler.cmake").write_text(
            'set(CMAKE_CXX_COMPILER "/fixture/c++")\n'
            'set(CMAKE_CXX_COMPILER_ID "GNU")\n'
            f'set(CMAKE_CXX_COMPILER_VERSION "{version}")\n', encoding="utf-8")
        return root

    def test_toolchain_comparison_uses_detected_compiler_version(self):
        baseline, current = self.make_build("baseline"), self.make_build("current", "13.1.0")
        result = self.cli("verify-toolchains", "--baseline-build", baseline,
                          "--current-build", current, "--output-dir", self.output)
        self.assertEqual(result.returncode, 1, result.stderr)
        record = self.read_json("toolchain-comparison.json")
        self.assertFalse(record["valid"])
        self.assertIn("CMAKE_CXX_COMPILER_VERSION", " ".join(record["errors"]))

    def test_matching_toolchains_pass_with_retained_compiler_identity(self):
        baseline, current = self.make_build("baseline"), self.make_build("current")
        result = self.cli("verify-toolchains", "--baseline-build", baseline,
                          "--current-build", current, "--output-dir", self.output)
        self.assertEqual(result.returncode, 0, result.stderr)
        record = self.read_json("toolchain-comparison.json")
        self.assertTrue(record["valid"])
        self.assertEqual(record["current"]["toolchain"]["CMAKE_CXX_COMPILER_ID"], "GNU")

    def test_module_hashes_identify_all_three_actual_native_files(self):
        suffix = {"Linux": ".so", "Darwin": "", "Windows": ".vst3"}[platform.system()]
        for name in diagnostics.BUNDLE_NAMES:
            module = self.root / f"{name}.vst3/Contents/native" / f"{name}{suffix}"
            module.parent.mkdir(parents=True)
            module.write_bytes(name.encode("ascii"))
        errors = []
        snapshot = diagnostics.module_snapshot(self.root, errors)
        self.assertEqual(errors, [])
        self.assertEqual(len(snapshot["modules"]), 3)
        for module in snapshot["modules"]:
            self.assertFalse(Path(module["path"]).is_absolute())
            self.assertEqual(module["sha256"],
                             hashlib.sha256((self.root / module["path"]).read_bytes()).hexdigest())
        (self.root / snapshot["modules"][1]["path"]).unlink()
        errors = []
        self.assertEqual(len(diagnostics.module_snapshot(self.root, errors)["modules"]), 2)
        self.assertTrue(errors)

    @unittest.skipUnless(shutil.which("git"), "Git is required for source binding")
    def test_fixed_legacy_manifest_rejects_unrelated_source_and_retains_reason(self):
        source, head = self.repository()
        build = self.make_build("baseline")
        result = self.cli("baseline-manifest", "--source-root", source,
                          "--bundle-root", self.root / "missing-bundles", "--build-dir", build,
                          "--sdk-root", self.root / "missing-sdk", "--output-dir", self.output)
        self.assertEqual(result.returncode, 1, result.stderr)
        record = self.read_json("baseline-manifest.json")
        self.assertEqual(record["source_commit"], head)
        self.assertIn(diagnostics.LEGACY_COMMIT, " ".join(record["errors"]))
        self.assertFalse(record["valid"])
        self.assertFalse((self.root / "missing-bundles/manifest.json").exists())


if __name__ == "__main__":
    unittest.main()
