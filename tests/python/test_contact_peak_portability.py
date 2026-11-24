"""CLI portability regressions; constructed payloads are not physics evidence."""
from __future__ import annotations

import errno
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

from test_contact_peak_schema import ROOT, encode, payload


CLI = ROOT / "tools" / "validate_contact_peaks.py"


class ContactPeakPortabilityTests(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory(prefix="contact portability ")
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name)

    def retained_paths(self, candidate, output, producer):
        document = payload("forced_clamp")
        run = document["runs"][0]
        run["samples"][0]["hammer_gap_m"] = 1e-4
        run["diagnostics"]["sampled_maxima"]["hammer_compression_m"] = 1e-4
        run["diagnostics"]["located_peak_candidates"]["hammer_compression"]["value_m"] = 1e-4
        raw = encode(document) + b"\r\n"
        candidate.write_bytes(raw)
        # Include CR/LF, Ctrl-Z and non-text bytes across the hash read boundary.
        producer_raw = bytes(range(256)) * 257 + b"\r\n\x1a"
        producer.write_bytes(producer_raw)
        completed = subprocess.run(
            [sys.executable, "-S", str(CLI), "--candidate", str(candidate),
             "--output", str(output), "--producer-binary", str(producer)],
            cwd=self.root, env={**os.environ, "PYTHONIOENCODING": "ascii:strict"},
            capture_output=True, check=False, timeout=15,
        )
        self.assertEqual(completed.returncode, 0, completed.stdout + completed.stderr)
        self.assertEqual(completed.stderr, b"")
        self.assertTrue(completed.stdout.isascii())
        summary = json.loads(completed.stdout)
        self.assertEqual(summary["output"], str(output))
        self.assertEqual(summary["phy002_acceptance"], "incomplete")
        records = {}
        for name in ("plan.json", "manifest.json", "candidate-0001.record.json"):
            retained = (output / name).read_bytes()
            self.assertTrue(retained.isascii(), name)
            records[name] = json.loads(retained)
        plan, manifest, record = (records[name] for name in
                                  ("plan.json", "manifest.json", "candidate-0001.record.json"))
        self.assertEqual(plan["candidates"], [str(candidate)])
        self.assertEqual(plan["producer_binary"], str(producer))
        self.assertEqual(record["input_path"], str(candidate))
        self.assertEqual(manifest["cases"], [record])
        self.assertEqual(manifest["status"], "pass")
        self.assertEqual(manifest["phy002_acceptance"], "incomplete")
        self.assertEqual(manifest["operational_errors"], [])
        self.assertEqual(manifest["plan_sha256"],
                         hashlib.sha256((output / "plan.json").read_bytes()).hexdigest())
        self.assertEqual((output / record["raw_file"]).read_bytes(), raw)
        self.assertEqual(record["raw_sha256"], hashlib.sha256(raw).hexdigest())
        self.assertEqual(record["retained_sha256"], record["raw_sha256"])
        self.assertEqual(manifest["declared_producer"], {
            "declared_path": str(producer), "bytes": len(producer_raw),
            "sha256": hashlib.sha256(producer_raw).hexdigest(),
            "executed": False, "candidate_production_attested": False,
        })
        self.assertEqual(list(output.glob(".contact-peaks-*")), [])
        return plan, record

    def test_help_works_with_optional_imports_explicitly_blocked(self):
        bootstrap = """import builtins, pathlib, runpy, sys
script = sys.argv[1]
sys.path.insert(0, str(pathlib.Path(script).parent))
sys.argv = [script, '--help']
original = builtins.__import__
def guarded(name, *args, **kwargs):
    if name.split('.')[0] in {'numpy', 'scipy'}:
        raise AssertionError('optional backend imported by --help')
    return original(name, *args, **kwargs)
builtins.__import__ = guarded
try:
    runpy.run_path(script, run_name='__main__')
except SystemExit as error:
    assert error.code == 0, error.code
else:
    raise AssertionError('--help did not exit')
assert 'numpy' not in sys.modules and 'scipy' not in sys.modules
"""
        completed = subprocess.run([sys.executable, "-S", "-c", bootstrap, str(CLI)],
                                   cwd=self.root, capture_output=True, check=False, timeout=10)
        self.assertEqual(completed.returncode, 0, completed.stderr)
        self.assertEqual(completed.stderr, b"")
        for option in (b"--candidate", b"--output", b"--with-damped-time", b"--producer-binary"):
            self.assertIn(option, completed.stdout)
        self.assertEqual(list(self.root.iterdir()), [])

    def test_unicode_and_space_paths_keep_raw_hashes_with_ascii_stdout(self):
        self.retained_paths(self.root / "candidate 旋律 Ω é.json",
                            self.root / "evidence 圧縮 Ω é",
                            self.root / "declared producer Ω é.bin")

    @unittest.skipUnless(os.name == "posix", "surrogate-escaped filenames are POSIX-specific")
    def test_posix_surrogate_escaped_paths_survive_retention(self):
        candidate = self.root / os.fsdecode(b"candidate \xff.json")
        output = self.root / os.fsdecode(b"evidence \xfe")
        producer = self.root / os.fsdecode(b"declared producer \xfd.bin")
        # Some POSIX filesystems reject undecodable names before the CLI can
        # run. Probe that capability without hiding unrelated I/O failures.
        try:
            candidate.write_bytes(b"filename capability probe")
        except UnicodeEncodeError:
            self.skipTest("filesystem encoding rejects surrogate-escaped filenames")
        except OSError as error:
            if error.errno == errno.EILSEQ:
                self.skipTest("filesystem rejects undecodable filename bytes")
            raise
        candidate.unlink()
        plan, record = self.retained_paths(candidate, output, producer)
        self.assertEqual(os.fsencode(record["input_path"]), os.fsencode(candidate))
        self.assertEqual(os.fsencode(plan["producer_binary"]), os.fsencode(producer))
        self.assertIn(b"\\udcff", (output / "plan.json").read_bytes())


if __name__ == "__main__":
    unittest.main()
