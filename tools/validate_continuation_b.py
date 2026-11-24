#!/usr/bin/env python3
"""Read-only integrity checks for the 2026-11-24 continuation-B worker ledger.

Only actually assigned workers belong in the ledger. This tool checks metadata
and report bytes; it cannot establish that a worker ran or that its work passed
scientific acceptance. It never creates assignments, reports, or replacement
hashes. Running records have four fields; completed records add report/sha256.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import math
import os
from pathlib import Path, PurePosixPath
import re
import stat
import sys

ROOT = Path(__file__).resolve().parents[1]
LEDGER = "project/continuation-b-20261124.json"
CAMPAIGN = "continuation-b-20261124"
WAVES = 5
PER_WAVE = 20
MAX_AGENTS = WAVES * PER_WAVE
MAX_LEDGER_BYTES = 256 * 1024
MAX_REPORT_BYTES = 1024 * 1024
LIMITATION = ("Report hashes check byte integrity; they do not prove actual agent "
              "execution, concurrency history, scientific acceptance, or physical fidelity.")


def checked_path(root: Path, relative: str) -> Path:
    """Require a canonical root-relative path without symlinks or reparse points."""
    if type(relative) is not str:
        raise ValueError("path must be a string")
    parts = PurePosixPath(relative)
    if (not relative or not parts.parts or len(relative) > 1024 or
            parts.is_absolute() or parts.as_posix() != relative or
            ".." in parts.parts or "\\" in relative or ":" in relative or
            any(ord(character) < 32 or ord(character) == 127 for character in relative)):
        raise ValueError(f"unsafe root-relative path: {relative!r}")
    path = root
    for part in parts.parts:
        path = path / part
        try:
            info = path.lstat()
        except FileNotFoundError as exc:
            raise ValueError(f"missing regular file: {relative}") from exc
        if (stat.S_ISLNK(info.st_mode) or
                getattr(info, "st_file_attributes", 0) &
                getattr(stat, "FILE_ATTRIBUTE_REPARSE_POINT", 0)):
            raise ValueError(f"symlink or reparse path component: {relative}")
    try:
        path.resolve(strict=True).relative_to(root)
    except (OSError, RuntimeError, ValueError) as exc:
        raise ValueError(f"path escapes root: {relative}") from exc
    return path


def open_readonly(root: Path, path: Path) -> int:
    """Use descriptor-relative no-follow opens where the platform supports them."""
    flags = (os.O_RDONLY | getattr(os, "O_BINARY", 0) |
             getattr(os, "O_NOFOLLOW", 0) | getattr(os, "O_NONBLOCK", 0))
    if (os.open in os.supports_dir_fd and hasattr(os, "O_NOFOLLOW") and
            hasattr(os, "O_DIRECTORY")):
        directory_flags = os.O_RDONLY | os.O_DIRECTORY | os.O_NOFOLLOW
        directory = os.open(root, directory_flags)
        try:
            parts = path.relative_to(root).parts
            for part in parts[:-1]:
                descendant = os.open(part, directory_flags, dir_fd=directory)
                os.close(directory)
                directory = descendant
            return os.open(parts[-1], flags, dir_fd=directory)
        finally:
            os.close(directory)
    return os.open(path, flags)


def read_regular(root: Path, relative: str, limit: int) -> bytes:
    """Bound reads and reject special files, symlinks, and observed file changes."""
    path = checked_path(root, relative)
    expected = path.lstat()
    if not stat.S_ISREG(expected.st_mode):
        raise ValueError(f"not a regular file: {relative}")
    if expected.st_size > limit:
        raise ValueError(f"file exceeds {limit}-byte limit: {relative}")
    with os.fdopen(open_readonly(root, path), "rb") as source:
        before = os.fstat(source.fileno())
        if not stat.S_ISREG(before.st_mode):
            raise ValueError(f"not a regular file: {relative}")
        if (before.st_dev, before.st_ino) != (expected.st_dev, expected.st_ino):
            raise ValueError(f"file changed while opening: {relative}")
        if before.st_size > limit:
            raise ValueError(f"file exceeds {limit}-byte limit: {relative}")
        content = source.read(limit + 1)
        after = os.fstat(source.fileno())
    if len(content) > limit:
        raise ValueError(f"file exceeds {limit}-byte limit: {relative}")
    observed = checked_path(root, relative).lstat()
    identity = lambda info: (info.st_dev, info.st_ino, info.st_size,
                             info.st_mtime_ns, info.st_ctime_ns)
    if (before.st_size != len(content) or identity(before) != identity(after) or
            identity(after) != identity(observed)):
        raise ValueError(f"file changed while reading: {relative}")
    return content


def strict_json(content: bytes) -> object:
    def unique_object(pairs):
        result = {}
        for key, value in pairs:
            if key in result:
                raise ValueError(f"duplicate JSON key: {key}")
            result[key] = value
        return result

    def invalid_number(value):
        raise ValueError(f"nonfinite JSON number: {value}")

    def finite_float(value):
        number = float(value)
        if not math.isfinite(number):
            invalid_number(value)
        return number

    return json.loads(content.decode("utf-8"), object_pairs_hook=unique_object,
                      parse_constant=invalid_number, parse_float=finite_float)


def validate_records(data: object, root: Path, require_complete: bool = False) -> dict:
    expected = {"schema_version": 1, "requested_agents": MAX_AGENTS,
                "requested_waves": WAVES, "concurrency_limit_including_integrator": 4}
    fields = {*expected, "campaign", "baseline_commit", "status", "agents", "completed_agents"}
    if type(data) is not dict or set(data) != fields:
        raise ValueError("invalid continuation-B ledger fields")
    for key, value in expected.items():
        if type(data[key]) is not int or data[key] != value:
            raise ValueError(f"{key} must be the integer {value}")
    if type(data["campaign"]) is not str or data["campaign"] != CAMPAIGN:
        raise ValueError(f"campaign must be {CAMPAIGN}")
    baseline = data["baseline_commit"]
    if type(baseline) is not str or not re.fullmatch(r"[0-9a-f]{40}", baseline):
        raise ValueError("baseline_commit must be a 40-digit lowercase hexadecimal commit ID")
    if type(data["status"]) is not str or data["status"] not in {"in_progress", "complete"}:
        raise ValueError("ledger status must be in_progress or complete")
    records = data["agents"]
    if type(records) is not list or not 0 <= len(records) <= MAX_AGENTS:
        raise ValueError("agents must be a list of at most 100 assignments")
    if (type(data["completed_agents"]) is not int or
            not 0 <= data["completed_agents"] <= len(records)):
        raise ValueError("completed_agents must be an integer within the recorded count")

    names = set()
    completed_by_wave = [0] * WAVES
    base_fields = {"assignment", "wave", "agent_task_name", "status"}
    for number, record in enumerate(records, 1):
        if type(record) is not dict or not base_fields <= set(record):
            raise ValueError(f"assignment {number}: invalid record fields")
        status = record["status"]
        if type(status) is not str or status not in {"running", "completed"}:
            raise ValueError(f"assignment {number}: status must be running or completed")
        fields = base_fields | ({"report", "sha256"} if status == "completed" else set())
        if set(record) != fields:
            raise ValueError(f"assignment {number}: incorrect fields for {status} record")
        if type(record["assignment"]) is not int or record["assignment"] != number:
            raise ValueError(f"assignment {number}: assignments must be consecutive integers 1..N")
        wave = 1 + (number - 1) // PER_WAVE
        if type(record["wave"]) is not int or record["wave"] != wave:
            raise ValueError(f"assignment {number}: incorrect wave; expected {wave}")
        name = record["agent_task_name"]
        if type(name) is not str:
            raise ValueError(f"assignment {number}: agent_task_name must be a string")
        if name in names:
            raise ValueError(f"assignment {number}: duplicate agent_task_name")
        match = re.fullmatch(rf"/root/w{wave}_{number:03d}_([a-z0-9]+(?:_[a-z0-9]+)*)", name)
        if not match or len(match[1]) > 128:
            raise ValueError(f"assignment {number}: incorrect canonical agent_task_name")
        names.add(name)
        if status == "running":
            continue
        slug = match[1].replace("_", "-")
        report = f"docs/agents/reports/20261124b-w{wave}-{number:03d}-{slug}.md"
        if type(record["report"]) is not str or record["report"] != report:
            raise ValueError(f"assignment {number}: incorrect report path; expected {report}")
        digest = record["sha256"]
        if type(digest) is not str or not re.fullmatch(r"[0-9a-f]{64}", digest):
            raise ValueError(f"assignment {number}: sha256 must be 64 lowercase hexadecimal digits")
        content = read_regular(root, report, MAX_REPORT_BYTES)
        try:
            report_text = content.decode("utf-8")
        except UnicodeDecodeError as exc:
            raise ValueError(f"assignment {number}: report must be UTF-8 text") from exc
        if not report_text.strip() or "\x00" in report_text:
            raise ValueError(f"assignment {number}: report must contain nonempty text without NUL bytes")
        if hashlib.sha256(content).hexdigest() != digest:
            raise ValueError(f"assignment {number}: report SHA256 mismatch")
        completed_by_wave[wave - 1] += 1

    completed = sum(completed_by_wave)
    if data["completed_agents"] != completed:
        raise ValueError("completed_agents does not match completed records")
    complete = completed_by_wave == [PER_WAVE] * WAVES
    expected_status = "complete" if complete else "in_progress"
    if data["status"] != expected_status:
        raise ValueError(f"ledger status must be {expected_status} for these completed counts")
    if require_complete and not complete:
        raise ValueError("campaign not complete: require 100 completed reports, 20 in each of five waves")
    return {"schema_version": 1, "campaign": CAMPAIGN, "status": data["status"],
            "recorded_agents": len(records), "completed_agents": completed,
            "completed_by_wave": completed_by_wave,
            "completed_waves": sum(count == PER_WAVE for count in completed_by_wave),
            "limitation": LIMITATION}


def validate(root: Path, require_complete: bool = False, ledger: str = LEDGER) -> dict:
    root = root.resolve(strict=True)
    if not root.is_dir():
        raise ValueError("root must be a directory")
    content = read_regular(root, ledger, MAX_LEDGER_BYTES)
    result = validate_records(strict_json(content), root, require_complete)
    result["ledger_sha256"] = hashlib.sha256(content).hexdigest()
    return result


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, default=ROOT, help="repository or fixture root")
    parser.add_argument("--ledger", default=LEDGER,
                        help="canonical POSIX ledger path relative to --root")
    parser.add_argument("--require-complete", action="store_true",
                        help="require 100 completed reports, 20 in each of five waves")
    parser.add_argument("--json", action="store_true", help="print the integrity summary as JSON")
    args = parser.parse_args(argv)
    try:
        result = validate(args.root, args.require_complete, args.ledger)
    except (OSError, ValueError, RecursionError, RuntimeError) as exc:
        print(f"Continuation B error: {exc}", file=sys.stderr)
        return 1
    if args.json:
        print(json.dumps(result, indent=2, allow_nan=False))
    else:
        print(f"Continuation B metadata valid: {result['recorded_agents']}/100 recorded; "
              f"{result['completed_agents']}/100 completed reports; "
              f"{result['completed_waves']}/5 completed waves.")
        print(LIMITATION)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
