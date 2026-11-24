"""Untrusted archive boundaries and semantic tampering beyond regenerated hashes."""
from __future__ import annotations

import copy
import gzip
import io
import json
from pathlib import Path
import sys
import tarfile
import tempfile
import unittest
from unittest import mock

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools"))
import verify_vst3_matrix_archive as verifier


def tar_bytes(entries):
    target = io.BytesIO()
    with tarfile.open(fileobj=target, mode="w", format=tarfile.PAX_FORMAT) as archive:
        for name, payload, kind in entries:
            member = tarfile.TarInfo(name)
            member.type = kind
            member.size = len(payload) if kind == tarfile.REGTYPE else 0
            member.linkname = "outside" if kind in (tarfile.LNKTYPE, tarfile.SYMTYPE) else ""
            archive.addfile(member, io.BytesIO(payload))
    return gzip.compress(target.getvalue(), mtime=0)


class Boundaries(unittest.TestCase):
    def test_strict_json_rejects_duplicates_nonfinite_and_bad_encoding(self):
        for raw in (b'{"a":1,"a":2}', b'{"x":NaN}', b'[Infinity]',
                    b'[1e309]', b'{"a":"\xff"}', b'1' * 129):
            with self.subTest(raw=raw[:30]), self.assertRaises(verifier.VerificationError):
                verifier.strict_json(raw, "fixture")

    def test_json_depth_node_and_byte_limits(self):
        with self.assertRaisesRegex(verifier.VerificationError, "nesting"):
            verifier.strict_json(b'[' * 65 + b'0' + b']' * 65, "fixture")
        with mock.patch.object(verifier, "MAX_JSON_NODES", 3):
            with self.assertRaisesRegex(verifier.VerificationError, "node"):
                verifier.strict_json(b'[0,1,2]', "fixture")
        with mock.patch.object(verifier, "MAX_MEMBER_BYTES", 3):
            with self.assertRaisesRegex(verifier.VerificationError, "byte"):
                verifier.strict_json(b'[0,1]', "fixture")
        # Brackets and escaped quotes inside strings do not consume depth.
        self.assertEqual(verifier.strict_json(b'"[\\\"["', "fixture"), '["[')

    def test_archive_names_and_types(self):
        for name in ("../escape", "/absolute", "a/b", "a\\b", "C:drive", ".", "a\n"):
            with self.subTest(name=name), self.assertRaisesRegex(verifier.VerificationError, "unsafe"):
                verifier.archive_members(tar_bytes([(name, b"", tarfile.REGTYPE)]))
        for kind in (tarfile.SYMTYPE, tarfile.LNKTYPE, tarfile.FIFOTYPE, tarfile.DIRTYPE):
            with self.subTest(kind=kind), self.assertRaisesRegex(verifier.VerificationError, "non-regular"):
                verifier.archive_members(tar_bytes([("member", b"", kind)]))

    def test_archive_duplicate_member(self):
        raw = tar_bytes([("same", b"a", tarfile.REGTYPE), ("same", b"b", tarfile.REGTYPE)])
        with self.assertRaisesRegex(verifier.VerificationError, "duplicate"):
            verifier.archive_members(raw)

    def test_archive_budgets(self):
        raw = tar_bytes([("one", b"1234", tarfile.REGTYPE), ("two", b"5", tarfile.REGTYPE)])
        for constant, limit, diagnostic in (
            ("MAX_COMPRESSED_BYTES", 8, "compressed"),
            ("MAX_TAR_BYTES", 128, "decompressed"),
            ("MAX_MEMBER_BYTES", 3, "member byte"),
            ("MAX_PAYLOAD_BYTES", 4, "payload"),
            ("MAX_MEMBERS", 1, "count"),
        ):
            with self.subTest(constant=constant), mock.patch.object(verifier, constant, limit):
                with self.assertRaisesRegex(verifier.VerificationError, diagnostic):
                    verifier.archive_members(raw)

    def test_archive_truncated_and_hidden_trailing_data(self):
        raw = tar_bytes([("one", b"payload", tarfile.REGTYPE)])
        with self.assertRaises(verifier.VerificationError):
            verifier.archive_members(raw[:-9])
        trailing = gzip.compress(gzip.decompress(raw) + b"concealed", mtime=0)
        with self.assertRaisesRegex(verifier.VerificationError, "after tar end"):
            verifier.archive_members(trailing)

    def test_tar_advertised_size_cannot_hide_truncation(self):
        header = tarfile.TarInfo("truncated")
        header.size = 1024
        raw = gzip.compress(header.tobuf() + b"x", mtime=0)
        with self.assertRaises(verifier.VerificationError):
            verifier.archive_members(raw)

    def test_regular_input_and_read_limit(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "file"
            path.write_bytes(b"1234")
            with self.assertRaisesRegex(verifier.VerificationError, "byte limit"):
                verifier.bounded_read(path, 3)
            with self.assertRaises((verifier.VerificationError, OSError)):
                verifier.bounded_read(Path(directory), 3)


class ArchivedMatrix(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.original = ROOT / "docs/validation/vst3-matrix-20261124.json"
        cls.report = json.loads(cls.original.read_bytes())
        with tarfile.open(cls.original.with_suffix(".tar.gz")) as archive:
            cls.payloads = {member.name: archive.extractfile(member).read() for member in archive}

    @staticmethod
    def edit_json(payloads, name, edit):
        document = json.loads(payloads[name])
        edit(document)
        payloads[name] = json.dumps(document, ensure_ascii=True, allow_nan=False).encode()

    @classmethod
    def rebind_execution(cls, payloads, name="r1-p01-direct"):
        execution_name = name + ".execution.json"
        cls.edit_json(payloads, execution_name,
                      lambda d: d.update(stdout_sha256=verifier.digest(payloads[name + ".stdout.json"])))
        execution = json.loads(payloads[execution_name])
        cls.edit_json(payloads, "manifest.json",
                      lambda d: d["results"].__setitem__(execution["sequence"] - 1, execution))

    def bundle(self, directory, edit):
        report, payloads = copy.deepcopy(self.report), dict(self.payloads)
        edit(report, payloads)
        raw = tar_bytes([(name, data, tarfile.REGTYPE) for name, data in payloads.items()])
        archive_path = Path(directory) / "fixture.tar.gz"
        archive_path.write_bytes(raw)
        report.update(archive=archive_path.name, archive_bytes=len(raw),
                      archive_sha256=verifier.digest(raw),
                      member_sha256={name: verifier.digest(data) for name, data in payloads.items()})
        report_path = Path(directory) / "fixture.json"
        report_path.write_text(json.dumps(report), encoding="utf-8")
        return report_path

    def rejected(self, edit, diagnostic):
        with tempfile.TemporaryDirectory() as directory:
            path = self.bundle(directory, edit)
            with self.assertRaisesRegex(verifier.VerificationError, diagnostic):
                verifier.verify_report(path)

    def test_retained_matrix_recomputes_and_never_runs_commands(self):
        with mock.patch("subprocess.run", side_effect=AssertionError("must not execute")), \
             mock.patch("subprocess.Popen", side_effect=AssertionError("must not execute")), \
             mock.patch.object(tarfile.TarFile, "extractall", side_effect=AssertionError("must not extract")):
            result = verifier.verify_report(self.original)
        self.assertEqual(result["process_runs"], 72)
        self.assertEqual(result["same_variant_repeat_pairs"], 36)
        self.assertEqual(result["measured_callbacks"], 40128)
        self.assertEqual(result["measured_host_frames"], 3846144)
        self.assertEqual(result["member_count"], 298)

    def test_summary_tampering_with_valid_archive_hashes(self):
        self.rejected(lambda r, p: r["results"][0].update(deadline_misses=999),
                      "published timing summaries")

    def test_raw_timing_tampering_with_rebound_hashes(self):
        def edit(report, payloads):
            self.edit_json(payloads, "r1-p01-direct.stdout.json",
                           lambda d: d["timing"]["raw_callback_us"].__setitem__(0, 1e9))
            self.rebind_execution(payloads)
        self.rejected(edit, "timing.summary")

    def test_workload_tampering_with_rebound_hashes(self):
        def edit(report, payloads):
            self.edit_json(payloads, "r1-p01-direct.stdout.json",
                           lambda d: d["workload"].update(sample_rate_hz=96000))
            self.rebind_execution(payloads)
        self.rejected(edit, "workload.sample_rate_hz")

    def test_repeat_mismatch_with_rebound_hashes(self):
        def edit(report, payloads):
            self.edit_json(payloads, "r1-p01-direct.stdout.json",
                           lambda d: d["integrity"].update(output_fnv1a64="123456789abcdef0"))
            self.rebind_execution(payloads)
        self.rejected(edit, "repeat pairs")

    def test_plan_tampering_cannot_redefine_expectations(self):
        def edit(report, payloads):
            self.edit_json(payloads, "plan.json", lambda d: d["profiles"][0].update(active_keys=1))
        self.rejected(edit, "frozen plan content")

    def test_missing_member_with_updated_inventory(self):
        self.rejected(lambda r, p: p.pop("machine-before.json"), "frozen member inventory")

    def test_source_hash_drift(self):
        def edit(report, payloads):
            def change(hashes):
                hashes[next(iter(hashes))] = "0" * 64
            self.edit_json(payloads, "input-hashes-after.json", change)
        self.rejected(edit, "before/after input hashes")

    def test_false_completion_and_bool_count_rejected(self):
        self.rejected(lambda r, p: r.update(status="incomplete"), "report status")
        self.rejected(lambda r, p: r.update(schema_version=True), "report schema")

    def test_inconsistent_child_cpu_rejected(self):
        def edit(report, payloads):
            name = "r1-p01-direct"
            self.edit_json(payloads, name + ".affinity.json", lambda d: d.update(affinity=[1]))
            affinity = json.loads(payloads[name + ".affinity.json"])
            self.edit_json(payloads, name + ".execution.json", lambda d: d.update(child_affinity=affinity))
            self.rebind_execution(payloads)
        self.rejected(edit, "child affinity")

    def test_historical_python_sum_difference_is_explicit(self):
        def edit(report, payloads):
            self.edit_json(payloads, "r1-p01-direct.execution.json",
                           lambda d: d["verification"].update(mean_input_order_us=68.39333593750001))
            self.rebind_execution(payloads)
        with tempfile.TemporaryDirectory() as directory:
            result = verifier.verify_report(self.bundle(directory, edit))
        self.assertEqual(result["input_order_mean_differences"], ["r1-p01-direct"])

    def test_cli_retains_failure_and_checks_later_argument(self):
        with tempfile.TemporaryDirectory() as directory:
            broken = Path(directory) / "broken.json"
            broken.write_text('{"schema_version":1,"schema_version":1}', encoding="utf-8")
            output = io.StringIO()
            with mock.patch("sys.stdout", output):
                status = verifier.main([str(broken), str(self.original)])
        self.assertEqual(status, 1)
        results = json.loads(output.getvalue())["results"]
        self.assertEqual([r["status"] for r in results], ["rejected", "verified"])


if __name__ == "__main__":
    unittest.main()
