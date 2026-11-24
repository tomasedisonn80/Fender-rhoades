#!/usr/bin/env python3
"""Read-only, bounded integrity verification of the retained HOST-MIDI archive."""

import argparse
from collections import Counter
from contextlib import contextmanager
import hashlib
import json
import math
import os
from pathlib import Path, PurePosixPath
import re
import stat
import zlib

ROOT = Path(__file__).resolve().parents[1]
MANIFEST = "docs/validation/host-midi-raw-20261124-manifest.json"
ARCHIVE = "docs/validation/host-midi-raw-20261124.tar.gz"
MAX_MANIFEST_BYTES = 2 * 1024 * 1024
MAX_COMPRESSED_BYTES = 16 * 1024 * 1024
MAX_TAR_BYTES = MAX_ORIGINAL_BYTES = 32 * 1024 * 1024
MAX_MEMBER_BYTES = MAX_REPORT_BYTES = 4 * 1024 * 1024
MAX_REFERENCE_BYTES = 16 * 1024 * 1024
MAX_BLOBS, MAX_ENTRIES, MAX_REPORTS = 2048, 4096, 128
CHUNK = 65536
WORKERS = {"worker033", "worker036", "worker038", "worker039"}
ARCHIVE_FIXED = {
    "path": ARCHIVE, "compression": "gzip", "compression_level": 9,
    "format": "ustar", "gzip_filename": "", "gzip_mtime": 0,
    "gzip_header_hex": "1f8b08000000000002ff", "tar_record_size_bytes": 10240,
    "member_mode": "0644", "member_mtime": 0, "member_uid": 0, "member_gid": 0,
    "member_uname": "", "member_gname": "",
    "member_order": "Lexicographic archive_member order (SHA-256 hexadecimal order).",
    "members": "Only regular files; no links, directories, extended headers or embedded manifest.",
}
LIMITATION = (
    "Byte integrity and manifest consistency only; no archived command is executed. "
    "Report experiment claims are not recomputed. Hashes do not authenticate "
    "execution, establish physical fidelity, or prove host/device deadlines."
)


class VerificationError(ValueError):
    """The evidence is malformed, inconsistent, unreadable or over its limits."""


def require(condition, message):
    if not condition:
        raise VerificationError(message)


def same(actual, expected, label):
    # JSON encodings distinguish booleans, integers and floats, including nested counts.
    require(json.dumps(actual, sort_keys=True, allow_nan=False)
            == json.dumps(expected, sort_keys=True, allow_nan=False), label + " mismatch")


def fields(value, specification, label):
    require(type(value) is dict and value.keys() == specification.keys(), label + ": fields differ")
    for key, kind in specification.items():
        require(type(value[key]) is kind, label + ": wrong type for " + key)


def strict_json(raw):
    def pairs(items):
        result = {}
        for key, value in items:
            require(key not in result, "duplicate JSON key: " + key)
            result[key] = value
        return result

    def number(value):
        result = float(value)
        require(math.isfinite(result), "nonfinite JSON number")
        return result

    def constant(value):
        raise VerificationError("nonfinite JSON constant: " + value)

    return json.loads(raw.decode("utf-8"), object_pairs_hook=pairs,
                      parse_float=number, parse_constant=constant)


def safe_path(value):
    require(type(value) is str and re.fullmatch(r"[A-Za-z0-9_.-]+(?:/[A-Za-z0-9_.-]+)*", value)
            and all(p not in (".", "..") for p in value.split("/")), "unsafe path: " + repr(value))
    return PurePosixPath(value)


@contextmanager
def regular_file(root, name, limit):
    relative = safe_path(name)
    path = root
    for part in relative.parts:
        path = path / part
        require(not path.is_symlink(), "symlink input refused: " + name)
    flags = os.O_RDONLY | getattr(os, "O_NONBLOCK", 0) | getattr(os, "O_NOFOLLOW", 0)
    with os.fdopen(os.open(path, flags | getattr(os, "O_BINARY", 0)), "rb") as source:
        before = os.fstat(source.fileno())
        require(stat.S_ISREG(before.st_mode) and before.st_size <= limit,
                "regular file within byte limit required: " + name)
        yield source
        identity = lambda s: (s.st_dev, s.st_ino, s.st_size, s.st_mtime_ns, s.st_ctime_ns)
        require(identity(before) == identity(os.fstat(source.fileno())) == identity(path.stat()),
                "input changed during verification: " + name)


def read_file(root, name, limit):
    with regular_file(root, name, limit) as source:
        raw = source.read(limit + 1)
    require(len(raw) <= limit, "file byte limit exceeded: " + name)
    return raw


class GzipReader:
    """One gzip stream, bounded reads, CRC validation, no concatenation or suffix."""

    def __init__(self, source):
        self.source, self.codec, self.pending = source, zlib.decompressobj(31), b""
        self.compressed_bytes = self.uncompressed_bytes = 0
        self.sha256 = hashlib.sha256()

    def read(self, count):
        result = bytearray()
        while len(result) < count:
            if self.codec.eof:
                require(not self.codec.unused_data and not self.source.read(1), "extra gzip data")
                break
            if not self.pending:
                self.pending = self.source.read(CHUNK)
                require(self.pending, "truncated gzip stream")
                self.compressed_bytes += len(self.pending)
                require(self.compressed_bytes <= MAX_COMPRESSED_BYTES, "compressed byte limit exceeded")
                self.sha256.update(self.pending)
            part = self.codec.decompress(self.pending, min(CHUNK, count - len(result)))
            self.pending = self.codec.unconsumed_tail
            self.uncompressed_bytes += len(part)
            require(self.uncompressed_bytes <= MAX_TAR_BYTES, "uncompressed byte limit exceeded")
            result.extend(part)
        return bytes(result)


def check_tar(source, blobs, archive):
    require(source.read(10).hex() == ARCHIVE_FIXED["gzip_header_hex"], "noncanonical gzip header")
    source.seek(0)
    stream = GzipReader(source)
    for blob in blobs:
        header = stream.read(512)
        require(len(header) == 512, "truncated tar header")
        require(re.fullmatch(rb"[0-7]{11}\x00", header[124:136]), "invalid tar size encoding")
        size = int(header[124:135], 8)
        require(size <= MAX_MEMBER_BYTES, "tar member byte limit exceeded")
        require(size == blob["size_bytes"], "tar member size mismatch")
        require(header[:100] == blob["archive_member"].encode("ascii").ljust(100, b"\0"),
                "unexpected, duplicate, out-of-order or unsafe tar member")
        require(header[100:124] == b"0000644\0" + b"0000000\0" * 2
                and header[136:148] == b"00000000000\0"
                and header[156:265] == b"0" + bytes(100) + b"ustar\00000"
                and header[265:] == bytes(247), "noncanonical regular USTAR metadata")
        require(re.fullmatch(rb"[0-7]{6}\x00 ", header[148:156])
                and int(header[148:154], 8) == sum(header[:148] + b" " * 8 + header[156:]),
                "tar header checksum mismatch")
        digest, remaining = hashlib.sha256(), size
        while remaining:
            block = stream.read(min(CHUNK, remaining))
            require(block, "truncated tar member")
            digest.update(block)
            remaining -= len(block)
        require(digest.hexdigest() == blob["sha256"], "blob SHA-256 mismatch")
        padding = (-size) % 512
        require(stream.read(padding) == bytes(padding), "nonzero or truncated member padding")
    padding = 1024 + (-(stream.uncompressed_bytes + 1024)) % 10240
    require(stream.read(padding) == bytes(padding), "missing tar terminator or extra tar member/data")
    require(stream.read(1) == b"", "extra uncompressed data")
    same(stream.uncompressed_bytes, archive["uncompressed_size_bytes"], "uncompressed size")
    same(stream.compressed_bytes, archive["size_bytes"], "archive size")
    same(stream.sha256.hexdigest(), archive["sha256"], "archive SHA-256")


def fingerprint(record, limit):
    require(type(record["sha256"]) is str and re.fullmatch(r"[0-9a-f]{64}", record["sha256"]),
            "invalid SHA-256")
    require(type(record["size_bytes"]) is int and 0 <= record["size_bytes"] <= limit,
            "invalid size or byte limit exceeded")


def _verify(root):
    root = Path(root).resolve(strict=True)
    raw = read_file(root, MANIFEST, MAX_MANIFEST_BYTES)
    manifest = strict_json(raw)
    fields(manifest, {"schema_version": int, "kind": str, "assignment": str, "scope": str,
        "archive": dict, "summary": dict, "blobs": list, "entries": list, "exclusions": list,
        "referenced_reports": list, "source_context": dict, "report_validation": dict,
        "missing_data": dict, "limitations": list, "reproduction": dict}, "manifest")
    same(manifest["schema_version"], 1, "schema version")
    same(manifest["kind"], "host-midi-raw-evidence-archive", "manifest kind")
    require(manifest["assignment"] and manifest["scope"]
            and all(type(v) is str for v in manifest["limitations"]), "invalid manifest descriptions")
    archive = manifest["archive"]
    fields(archive, {**{k: type(v) for k, v in ARCHIVE_FIXED.items()},
                    "sha256": str, "size_bytes": int, "uncompressed_size_bytes": int}, "archive")
    for key, value in ARCHIVE_FIXED.items():
        same(archive[key], value, "archive " + key)
    fingerprint(archive, MAX_COMPRESSED_BYTES)
    require(0 < archive["uncompressed_size_bytes"] <= MAX_TAR_BYTES, "invalid uncompressed size")
    blobs, entries, exclusions = (manifest[k] for k in ("blobs", "entries", "exclusions"))
    require(0 < len(blobs) <= MAX_BLOBS and 0 < len(entries) <= MAX_ENTRIES
            and len(exclusions) <= MAX_ENTRIES, "inventory count limit exceeded")
    by_name = {}
    for blob in blobs:
        fields(blob, {"archive_member": str, "sha256": str, "size_bytes": int,
                      "original_path_count": int}, "blob")
        fingerprint(blob, MAX_MEMBER_BYTES)
        name = blob["archive_member"]
        require(name == "blobs/sha256/" + blob["sha256"] and name not in by_name
                and 0 < blob["original_path_count"] <= MAX_ENTRIES, "invalid or duplicate blob")
        by_name[name] = blob
    same(list(by_name), sorted(by_name), "blob order")
    tar_size = sum(512 + ((b["size_bytes"] + 511) // 512) * 512 for b in blobs) + 1024
    same((tar_size + 10239) // 10240 * 10240, archive["uncompressed_size_bytes"], "tar layout size")
    paths, references, workers = set(), Counter(), {}
    for entry in entries:
        fields(entry, {"original_path": str, "archive_member": str, "sha256": str,
                       "size_bytes": int, "worker": str, "role": str}, "entry")
        fingerprint(entry, MAX_MEMBER_BYTES)
        path = safe_path(entry["original_path"])
        require(len(path.parts) >= 3 and path.parts[0] == ".agent-work"
                and path.parts[1] == entry["worker"] and entry["worker"] in WORKERS
                and entry["original_path"] not in paths and entry["role"], "invalid original path or worker")
        paths.add(entry["original_path"])
        blob = by_name.get(entry["archive_member"])
        require(blob is not None and (entry["sha256"], entry["size_bytes"])
                == (blob["sha256"], blob["size_bytes"]), "original path/blob mismatch")
        references[entry["archive_member"]] += 1
        worker = workers.setdefault(entry["worker"], {"file_count": 0, "size_bytes": 0,
                                                      "file_suffix_counts": Counter()})
        worker["file_count"] += 1
        worker["size_bytes"] += entry["size_bytes"]
        worker["file_suffix_counts"][path.suffix] += 1
    for name, blob in by_name.items():
        same(references[name], blob["original_path_count"], "blob reference count")
    for excluded in exclusions:
        fields(excluded, {"original_path": str, "sha256": str, "size_bytes": int, "reason": str}, "exclusion")
        fingerprint(excluded, MAX_MEMBER_BYTES)
        path = safe_path(excluded["original_path"])
        require(len(path.parts) >= 3 and path.parts[0] == ".agent-work" and path.parts[1] in WORKERS
                and excluded["original_path"] not in paths and excluded["reason"], "invalid exclusion")
        paths.add(excluded["original_path"])
    original_bytes = sum(e["size_bytes"] for e in entries)
    blob_bytes = sum(b["size_bytes"] for b in blobs)
    excluded_bytes = sum(e["size_bytes"] for e in exclusions)
    require(max(original_bytes, excluded_bytes) <= MAX_ORIGINAL_BYTES, "original inventory byte limit exceeded")
    summary = {"original_file_count": len(entries), "original_bytes": original_bytes,
        "blob_count": len(blobs), "blob_bytes": blob_bytes, "duplicate_original_count": len(entries) - len(blobs),
        "deduplicated_bytes": original_bytes - blob_bytes, "excluded_file_count": len(exclusions),
        "excluded_bytes": excluded_bytes, "workers": workers}
    same(manifest["summary"], summary, "aggregate summary")
    reports, report_paths, report_bytes = manifest["referenced_reports"], set(), 0
    require(len(reports) <= MAX_REPORTS, "report count limit exceeded")
    for report in reports:
        fields(report, {"path": str, "sha256": str, "size_bytes": int, "kind": str}, "report")
        fingerprint(report, MAX_REPORT_BYTES)
        safe_path(report["path"])
        require(report["path"] not in report_paths and report["kind"], "duplicate or invalid report")
        report_paths.add(report["path"])
        report_bytes += report["size_bytes"]
        require(report_bytes <= MAX_REFERENCE_BYTES, "report total byte limit exceeded")
        report_raw = read_file(root, report["path"], MAX_REPORT_BYTES)
        same(len(report_raw), report["size_bytes"], "report size")
        same(hashlib.sha256(report_raw).hexdigest(), report["sha256"], "report SHA-256")
    with regular_file(root, ARCHIVE, MAX_COMPRESSED_BYTES) as source:
        same(os.fstat(source.fileno()).st_size, archive["size_bytes"], "archive file size")
        check_tar(source, blobs, archive)
    return {"status": "verified", "archive_sha256": archive["sha256"],
            "manifest_sha256": hashlib.sha256(raw).hexdigest(), "blob_count": len(blobs),
            "original_file_count": len(entries), "original_bytes": original_bytes,
            "blob_bytes": blob_bytes, "referenced_report_count": len(reports), "limitation": LIMITATION}


def verify_archive(root=ROOT):
    try:
        return _verify(root)
    except VerificationError:
        raise
    except (OSError, ValueError, TypeError, KeyError, OverflowError, RecursionError, zlib.error) as error:
        raise VerificationError("malformed or unreadable evidence: " + str(error)) from error


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, default=ROOT, help="repository or fixture root")
    args = parser.parse_args(argv)
    try:
        result = verify_archive(args.root)
    except VerificationError as error:
        result = {"status": "rejected", "error": str(error), "limitation": LIMITATION}
    print(json.dumps(result, indent=2, allow_nan=False))
    return 0 if result["status"] == "verified" else 1


if __name__ == "__main__":
    raise SystemExit(main())
