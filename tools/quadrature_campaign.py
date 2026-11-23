#!/usr/bin/env python3
"""Validate recorded quadrature workers and regenerate their report index.

Only the integrator records returned assignments in the existing ledger. This
tool never launches workers, registers assignments, changes report hashes, or
attests that recorded work actually happened. Matching metadata and bytes does
not establish scientific acceptance or physical fidelity.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import math
import os
from pathlib import Path, PurePosixPath
import re
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[1]
LEDGER = "project/quadrature-campaign-20261123.json"
INDEX = "docs/agents/QUADRATURE_CAMPAIGN.md"
REPORTS = "docs/agents/transient-campaign"


def checked_path(root: Path, relative: str) -> Path:
    """Require a canonical POSIX-relative path with no symlink components."""
    if type(relative) is not str:
        raise ValueError("campaign path must be a string")
    parts = PurePosixPath(relative)
    if (not relative or not parts.parts or parts.is_absolute() or ".." in parts.parts or
            "\\" in relative or ":" in relative or parts.as_posix() != relative):
        raise ValueError(f"unsafe campaign path: {relative}")
    root = root.resolve()
    path = root
    for part in parts.parts:
        path = path / part
        if path.is_symlink():
            raise ValueError(f"symlink campaign path: {relative}")
    try:
        path.resolve().relative_to(root)
    except (OSError, RuntimeError, ValueError) as exc:
        raise ValueError(f"unsafe campaign path: {relative}") from exc
    return path


def read(root: Path) -> object:
    def unique_object(entries):
        result = {}
        for key, value in entries:
            if key in result:
                raise ValueError(f"duplicate JSON key: {key}")
            result[key] = value
        return result

    def invalid_constant(value):
        raise ValueError(f"nonfinite JSON value: {value}")

    def finite_float(value):
        number = float(value)
        if not math.isfinite(number):
            invalid_constant(value)
        return number

    path = checked_path(root, LEDGER)
    if not path.is_file():
        raise ValueError("missing regular quadrature campaign ledger")
    return json.loads(path.read_text(encoding="utf-8"),
                      object_pairs_hook=unique_object,
                      parse_constant=invalid_constant, parse_float=finite_float)


def validate(data: object, root: Path, require_complete: bool = False) -> None:
    fixed = {"schema_version": 1, "requested_assignments": 100,
             "requested_waves": 5, "assignments_per_wave": 20,
             "maximum_concurrent_workers": 3}
    fields = {*fixed, "baseline_commit", "scope", "completed_waves", "assignments"}
    if (type(data) is not dict or set(data) != fields or
            any(type(data[key]) is not int or data[key] != value
                for key, value in fixed.items()) or
            type(data["assignments"]) is not list or len(data["assignments"]) > 100):
        raise ValueError("invalid quadrature campaign schema")
    if (type(data["baseline_commit"]) is not str or
            not re.fullmatch(r"[0-9a-f]{40}", data["baseline_commit"])):
        raise ValueError("baseline_commit must be a lowercase 40-digit hexadecimal commit ID")
    if type(data["scope"]) is not str or not data["scope"].strip():
        raise ValueError("campaign scope must be a nonempty string")

    fields = {"assignment", "wave", "agent", "scope", "status", "report", "report_sha256"}
    names, reports, digests = set(), set(), set()
    for position, record in enumerate(data["assignments"], 1):
        if type(record) is not dict or set(record) != fields:
            raise ValueError("invalid quadrature assignment fields")
        number = record["assignment"]
        if type(number) is not int or number != position:
            raise ValueError("assignments must be unique and ordered consecutively from 1")
        wave = (number - 1) // 20 + 1
        if type(record["wave"]) is not int or record["wave"] != wave:
            raise ValueError(f"assignment {number}: incorrect wave")
        name = record["agent"]
        if type(name) is not str or name in names:
            raise ValueError(f"assignment {number}: invalid or duplicate actual agent identifier")
        if not re.fullmatch(rf"/root/w{wave}_a{number:02d}_[a-z0-9_]+", name):
            raise ValueError(f"assignment {number}: incorrect actual agent prefix or slug")
        if type(record["scope"]) is not str or not record["scope"].strip():
            raise ValueError(f"assignment {number}: scope must be a nonempty string")
        if record["status"] != "completed":
            raise ValueError(f"assignment {number}: only returned completed assignments may be recorded")
        report = record["report"]
        expected = f"{REPORTS}/w{wave}_a{number:02d}.md"
        if type(report) is not str or report in reports or report != expected:
            raise ValueError(f"assignment {number}: incorrect or reused report path")
        path = checked_path(root, report)
        digest = record["report_sha256"]
        if type(digest) is not str or not re.fullmatch(r"[0-9a-f]{64}", digest):
            raise ValueError(f"assignment {number}: invalid report SHA256")
        if digest in digests:
            raise ValueError(f"assignment {number}: reused report content")
        if not path.is_file():
            raise ValueError(f"assignment {number}: missing regular completed report")
        content = path.read_bytes()
        if not content.strip():
            raise ValueError(f"assignment {number}: completed report is empty")
        if hashlib.sha256(content).hexdigest() != digest:
            raise ValueError(f"assignment {number}: completed report changed")
        names.add(name)
        reports.add(report)
        digests.add(digest)

    complete = list(range(1, len(data["assignments"]) // 20 + 1))
    if (type(data["completed_waves"]) is not list or
            any(type(value) is not int for value in data["completed_waves"]) or
            data["completed_waves"] != complete):
        raise ValueError("completed_waves must exactly match the full groups of 20 returned reports")
    if require_complete and len(data["assignments"]) != 100:
        raise ValueError("quadrature campaign is not complete: require 100 completed reports")


def render(data: dict) -> str:
    count, waves = len(data["assignments"]), len(data["completed_waves"])
    wave_text = ("0 completed waves" if waves == 0 else "wave 1" if waves == 1
                 else f"waves 1–{waves}")
    lines = ["# Quadrature and portability campaign", "",
             f"Actual assignments for the continuation starting at `{data['baseline_commit'][:7]}`. The request is",
             "100 workers in five waves of 20. Up to three workers run concurrently; each",
             "wave is batched. The integrator is not counted. Only returned assignments are",
             "listed as completed. Planned later workers are not counted.", "",
             "The [canonical ledger](../../project/quadrature-campaign-20261123.json) retains",
             "actual agent identifiers and SHA-256 hashes of their reports. Reports describe",
             "what was reviewed, changed, run and left unverified. More agents or tests do not",
             "establish physical fidelity.", "",
             f"Completed so far: **{count} of 100 assignments; {wave_text} of 5.**", "",
             "| Assignment | Wave | Scope | Report |", "|---|---|---|---|"]
    for record in data["assignments"]:
        scope = record["scope"].replace("\\", "\\\\").replace("|", "\\|")
        scope = scope.replace("\r", " ").replace("\n", " ")
        relative = PurePosixPath(record["report"]).relative_to("docs/agents").as_posix()
        lines.append(f"| {record['assignment']:03d} | {record['wave']} | {scope} | [Report]({relative}) |")
    if count >= 40:
        lines.extend(["", "## Review corrections", "",
                      "[Assignment 40](transient-campaign/w2_a40.md) corrects the host-bound statement in",
                      "assignment 38: host blocks and parameter queues allow 8192 frames/points; the",
                      "underlying event processor allows 4096 frames and receives one-frame host calls.",
                      "The original returned report is retained unchanged."])
    return "\n".join(lines) + "\n"


def write_index(root: Path, content: str) -> None:
    path = checked_path(root, INDEX)
    path.parent.mkdir(parents=True, exist_ok=True)
    descriptor, temporary = tempfile.mkstemp(prefix=".quadrature-index-", dir=path.parent)
    try:
        with os.fdopen(descriptor, "w", encoding="utf-8", newline="\n") as stream:
            stream.write(content)
            stream.flush()
            os.fsync(stream.fileno())
        os.replace(temporary, path)
    finally:
        if os.path.exists(temporary):
            os.unlink(temporary)


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, default=ROOT,
                        help="repository root (primarily for isolated tests)")
    commands = parser.add_subparsers(dest="command", required=True)
    check = commands.add_parser("validate", help="check recorded assignments and report bytes")
    check.add_argument("--require-complete", action="store_true")
    sync = commands.add_parser("sync", help="regenerate only the Markdown index")
    sync.add_argument("--check", action="store_true", help="fail on index drift without writing")
    args = parser.parse_args(argv)
    try:
        data = read(args.root)
        validate(data, args.root, getattr(args, "require_complete", False))
        if args.command == "sync":
            expected = render(data)
            path = checked_path(args.root, INDEX)
            if args.check:
                if not path.is_file() or path.read_text(encoding="utf-8") != expected:
                    raise ValueError("quadrature index is stale; run tools/quadrature_campaign.py sync")
            else:
                write_index(args.root, expected)
        print(f"Quadrature campaign records valid: {len(data['assignments'])}/100 completed "
              f"assignments; {len(data['completed_waves'])}/5 completed waves.")
        return 0
    except (OSError, ValueError, RecursionError) as exc:
        print(f"Quadrature campaign error: {exc}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
