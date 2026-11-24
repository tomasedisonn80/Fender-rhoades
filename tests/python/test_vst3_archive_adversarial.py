"""Independent malformed-container and execution-order archive regressions."""
from __future__ import annotations

import copy
from contextlib import redirect_stdout
import gzip
import hashlib
import io
import json
from pathlib import Path
import sys
import tarfile
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools"))
import verify_vst3_matrix_archive as verifier


# Valid gzip envelope with a reserved DEFLATE block type; gzip delegates this
# corruption to zlib.error rather than OSError or EOFError.
INVALID_DEFLATE = bytes.fromhex("1f8b0800000000000003070000000000000000")


def compressed_tar(payloads, *, pax_headers=None):
    stream = io.BytesIO()
    with tarfile.open(fileobj=stream, mode="w", format=tarfile.PAX_FORMAT) as archive:
        for name, raw in payloads.items():
            member = tarfile.TarInfo(name)
            member.size = len(raw)
            if pax_headers:
                member.pax_headers = pax_headers
            archive.addfile(member, io.BytesIO(raw))
    return gzip.compress(stream.getvalue(), mtime=0)


class IndependentArchiveBoundaries(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.original = ROOT / "docs/validation/vst3-matrix-20261124.json"
        cls.report = json.loads(cls.original.read_bytes())
        with tarfile.open(cls.original.with_suffix(".tar.gz"), "r:gz") as archive:
            cls.payloads = {member.name: archive.extractfile(member).read()
                            for member in archive}

    def rebound_report(self, directory, raw, payloads=None):
        report = copy.deepcopy(self.report)
        report.update(archive="adversarial.tar.gz", archive_bytes=len(raw),
                      archive_sha256=hashlib.sha256(raw).hexdigest())
        if payloads is not None:
            report["member_sha256"] = {
                name: hashlib.sha256(data).hexdigest() for name, data in payloads.items()
            }
        (Path(directory) / report["archive"]).write_bytes(raw)
        path = Path(directory) / "adversarial.json"
        path.write_text(json.dumps(report), encoding="utf-8")
        return path

    def test_invalid_deflate_is_a_normalized_rejection(self):
        with self.assertRaisesRegex(verifier.VerificationError, "invalid compressed tar"):
            verifier.archive_members(INVALID_DEFLATE)

    def test_cli_retains_invalid_deflate_and_verifies_later_report(self):
        with tempfile.TemporaryDirectory() as directory:
            broken = self.rebound_report(directory, INVALID_DEFLATE)
            output = io.StringIO()
            with redirect_stdout(output):
                status = verifier.main([str(broken), str(self.original)])
        self.assertEqual(status, 1)
        results = json.loads(output.getvalue())["results"]
        self.assertEqual([item["status"] for item in results], ["rejected", "verified"])
        self.assertIn("invalid compressed tar", results[0]["error"])
        self.assertEqual(results[1]["process_runs"], 72)

    def test_second_gzip_member_cannot_hide_a_second_tar_archive(self):
        first = compressed_tar({"one": b"first"})
        second = compressed_tar({"two": b"second"})
        with self.assertRaisesRegex(verifier.VerificationError, "after tar end"):
            verifier.archive_members(first + second)

    def test_pax_overrides_cannot_bypass_member_boundaries(self):
        for fields, diagnostic in (
            ({"path": "../outside"}, "unsafe"),
            ({"size": str(verifier.MAX_MEMBER_BYTES + 1)}, "member byte"),
            ({"GNU.sparse.map": "0,1", "GNU.sparse.size": "1"}, "sparse"),
        ):
            with self.subTest(fields=fields):
                raw = compressed_tar({"safe": b"x"}, pax_headers=fields)
                with self.assertRaisesRegex(verifier.VerificationError, diagnostic):
                    verifier.archive_members(raw)

    def test_rebound_execution_records_cannot_overlap_or_use_boolean_time(self):
        plan = json.loads(self.payloads["plan.json"])
        first, second = plan["runs"][:2]
        prior_end = json.loads(self.payloads[first["record_file"]])["end_monotonic_ns"]
        for start in (prior_end - 1, True):
            with self.subTest(start=start), tempfile.TemporaryDirectory() as directory:
                payloads = dict(self.payloads)
                execution = json.loads(payloads[second["record_file"]])
                execution["start_monotonic_ns"] = start
                payloads[second["record_file"]] = json.dumps(execution).encode()
                manifest = json.loads(payloads["manifest.json"])
                manifest["results"][execution["sequence"] - 1] = execution
                payloads["manifest.json"] = json.dumps(manifest).encode()
                path = self.rebound_report(directory, compressed_tar(payloads), payloads)
                with self.assertRaisesRegex(verifier.VerificationError, "nonsequential"):
                    verifier.verify_report(path)


if __name__ == "__main__":
    unittest.main()
