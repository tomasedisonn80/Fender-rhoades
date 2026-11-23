#!/usr/bin/env python3
"""Opt-in local payload integrity checks; Python standard library only.

Paths in the manifest's nonempty artifacts list are relative to --payload-root.
Only that list is validated here; use validate_data.py for session/profile rules.
Inputs are read, never executed, fetched, imported, or rewritten. A successful
check establishes byte agreement, not authenticity, calibration, or fidelity.

Use trusted, quiescent local directories. Symlink/reparse components are rejected;
descriptor and pathname checks detect ordinary concurrent changes. This is not
hardened against adversarial directory races on every platform, a filesystem
snapshot, or a guarantee about bytes after inspection. Report publication requires
same-filesystem hard-link support and never falls back to a partial final file.
"""
from __future__ import annotations

import argparse
from datetime import datetime, timezone
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import re
import stat
import sys
import tempfile

VERSION = "1.0.1"
CHUNK_SIZE = 1024 * 1024

# File-based imports also work when tests load this script without changing sys.path.
SPEC = importlib.util.spec_from_file_location("payload_data_contracts", Path(__file__).with_name("validate_data.py"))
assert SPEC and SPEC.loader
contracts = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(contracts)
DataError = contracts.DataError
require = contracts.require


def local_path(value: str | Path) -> Path:
    raw = os.fspath(value)
    windows_drive = os.name == "nt" and re.match(r"^[A-Za-z]:[\\/]", raw)
    require(not re.match(r"^[A-Za-z][A-Za-z0-9+.-]*:", raw) or windows_drive,
            "local filesystem paths required; URLs are not supported")
    require(bool(raw) and not any(ord(c) < 32 or ord(c) == 127 for c in raw),
            "local path is empty or contains control characters")
    # Do not normalize '..' across a possible symlink before checking components.
    return Path(raw).absolute()


def checked_stat(path: Path) -> os.stat_result:
    """lstat every component, including the filesystem anchor and supplied root."""
    current = Path(path.anchor)
    components = [current]
    for part in path.parts[1:]:
        current = current / part
        components.append(current)
    for index, component in enumerate(components):
        info = component.lstat()
        reparse = getattr(info, "st_file_attributes", 0) & getattr(stat, "FILE_ATTRIBUTE_REPARSE_POINT", 0)
        require(not stat.S_ISLNK(info.st_mode) and not reparse,
                f"symlink/reparse component rejected: {component}")
        if index < len(components) - 1:
            require(stat.S_ISDIR(info.st_mode), f"path component is not a directory: {component}")
    return info


def identity(info: os.stat_result) -> tuple[int, int]:
    return info.st_dev, info.st_ino


def fingerprint(info: os.stat_result) -> tuple[int, ...]:
    # Access times can change simply from reading; they are deliberately excluded.
    return (*identity(info), info.st_mode, info.st_size, info.st_mtime_ns, info.st_ctime_ns)


def scan_regular(path: Path, *, capture: bool = False) -> tuple[dict, bytes]:
    """Stream one regular file from one descriptor, retaining partial observations."""
    observed = {"bytes": None, "sha256": None, "read_complete": False, "failures": []}
    chunks = []
    try:
        path_before = checked_stat(path)
        require(stat.S_ISREG(path_before.st_mode), f"regular file required: {path}")
        flags = os.O_RDONLY | getattr(os, "O_BINARY", 0) | getattr(os, "O_NOFOLLOW", 0)
        # Avoid hanging if a regular file is replaced by a FIFO between lstat/open.
        flags |= getattr(os, "O_NONBLOCK", 0)
        descriptor = os.open(path, flags)
        try:
            before = os.fstat(descriptor)
            require(stat.S_ISREG(before.st_mode), f"opened object is not a regular file: {path}")
            require(fingerprint(before) == fingerprint(path_before), f"file changed before reading: {path}")
            digest, count = hashlib.sha256(), 0
            try:
                while chunk := os.read(descriptor, CHUNK_SIZE):
                    digest.update(chunk)
                    count += len(chunk)
                    if capture:
                        chunks.append(chunk)
                observed["read_complete"] = True
            finally:
                observed.update(bytes=count, sha256=digest.hexdigest())
            after = os.fstat(descriptor)
            path_after = checked_stat(path)
            require(fingerprint(before) == fingerprint(after), f"file changed while reading: {path}")
            require(fingerprint(after) == fingerprint(path_after), f"pathname changed while reading: {path}")
            require(count == after.st_size, f"read count differs from file size: {path}")
        finally:
            os.close(descriptor)
    except (DataError, OSError, ValueError) as exc:
        observed["failures"].append(str(exc))
    return observed, b"".join(chunks)


def parse_manifest(raw: bytes) -> dict:
    """Strict JSON over the exact bytes hashed, without reopening the manifest.

    strict_load accepts only a pathname, so use its duplicate/nonfinite policy
    here and reuse the shared numeric and artifact validators.
    """
    def pairs(items):
        result = {}
        for key, value in items:
            require(key not in result, f"duplicate JSON key: {key}")
            result[key] = value
        return result

    def invalid(value):
        raise DataError(f"nonfinite JSON constant: {value}")

    try:
        data = json.loads(raw.decode("utf-8"), object_pairs_hook=pairs, parse_constant=invalid)
        require(isinstance(data, dict), "manifest: object required")
        pending = [data]
        while pending:
            value = pending.pop()
            if type(value) in (int, float):
                contracts.number(value, "JSON number")
            elif isinstance(value, dict):
                pending.extend(value.values())
            elif isinstance(value, list):
                pending.extend(value)
        return data
    except (UnicodeError, ValueError, RecursionError) as exc:
        raise DataError(f"invalid manifest: {exc}") from exc


def artifacts_from(manifest: dict) -> list[dict]:
    artifacts = manifest.get("artifacts")
    require(isinstance(artifacts, list) and artifacts, "manifest artifacts: nonempty list required")
    seen = set()
    for artifact in artifacts:
        contracts.keys(artifact, {"path", "sha256", "bytes"}, "artifact")
        contracts.safe_relative(artifact["path"])
        require(artifact["path"] not in seen, "duplicate artifact path")
        seen.add(artifact["path"])
        require(isinstance(artifact["sha256"], str) and re.fullmatch(r"[0-9a-f]{64}", artifact["sha256"]),
                "artifact: lowercase SHA-256 required")
        require(type(artifact["bytes"]) is int and artifact["bytes"] > 0,
                "artifact: positive integer byte count required")
    return artifacts


def check_root(root: Path, expected: tuple[int, int]) -> None:
    info = checked_stat(root)
    require(stat.S_ISDIR(info.st_mode) and identity(info) == expected,
            f"payload root changed: {root}")


def verify_artifact(root: Path, root_identity: tuple[int, int], artifact: dict) -> dict:
    path = root / artifact["path"]
    result = {"path": artifact["path"], "location": str(path),
              "expected": {"bytes": artifact["bytes"], "sha256": artifact["sha256"]},
              "observed": {"bytes": None, "sha256": None, "read_complete": False},
              "failures": []}
    try:
        check_root(root, root_identity)
        checked_stat(path)
        require(path.resolve(strict=True).is_relative_to(root), "artifact escapes payload root")
        observed, _ = scan_regular(path)
        result["failures"].extend(observed.pop("failures"))
        result["observed"] = observed
        check_root(root, root_identity)
    except (DataError, OSError, ValueError) as exc:
        result["failures"].append(str(exc))
    if result["observed"]["read_complete"]:
        for field in ("bytes", "sha256"):
            if result["observed"][field] != result["expected"][field]:
                result["failures"].append(f"{field} mismatch")
    result["result"] = "fail" if result["failures"] else "pass"
    return result


def report_target(path: Path, root: Path, manifest: Path) -> tuple[Path, tuple[int, int]]:
    parent = checked_stat(path.parent)
    require(stat.S_ISDIR(parent.st_mode), "report parent must be an existing directory")
    target = path.parent.resolve(strict=True) / path.name
    require(not target.is_relative_to(root), "report must be outside payload root")
    require(target != Path(os.path.abspath(manifest)), "report must not alias manifest")
    # lstat also catches dangling symlinks; refusing every existing object covers
    # hard-link aliases of inputs, too. Publication rechecks absence atomically.
    try:
        target.lstat()
    except FileNotFoundError:
        pass
    else:
        raise DataError(f"report already exists; refusing overwrite: {target}")
    return target, identity(parent)


def publish_report(target: Path, parent_identity: tuple[int, int], report: dict) -> None:
    require(identity(checked_stat(target.parent)) == parent_identity, "report parent changed")
    descriptor, temporary = tempfile.mkstemp(prefix=".payload-report-", suffix=".tmp", dir=target.parent)
    try:
        with os.fdopen(descriptor, "w", encoding="utf-8", newline="\n") as stream:
            json.dump(report, stream, indent=2, sort_keys=True, allow_nan=False)
            stream.write("\n")
            stream.flush()
            os.fsync(stream.fileno())
        require(identity(checked_stat(target.parent)) == parent_identity, "report parent changed")
        # Unlike replace/rename, link cannot overwrite an existing destination.
        os.link(temporary, target)
    finally:
        try:
            Path(temporary).unlink(missing_ok=True)
        except OSError as exc:
            # Publication has already succeeded if link returned. A leftover
            # temporary hard link must not turn that result into a false failure
            # or mask an earlier write/link error during unsuccessful publication.
            print(f"Payload report temporary cleanup failed: {temporary}: {exc}", file=sys.stderr)


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--manifest", required=True, help="local JSON manifest")
    parser.add_argument("--payload-root", required=True, help="explicit local artifact root")
    parser.add_argument("--report", required=True, help="new report outside the payload root; parent must exist")
    args = parser.parse_args(argv)
    try:
        manifest, root, requested_report = (local_path(p) for p in (args.manifest, args.payload_root, args.report))
        root_info = checked_stat(root)
        require(stat.S_ISDIR(root_info.st_mode), "payload root must be a directory")
        root = root.resolve(strict=True)
        root_identity = identity(root_info)
        check_root(root, root_identity)
        target, parent_identity = report_target(requested_report, root, manifest)
        observed, raw = scan_regular(manifest, capture=True)
        report = {"schema_version": 1, "tool": {"name": "verify_payloads", "version": VERSION},
                  "manifest": {"path": str(manifest), **observed}, "payload_root": str(root),
                  "scope": "Byte integrity only; no authenticity, calibration, or physical fidelity claim.",
                  "concurrency": "Trusted, quiescent local directories; not hardened against adversarial directory races on every platform.",
                  "artifacts": [], "failures": list(observed["failures"])}
        if not report["failures"]:
            try:
                artifacts = artifacts_from(parse_manifest(raw))
                report["artifacts"] = [verify_artifact(root, root_identity, item) for item in artifacts]
                check_root(root, root_identity)
            except (DataError, OSError, ValueError) as exc:
                report["failures"].append(str(exc))
        failed = sum(item["result"] == "fail" for item in report["artifacts"])
        report["result"] = "fail" if report["failures"] or failed else "pass"
        report["checked_at"] = datetime.now(timezone.utc).isoformat().replace("+00:00", "Z")
        publish_report(target, parent_identity, report)
        print(f"Payload integrity {report['result']}: {len(report['artifacts'])} artifact(s), {failed} failed; report: {target}")
        return 1 if report["result"] == "fail" else 0
    except (DataError, OSError, ValueError) as exc:
        print(f"Payload verification could not publish a report: {exc}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
