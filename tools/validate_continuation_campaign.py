#!/usr/bin/env python3
"""Read-only validation of the 2026-11-23 continuation campaign.

Checks recorded assignments and report bytes, not actual agent execution or work
acceptance. Does not launch agents, register records, regenerate indexes, or read
the separate setup/development campaign ledgers.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import math
from pathlib import Path, PurePosixPath
import re
import sys

ROOT = Path(__file__).resolve().parents[1]
LEDGER = "project/continuation-campaign-20261123.json"
REPORTS = "docs/agents/continuation-20261123"


def checked_path(relative: str) -> Path:
    """Keep a canonical relative path inside ROOT without following symlinks."""
    if type(relative) is not str:
        raise ValueError("campaign path must be a string")
    parts = PurePosixPath(relative)
    if (not relative or parts.is_absolute() or ".." in parts.parts or
            "\\" in relative or parts.as_posix() != relative):
        raise ValueError(f"unsafe campaign path: {relative}")
    root = ROOT.resolve()
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


def read() -> object:
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

    path = checked_path(LEDGER)
    if not path.is_file():
        raise ValueError("missing regular continuation campaign ledger")
    return json.loads(path.read_text(encoding="utf-8"),
                      object_pairs_hook=unique_object,
                      parse_constant=invalid_constant, parse_float=finite_float)


def validate(data: object, require_complete: bool = False) -> None:
    expected = {"schema_version": 1, "requested_agents": 100,
                "waves": 5, "agents_per_wave": 20}
    if (type(data) is not dict or
            set(data) != {*expected, "baseline_commit", "agents"} or
            any(type(data[key]) is not int or data[key] != value
                for key, value in expected.items()) or
            type(data["agents"]) is not list):
        raise ValueError("invalid continuation campaign schema")
    if (type(data["baseline_commit"]) is not str or
            not re.fullmatch(r"[0-9a-fA-F]{40}", data["baseline_commit"])):
        raise ValueError("baseline_commit must be a 40-digit hexadecimal commit ID")

    fields = {"id", "wave", "task_name", "scope", "status", "report", "report_sha256"}
    agents, task_names = {}, set()
    for agent in data["agents"]:
        if type(agent) is not dict or set(agent) != fields:
            raise ValueError("invalid continuation assignment fields")
        identifier = agent["id"]
        if type(identifier) is not int or not 1 <= identifier <= 100:
            raise ValueError("agent id must be an integer from 1 through 100")
        if identifier in agents:
            raise ValueError(f"duplicate assignment id: {identifier}")
        wave = (identifier - 1) // 20 + 1
        if type(agent["wave"]) is not int or agent["wave"] != wave:
            raise ValueError(f"agent {identifier}: incorrect wave")
        name = agent["task_name"]
        if type(name) is not str:
            raise ValueError(f"agent {identifier}: task_name must be a string")
        if name in task_names:
            raise ValueError(f"duplicate task_name: {name}")
        if not re.fullmatch(rf"/root/w{wave}_{identifier:03d}_[a-z0-9_]+", name):
            raise ValueError(f"agent {identifier}: incorrect task_name prefix or slug")
        if type(agent["scope"]) is not str or not agent["scope"].strip():
            raise ValueError(f"agent {identifier}: scope must be a nonempty string")
        if (type(agent["status"]) is not str or
                agent["status"] not in ("running", "completed")):
            raise ValueError(f"agent {identifier}: invalid status")
        report = f"{REPORTS}/w{wave}-{identifier:03d}.md"
        if type(agent["report"]) is not str or agent["report"] != report:
            raise ValueError(f"agent {identifier}: incorrect report path")
        path = checked_path(report)
        digest = agent["report_sha256"]
        if agent["status"] == "running":
            if digest is not None:
                raise ValueError(f"agent {identifier}: running report hash must be null")
        else:
            if type(digest) is not str or not re.fullmatch(r"[0-9a-f]{64}", digest):
                raise ValueError(f"agent {identifier}: invalid report SHA256")
            if not path.is_file():
                raise ValueError(f"agent {identifier}: missing regular completed report")
            if hashlib.sha256(path.read_bytes()).hexdigest() != digest:
                raise ValueError(f"agent {identifier}: completed report changed")
        agents[identifier] = agent
        task_names.add(name)

    for later_wave in sorted({agent["wave"] for agent in agents.values()}):
        for identifier in range(1, (later_wave - 1) * 20 + 1):
            previous = agents.get(identifier)
            if previous is None or previous["status"] != "completed":
                raise ValueError(f"wave {later_wave} requires all 20 assignments in each "
                                 f"earlier wave completed; agent {identifier} is incomplete")
    if require_complete and (len(agents) != 100 or
                             any(agent["status"] != "completed" for agent in agents.values())):
        raise ValueError("continuation campaign is not complete: require 100 completed reports")


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--require-complete", action="store_true",
                        help="require all 100 assignments and their unchanged reports")
    args = parser.parse_args(argv)
    try:
        data = read()
        validate(data, args.require_complete)
        completed = sum(agent["status"] == "completed" for agent in data["agents"])
        print(f"Continuation campaign records valid: {len(data['agents'])}/100 recorded, "
              f"{completed}/100 completed in five waves.")
        return 0
    except (OSError, ValueError, RecursionError) as exc:
        print(f"Continuation campaign error: {exc}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
