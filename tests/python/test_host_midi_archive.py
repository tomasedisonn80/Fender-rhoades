"""Small independent fixtures for the read-only HOST MIDI raw-archive verifier.

No test opens the retained production archive.  The fixture contains two tiny
content blobs, three original paths, one exclusion and one referenced report.
The stdlib tar/gzip writer is deliberately independent of the verifier's reader.
"""
from __future__ import annotations

from collections import Counter
from contextlib import redirect_stdout
import gzip
import hashlib
import io
import json
import os
from pathlib import Path
import stat
import sys
import tarfile
import tempfile
import unittest
from unittest import mock

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools"))
import verify_host_midi_archive as verifier


MANIFEST = "docs/validation/host-midi-raw-20261124-manifest.json"
ARCHIVE = "docs/validation/host-midi-raw-20261124.tar.gz"
REPORT = "docs/agents/reports/fixture.md"


def sha256(raw):
    return hashlib.sha256(raw).hexdigest()


def gzip_bytes(raw):
    target = io.BytesIO()
    with gzip.GzipFile(filename="", mode="wb", fileobj=target,
                       compresslevel=9, mtime=0) as stream:
        stream.write(raw)
    return target.getvalue()


def tar_bytes(members):
    """members are (name, payload, TarInfo overrides), in supplied order."""
    target = io.BytesIO()
    with tarfile.open(fileobj=target, mode="w", format=tarfile.USTAR_FORMAT) as archive:
        for name, payload, fields in members:
            member = tarfile.TarInfo(name)
            member.size = len(payload)
            member.mode = 0o644
            for key, value in fields.items():
                setattr(member, key, value)
            archive.addfile(member, io.BytesIO(payload))
    return target.getvalue()


def tree_snapshot(root):
    """Observe names, types, bytes and metadata without following symlinks.

    Access times are intentionally absent: ordinary read-only verification may
    update atime.  mtime, ctime, permissions and symlink destinations are bound.
    """
    result = {}
    for path in sorted((root, *root.rglob("*"))):
        info = path.lstat()
        content = None
        if stat.S_ISREG(info.st_mode):
            content = sha256(path.read_bytes())
        elif stat.S_ISLNK(info.st_mode):
            content = os.readlink(path)
        result[str(path.relative_to(root))] = (
            info.st_mode, info.st_size, info.st_mtime_ns, info.st_ctime_ns, content)
    return result


class TinyArchive:
    def __init__(self, root):
        self.root = root
        self.manifest_path = root / MANIFEST
        self.archive_path = root / ARCHIVE
        self.report_path = root / REPORT
        self.manifest_path.parent.mkdir(parents=True)
        self.report_path.parent.mkdir(parents=True)
        report = b"# Synthetic fixture report\n"
        self.report_path.write_bytes(report)
        originals = [
            (".agent-work/worker033/first.txt", b"alpha\n", "worker033"),
            (".agent-work/worker033/second.txt", b"beta record\n", "worker033"),
            (".agent-work/worker036/repeated.txt", b"alpha\n", "worker036"),
        ]
        payloads = {sha256(raw): raw for _, raw, _ in originals}
        self.members = [("blobs/sha256/" + digest, payloads[digest], {})
                        for digest in sorted(payloads)]
        counts = Counter(sha256(raw) for _, raw, _ in originals)
        self.manifest = {
            "schema_version": 1,
            "kind": "host-midi-raw-evidence-archive",
            "assignment": "unit-test-fixture",
            "scope": "Synthetic byte-integrity fixture; no acquisition claim.",
            "archive": {
                "path": ARCHIVE,
                "compression": "gzip",
                "compression_level": 9,
                "format": "ustar",
                "gzip_filename": "",
                "gzip_header_hex": "1f8b08000000000002ff",
                "gzip_mtime": 0,
                "member_gid": 0,
                "member_gname": "",
                "member_mode": "0644",
                "member_mtime": 0,
                "member_order": "Lexicographic archive_member order (SHA-256 hexadecimal order).",
                "member_uid": 0,
                "member_uname": "",
                "members": "Only regular files; no links, directories, extended headers or embedded manifest.",
                "tar_record_size_bytes": 10240,
            },
            "blobs": [{"archive_member": name, "sha256": sha256(raw),
                       "size_bytes": len(raw), "original_path_count": counts[sha256(raw)]}
                      for name, raw, _ in self.members],
            "entries": [{"original_path": path, "archive_member": "blobs/sha256/" + sha256(raw),
                         "sha256": sha256(raw), "size_bytes": len(raw),
                         "worker": worker, "role": "synthetic-output"}
                        for path, raw, worker in originals],
            "exclusions": [{"original_path": ".agent-work/worker033/unused.bin",
                            "sha256": sha256(b"excluded"), "size_bytes": 8,
                            "reason": "Synthetic excluded bytes are not archived."}],
            "referenced_reports": [{"path": REPORT, "sha256": sha256(report),
                                    "size_bytes": len(report), "kind": "fixture-report"}],
            "limitations": ["Synthetic fixture only."],
            "missing_data": {},
            "report_validation": {},
            "reproduction": {},
            "source_context": {},
        }
        self.recount()
        self.write_tar(tar_bytes(self.members))

    def recount(self):
        entries = self.manifest["entries"]
        blobs = self.manifest["blobs"]
        exclusions = self.manifest["exclusions"]
        workers = {}
        for worker in sorted({item["worker"] for item in entries}):
            selected = [item for item in entries if item["worker"] == worker]
            workers[worker] = {
                "file_count": len(selected),
                "size_bytes": sum(item["size_bytes"] for item in selected),
                "file_suffix_counts": dict(Counter(Path(item["original_path"]).suffix
                                                    for item in selected)),
            }
        original_bytes = sum(item["size_bytes"] for item in entries)
        blob_bytes = sum(item["size_bytes"] for item in blobs)
        self.manifest["summary"] = {
            "blob_bytes": blob_bytes, "blob_count": len(blobs),
            "deduplicated_bytes": original_bytes - blob_bytes,
            "duplicate_original_count": len(entries) - len(blobs),
            "excluded_bytes": sum(item["size_bytes"] for item in exclusions),
            "excluded_file_count": len(exclusions),
            "original_bytes": original_bytes, "original_file_count": len(entries),
            "workers": workers,
        }

    def save_manifest(self):
        self.manifest_path.write_text(json.dumps(self.manifest, sort_keys=True), encoding="utf-8")

    def write_compressed(self, raw, uncompressed_size):
        self.archive_path.write_bytes(raw)
        self.manifest["archive"].update(
            sha256=sha256(raw), size_bytes=len(raw), uncompressed_size_bytes=uncompressed_size)
        self.save_manifest()

    def write_tar(self, raw):
        self.write_compressed(gzip_bytes(raw), len(raw))


class HostMidiArchive(unittest.TestCase):
    def fixture(self):
        temporary = tempfile.TemporaryDirectory()
        self.addCleanup(temporary.cleanup)
        parent = Path(temporary.name)
        return parent, TinyArchive(parent / "repo")

    def rejected(self, parent, fixture, pattern=None):
        before = tree_snapshot(parent)
        expectation = (self.assertRaisesRegex(verifier.VerificationError, pattern)
                       if pattern else self.assertRaises(verifier.VerificationError))
        try:
            with expectation:
                verifier.verify_archive(fixture.root)
        finally:
            self.assertEqual(tree_snapshot(parent), before, "failed verification modified its inputs")

    def test_tiny_archive_recomputes_counts_and_is_read_only(self):
        parent, fixture = self.fixture()
        before = tree_snapshot(parent)
        with mock.patch("subprocess.run", side_effect=AssertionError("must not execute")), \
             mock.patch("subprocess.Popen", side_effect=AssertionError("must not execute")), \
             mock.patch.object(tarfile.TarFile, "extractall", side_effect=AssertionError("must not extract")), \
             mock.patch.object(tarfile.TarFile, "extract", side_effect=AssertionError("must not extract")):
            result = verifier.verify_archive(fixture.root)
        self.assertEqual(result["status"], "verified")
        self.assertEqual(result["blob_count"], 2)
        self.assertEqual(result["original_file_count"], 3)
        self.assertEqual(result["blob_bytes"], 18)
        self.assertEqual(result["original_bytes"], 24)
        self.assertEqual(result["referenced_report_count"], 1)
        self.assertEqual(result["archive_sha256"], sha256(fixture.archive_path.read_bytes()))
        self.assertFalse((fixture.root / ".agent-work").exists())
        self.assertEqual(tree_snapshot(parent), before)

    def test_cli_root_success_and_failure_are_read_only(self):
        parent, fixture = self.fixture()
        for invalid in (False, True):
            with self.subTest(invalid=invalid):
                if invalid:
                    fixture.manifest["schema_version"] = True
                    fixture.save_manifest()
                before = tree_snapshot(parent)
                output = io.StringIO()
                with redirect_stdout(output):
                    status = verifier.main(["--root", str(fixture.root)])
                self.assertEqual(status, 1 if invalid else 0)
                result = json.loads(output.getvalue())
                self.assertEqual(result["status"], "rejected" if invalid else "verified")
                self.assertEqual(tree_snapshot(parent), before)

    def test_compressed_byte_corruption_requires_matching_digest(self):
        parent, fixture = self.fixture()
        raw = bytearray(fixture.archive_path.read_bytes())
        raw[len(raw) // 2] ^= 1
        fixture.archive_path.write_bytes(raw)
        self.rejected(parent, fixture)

    def test_archive_size_digest_and_uncompressed_size_are_checked(self):
        for field, value in (("size_bytes", 1), ("sha256", "0" * 64),
                             ("uncompressed_size_bytes", 20480)):
            with self.subTest(field=field):
                parent, fixture = self.fixture()
                fixture.manifest["archive"][field] = value
                fixture.save_manifest()
                self.rejected(parent, fixture)

    def test_payload_corruption_is_detected_after_outer_hash_is_rebound(self):
        parent, fixture = self.fixture()
        name, raw, fields = fixture.members[0]
        fixture.members[0] = (name, bytes([raw[0] ^ 1]) + raw[1:], fields)
        fixture.write_tar(tar_bytes(fixture.members))
        self.rejected(parent, fixture, "blob.*SHA-256")

    def test_tar_payload_size_is_checked_after_manifest_counts_are_rebound(self):
        parent, fixture = self.fixture()
        blob = fixture.manifest["blobs"][0]
        blob["size_bytes"] += 1
        for entry in fixture.manifest["entries"]:
            if entry["sha256"] == blob["sha256"]:
                entry["size_bytes"] += 1
        fixture.recount()
        fixture.save_manifest()
        self.rejected(parent, fixture, "tar member size")

    def test_original_mapping_and_blob_reference_count_are_checked(self):
        for field, value in (("archive_member", "blobs/sha256/" + "0" * 64),
                             ("sha256", "0" * 64), ("size_bytes", 999),
                             ("worker", "worker036")):
            with self.subTest(field=field):
                parent, fixture = self.fixture()
                fixture.manifest["entries"][0][field] = value
                fixture.save_manifest()
                self.rejected(parent, fixture)
        parent, fixture = self.fixture()
        fixture.manifest["blobs"][0]["original_path_count"] += 1
        fixture.save_manifest()
        self.rejected(parent, fixture)

    def test_every_summary_total_is_recomputed(self):
        for field in ("blob_bytes", "blob_count", "deduplicated_bytes",
                      "duplicate_original_count", "excluded_bytes", "excluded_file_count",
                      "original_bytes", "original_file_count"):
            with self.subTest(field=field):
                parent, fixture = self.fixture()
                fixture.manifest["summary"][field] += 1
                fixture.save_manifest()
                self.rejected(parent, fixture)

    def test_worker_counts_bytes_and_suffixes_are_recomputed(self):
        for field, value in (("file_count", 99), ("size_bytes", 99),
                             ("file_suffix_counts", {".bin": 2})):
            with self.subTest(field=field):
                parent, fixture = self.fixture()
                fixture.manifest["summary"]["workers"]["worker033"][field] = value
                fixture.save_manifest()
                self.rejected(parent, fixture)

    def test_duplicate_inventory_records_are_rejected(self):
        for section in ("blobs", "entries", "exclusions", "referenced_reports"):
            with self.subTest(section=section):
                parent, fixture = self.fixture()
                fixture.manifest[section].append(dict(fixture.manifest[section][0]))
                fixture.save_manifest()
                self.rejected(parent, fixture)

    def test_excluded_path_cannot_also_be_an_archived_original(self):
        parent, fixture = self.fixture()
        fixture.manifest["exclusions"][0]["original_path"] = fixture.manifest["entries"][0]["original_path"]
        fixture.save_manifest()
        self.rejected(parent, fixture)

    def test_missing_unknown_duplicate_and_unordered_tar_members_are_rejected(self):
        for alteration in ("missing", "unknown", "duplicate", "unordered"):
            with self.subTest(alteration=alteration):
                parent, fixture = self.fixture()
                members = list(fixture.members)
                if alteration == "missing":
                    members.pop()
                elif alteration == "unknown":
                    members.append(("blobs/sha256/" + sha256(b"extra"), b"extra", {}))
                    members.sort(key=lambda item: item[0])
                elif alteration == "duplicate":
                    members.insert(0, members[0])
                else:
                    members.reverse()
                fixture.write_tar(tar_bytes(members))
                self.rejected(parent, fixture)

    def test_nonregular_and_extension_tar_types_are_rejected(self):
        for kind in (tarfile.SYMTYPE, tarfile.LNKTYPE, tarfile.DIRTYPE,
                     tarfile.FIFOTYPE, tarfile.CHRTYPE, tarfile.XHDTYPE,
                     tarfile.XGLTYPE, tarfile.GNUTYPE_LONGNAME):
            with self.subTest(kind=kind):
                parent, fixture = self.fixture()
                name, raw, _ = fixture.members[0]
                fixture.members[0] = (name, raw, {"type": kind})
                fixture.write_tar(tar_bytes(fixture.members))
                # tarfile also appends '/' to directory names, so that case
                # legitimately fails the member-name check before type metadata.
                diagnostic = "tar member" if kind == tarfile.DIRTYPE else "regular.*USTAR|USTAR.*metadata"
                self.rejected(parent, fixture, diagnostic)

    def test_canonical_tar_metadata_is_checked(self):
        for field, value in (("mode", 0o755), ("uid", 1), ("gid", 1),
                             ("mtime", 1), ("uname", "user"), ("gname", "group"),
                             ("linkname", "hidden-target")):
            with self.subTest(field=field):
                parent, fixture = self.fixture()
                name, raw, _ = fixture.members[0]
                fixture.members[0] = (name, raw, {field: value})
                fixture.write_tar(tar_bytes(fixture.members))
                self.rejected(parent, fixture)

    def test_tar_checksum_and_payload_padding_are_checked(self):
        for alteration in ("checksum", "padding"):
            with self.subTest(alteration=alteration):
                parent, fixture = self.fixture()
                raw = bytearray(tar_bytes(fixture.members))
                offset = 148 if alteration == "checksum" else 512 + len(fixture.members[0][1])
                raw[offset] ^= 1
                fixture.write_tar(raw)
                self.rejected(parent, fixture, alteration)

    def test_oversized_advertised_member_is_rejected_without_large_payload(self):
        parent, fixture = self.fixture()
        header = tarfile.TarInfo(fixture.members[0][0])
        header.mode = 0o644
        header.size = 4 * 1024 * 1024 + 1
        raw = header.tobuf(format=tarfile.USTAR_FORMAT) + bytes(10240 - 512)
        fixture.write_tar(raw)
        self.assertLess(fixture.archive_path.stat().st_size, 1024)
        self.rejected(parent, fixture, "member.*limit")

    def test_gzip_header_metadata_and_truncation_are_checked(self):
        for alteration in ("mtime", "os", "truncated"):
            with self.subTest(alteration=alteration):
                parent, fixture = self.fixture()
                raw = bytearray(fixture.archive_path.read_bytes())
                if alteration == "truncated":
                    del raw[-4:]
                else:
                    raw[4 if alteration == "mtime" else 9] ^= 1
                fixture.write_compressed(raw, 10240)
                self.rejected(parent, fixture)

    def test_concatenated_gzip_members_and_compressed_trailing_bytes_are_rejected(self):
        for tail in (gzip_bytes(b""), gzip_bytes(bytes(10240)), b"hidden"):
            with self.subTest(tail_size=len(tail)):
                parent, fixture = self.fixture()
                fixture.write_compressed(fixture.archive_path.read_bytes() + tail, 10240)
                self.rejected(parent, fixture, "extra gzip data")

    def test_nonzero_trailing_tar_data_is_rejected(self):
        parent, fixture = self.fixture()
        raw = bytearray(tar_bytes(fixture.members))
        raw[-1] = 1
        fixture.write_tar(raw)
        self.rejected(parent, fixture)

    def test_path_traversal_and_platform_ambiguous_manifest_paths_are_rejected(self):
        bad_paths = ("../outside", "/absolute", "C:/outside", "C:outside",
                     ".agent-work/worker033/../outside", ".agent-work//worker033/file",
                     ".agent-work/worker033\\file")
        for section, field in (("entries", "original_path"), ("exclusions", "original_path"),
                               ("referenced_reports", "path")):
            for path in bad_paths:
                with self.subTest(section=section, path=path):
                    parent, fixture = self.fixture()
                    fixture.manifest[section][0][field] = path
                    fixture.save_manifest()
                    self.rejected(parent, fixture)

    def test_tar_member_paths_cannot_escape_or_use_aliases(self):
        for name in ("../outside", "/absolute", "blobs/sha256/../outside",
                     "blobs\\sha256\\outside", "./blobs/sha256/outside"):
            with self.subTest(name=name):
                parent, fixture = self.fixture()
                _, raw, fields = fixture.members[0]
                fixture.members[0] = (name, raw, fields)
                fixture.write_tar(tar_bytes(fixture.members))
                self.rejected(parent, fixture)

    def test_boolean_and_float_values_are_not_integer_metadata(self):
        cases = (("schema_version", None, True), ("schema_version", None, 1.0),
                 ("archive", "gzip_mtime", False), ("archive", "compression_level", 9.0),
                 ("summary", "blob_count", 2.0), ("blobs", "original_path_count", True),
                 ("entries", "size_bytes", True), ("exclusions", "size_bytes", False),
                 ("referenced_reports", "size_bytes", True))
        for section, field, value in cases:
            with self.subTest(section=section, field=field, value=value):
                parent, fixture = self.fixture()
                if field is None:
                    fixture.manifest[section] = value
                else:
                    target = fixture.manifest[section]
                    (target[0] if isinstance(target, list) else target)[field] = value
                fixture.save_manifest()
                self.rejected(parent, fixture)

    def test_duplicate_json_keys_are_rejected_even_when_values_agree(self):
        for nested in (False, True):
            with self.subTest(nested=nested):
                parent, fixture = self.fixture()
                raw = fixture.manifest_path.read_bytes()
                if nested:
                    raw = raw.replace(b'"source_context": {}',
                                      b'"source_context": {"same": 1, "same": 1}', 1)
                else:
                    raw = b'{"schema_version": 1, ' + raw[1:]
                fixture.manifest_path.write_bytes(raw)
                self.rejected(parent, fixture)

    def test_nonfinite_json_is_rejected_in_uninterpreted_metadata(self):
        for value in (b"NaN", b"Infinity", b"-Infinity", b"1e999"):
            with self.subTest(value=value):
                parent, fixture = self.fixture()
                raw = fixture.manifest_path.read_bytes().replace(
                    b'"source_context": {}', b'"source_context": {"value": ' + value + b'}', 1)
                fixture.manifest_path.write_bytes(raw)
                self.rejected(parent, fixture)

    def test_unknown_missing_and_malformed_schema_fields_are_rejected(self):
        for alteration in ("unknown-root", "missing-root", "unknown-blob", "wrong-entries-type"):
            with self.subTest(alteration=alteration):
                parent, fixture = self.fixture()
                if alteration == "unknown-root":
                    fixture.manifest["unexpected"] = 1
                elif alteration == "missing-root":
                    fixture.manifest.pop("summary")
                elif alteration == "unknown-blob":
                    fixture.manifest["blobs"][0]["unexpected"] = 1
                else:
                    fixture.manifest["entries"] = {}
                fixture.save_manifest()
                self.rejected(parent, fixture)

    def test_referenced_report_content_size_and_presence_are_bound(self):
        for alteration in ("content", "size", "missing"):
            with self.subTest(alteration=alteration):
                parent, fixture = self.fixture()
                raw = fixture.report_path.read_bytes()
                if alteration == "content":
                    fixture.report_path.write_bytes(b"!" + raw[1:])
                elif alteration == "size":
                    fixture.report_path.write_bytes(raw + b"x")
                else:
                    fixture.report_path.unlink()
                self.rejected(parent, fixture)

    def test_file_symlinks_are_rejected_without_touching_targets(self):
        for field in ("manifest_path", "archive_path", "report_path"):
            with self.subTest(field=field):
                parent, fixture = self.fixture()
                path = getattr(fixture, field)
                target = parent / "outside-target"
                path.rename(target)
                try:
                    path.symlink_to(target)
                except (OSError, NotImplementedError) as error:
                    self.skipTest("symlinks unavailable: " + str(error))
                self.rejected(parent, fixture)

    def test_symlinked_parent_directory_is_rejected(self):
        parent, fixture = self.fixture()
        directory = fixture.root / "docs" / "validation"
        target = parent / "outside-directory"
        directory.rename(target)
        try:
            directory.symlink_to(target, target_is_directory=True)
        except (OSError, NotImplementedError) as error:
            self.skipTest("symlinks unavailable: " + str(error))
        self.rejected(parent, fixture)

    def test_directory_cannot_replace_a_regular_input_file(self):
        for field in ("manifest_path", "archive_path", "report_path"):
            with self.subTest(field=field):
                parent, fixture = self.fixture()
                path = getattr(fixture, field)
                path.unlink()
                path.mkdir()
                self.rejected(parent, fixture)


if __name__ == "__main__":
    unittest.main()
