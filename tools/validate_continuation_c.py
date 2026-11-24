#!/usr/bin/env python3
"""Read-only integrity checks for the 2026-11-24 continuation-C ledger.

Checks assignment metadata and report bytes. It does not establish actual agent
execution, concurrency history, scientific acceptance, or physical fidelity.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import math
from pathlib import Path
import re
import stat
import sys

ROOT = Path(__file__).resolve().parents[1]
LEDGER = "project/continuation-c-20261124.json"
CAMPAIGN = "continuation-c-20261124"
WAVES, PER_WAVE = 5, 20
TOTAL = WAVES * PER_WAVE


def read_regular(root: Path, relative: str) -> bytes:
    """Read an internally constructed path, rejecting linked path components."""
    path = root
    for part in relative.split("/"):
        path = path / part
        info = path.lstat()
        if (stat.S_ISLNK(info.st_mode) or
                getattr(info, "st_file_attributes", 0) &
                getattr(stat, "FILE_ATTRIBUTE_REPARSE_POINT", 0)):
            raise ValueError(f"symlink or reparse path component: {relative}")
    if not stat.S_ISREG(info.st_mode):
        raise ValueError(f"not a regular file: {relative}")
    if not path.resolve(strict=True).is_relative_to(root):
        raise ValueError(f"file escapes repository: {relative}")
    return path.read_bytes()


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


def validate(root: Path = ROOT, require_complete: bool = False) -> tuple[int, int]:
    root = root.resolve(strict=True)
    if not root.is_dir():
        raise ValueError("root must be a directory")
    data = strict_json(read_regular(root, LEDGER))
    fixed = {"schema_version": 1, "requested_agents": TOTAL, "requested_waves": WAVES,
             "concurrency_limit_including_integrator": 4}
    fields = {*fixed, "campaign", "baseline_commit", "status", "completed_agents", "agents"}
    if type(data) is not dict or set(data) != fields:
        raise ValueError("invalid continuation-C ledger fields")
    for key, expected in fixed.items():
        if type(data[key]) is not int or data[key] != expected:
            raise ValueError(f"{key} must be the integer {expected}")
    if data["campaign"] != CAMPAIGN:
        raise ValueError(f"campaign must be {CAMPAIGN}")
    if (type(data["baseline_commit"]) is not str or
            not re.fullmatch(r"[0-9a-f]{40}", data["baseline_commit"])):
        raise ValueError("baseline_commit must be 40 lowercase hexadecimal digits")
    if type(data["status"]) is not str or data["status"] not in {"in_progress", "complete"}:
        raise ValueError("ledger status must be in_progress or complete")
    records = data["agents"]
    if type(records) is not list or len(records) > TOTAL:
        raise ValueError("agents must be a list of at most 100 assignments")
    if type(data["completed_agents"]) is not int:
        raise ValueError("completed_agents must be an integer")

    names, reports = set(), set()
    completed_by_wave = [0] * WAVES
    base_fields = {"assignment", "wave", "agent_task_name", "status"}
    for number, record in enumerate(records, 1):
        if type(record) is not dict or not base_fields <= set(record):
            raise ValueError(f"assignment {number}: invalid record fields")
        status = record["status"]
        if type(status) is not str or status not in {"running", "completed"}:
            raise ValueError(f"assignment {number}: status must be running or completed")
        if set(record) != base_fields | ({"report", "sha256"} if status == "completed" else set()):
            raise ValueError(f"assignment {number}: incorrect fields for {status} record")
        if type(record["assignment"]) is not int or record["assignment"] != number:
            raise ValueError("assignments must be consecutive integers 1..N in recorded order")
        wave = (number - 1) // PER_WAVE + 1
        if type(record["wave"]) is not int or record["wave"] != wave:
            raise ValueError(f"assignment {number}: incorrect wave; expected {wave}")
        if any(count != PER_WAVE for count in completed_by_wave[:wave - 1]):
            raise ValueError(f"wave {wave} requires all 20 reports completed in every earlier wave")
        name = record["agent_task_name"]
        if type(name) is not str:
            raise ValueError(f"assignment {number}: agent_task_name must be a string")
        if name in names:
            raise ValueError(f"assignment {number}: duplicate agent_task_name")
        if not re.fullmatch(rf"/root/w{wave}_{number:03d}_[a-z0-9]+(?:_[a-z0-9]+)*", name):
            raise ValueError(f"assignment {number}: incorrect canonical agent_task_name")
        names.add(name)
        if status == "running":
            continue
        report = record["report"]
        if type(report) is not str:
            raise ValueError(f"assignment {number}: report path must be a string")
        if report in reports:
            raise ValueError(f"assignment {number}: duplicate report path")
        if report != f"docs/agents/reports/continuation-c/w{wave}-{number:03d}.md":
            raise ValueError(f"assignment {number}: incorrect canonical report path")
        reports.add(report)
        digest = record["sha256"]
        if type(digest) is not str or not re.fullmatch(r"[0-9a-f]{64}", digest):
            raise ValueError(f"assignment {number}: sha256 must be 64 lowercase hexadecimal digits")
        if hashlib.sha256(read_regular(root, report)).hexdigest() != digest:
            raise ValueError(f"assignment {number}: report SHA256 mismatch")
        completed_by_wave[wave - 1] += 1

    completed = sum(completed_by_wave)
    if data["completed_agents"] != completed:
        raise ValueError("completed_agents does not match completed records")
    if data["status"] == "complete" and completed != TOTAL:
        raise ValueError("complete status requires all 100 completed reports")
    if require_complete and (completed != TOTAL or data["status"] != "complete"):
        raise ValueError("campaign not complete: require complete status and 100 completed reports")
    return len(records), completed


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, default=ROOT, help="repository or isolated fixture root")
    parser.add_argument("--require-complete", action="store_true",
                        help="require complete status and all 100 completed reports")
    args = parser.parse_args(argv)
    try:
        recorded, completed = validate(args.root, args.require_complete)
    except (OSError, ValueError, RecursionError, RuntimeError) as exc:
        print(f"Continuation C error: {exc}", file=sys.stderr)
        return 1
    print(f"Continuation C records valid: {recorded}/100 recorded; {completed}/100 completed reports.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
