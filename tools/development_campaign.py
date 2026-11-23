#!/usr/bin/env python3
"""Record the 2026-11-23 development campaign: 100 actual agents in five waves.

One integrator owns writes. Register only task names returned by the collaboration
system. This ledger checks declared records; it does not launch or attest agents.
The previous setup campaign and its reports are not read or modified.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[1]
LEDGER = "project/development-campaign-20261123.json"
INDEX = "docs/agents/DEVELOPMENT_CAMPAIGN.md"
REPORTS = "docs/agents/development-20261123"


def checked_path(relative: str) -> Path:
    """Reject symlinks and any destination outside this campaign's repository."""
    root = ROOT.resolve()
    path = root / relative
    try:
        path.resolve().relative_to(root)
        for part in (path, *path.parents):
            if part == root:
                break
            if part.is_symlink():
                raise ValueError(f"symlink path is not allowed: {relative}")
    except (OSError, RuntimeError, ValueError) as exc:
        raise ValueError(f"unsafe campaign path: {relative}") from exc
    return path


def read() -> dict:
    def pairs(entries):
        result = {}
        for key, value in entries:
            if key in result:
                raise ValueError(f"duplicate JSON key: {key}")
            result[key] = value
        return result

    def invalid(value):
        raise ValueError(f"nonfinite JSON value: {value}")

    return json.loads(checked_path(LEDGER).read_text(encoding="utf-8"),
                      object_pairs_hook=pairs, parse_constant=invalid)


def atomic(relative: str, content: str) -> None:
    path = checked_path(relative)
    path.parent.mkdir(parents=True, exist_ok=True)
    descriptor, temporary = tempfile.mkstemp(prefix=".development-campaign-", dir=path.parent)
    try:
        with os.fdopen(descriptor, "w", encoding="utf-8", newline="\n") as handle:
            handle.write(content)
            handle.flush()
            os.fsync(handle.fileno())
        os.replace(temporary, path)
    finally:
        if os.path.exists(temporary):
            os.unlink(temporary)


def wave(identifier: str) -> int:
    if not isinstance(identifier, str) or not re.fullmatch(r"A[0-9]{3}", identifier):
        raise ValueError("agent ID must be A001 through A100")
    number = int(identifier[1:])
    if not 1 <= number <= 100:
        raise ValueError("agent ID must be A001 through A100")
    return (number - 1) // 20 + 1


def report(identifier: str) -> str:
    return f"{REPORTS}/W{wave(identifier)}-{identifier}.md"


def validate(data: dict, require_complete: bool = False) -> None:
    expected = {"schema_version": 1, "requested_agents": 100, "waves": 5,
                "agents_per_wave": 20}
    if (type(data) is not dict or set(data) != {*expected, "agents"} or
            any(type(data[key]) is not int or data[key] != value
                for key, value in expected.items()) or type(data["agents"]) is not list):
        raise ValueError("invalid five-wave development campaign schema")
    agents = {}
    fields = {"id", "wave", "task_name", "scope", "status", "report", "report_sha256"}
    for agent in data["agents"]:
        if type(agent) is not dict or set(agent) != fields:
            raise ValueError("invalid assignment fields")
        identifier = agent["id"]
        number = wave(identifier)
        if identifier in agents:
            raise ValueError(f"duplicate assignment: {identifier}")
        if (type(agent["wave"]) is not int or agent["wave"] != number or
                agent["task_name"] != f"/root/w{number}_a{identifier[1:]}" or
                agent["report"] != report(identifier)):
            raise ValueError(f"{identifier}: wave, actual task name or report mismatch")
        if (not isinstance(agent["scope"], str) or not agent["scope"].strip() or
                agent["status"] not in ("running", "completed")):
            raise ValueError(f"{identifier}: empty scope or invalid status")
        path = checked_path(agent["report"])
        if agent["status"] == "completed":
            digest = agent["report_sha256"]
            if (not isinstance(digest, str) or not re.fullmatch(r"[0-9a-f]{64}", digest) or
                    not path.is_file()):
                raise ValueError(f"{identifier}: missing regular report or invalid hash")
            if hashlib.sha256(path.read_bytes()).hexdigest() != digest:
                raise ValueError(f"{identifier}: completed report changed")
        elif agent["report_sha256"] is not None:
            raise ValueError(f"{identifier}: running assignment must not have a report hash")
        agents[identifier] = agent
    for agent in agents.values():
        for number in range(1, (agent["wave"] - 1) * 20 + 1):
            previous = agents.get(f"A{number:03d}")
            if previous is None or previous["status"] != "completed":
                raise ValueError(f"wave {agent['wave']} requires all earlier waves completed")
    if require_complete and (len(agents) != 100 or
                             any(a["status"] != "completed" for a in agents.values())):
        raise ValueError("development campaign is not complete")


def render(data: dict) -> str:
    completed = sum(a["status"] == "completed" for a in data["agents"])
    lines = ["# Development agent campaign — 2026-11-23", "",
             "<!-- Generated by tools/development_campaign.py; do not edit directly. -->", "",
             f"Requested: **100 actual worker assignments in five waves of 20**. "
             f"Recorded: **{len(data['agents'])}**. Completed: **{completed}**.", "",
             "Each wave runs in batches within the available concurrency. A later wave "
             "starts after all 20 reports in the preceding wave have been recorded. "
             "Completion records an assignment report, not resolved findings or physical fidelity.", "",
             "This development campaign is separate from the "
             "[earlier setup campaign](CAMPAIGN.md). Recorded task names and report hashes "
             "provide an audit trail; the tool cannot independently attest agent execution.", ""]
    for number in range(1, 6):
        lines.extend([f"## Wave {number}", "", "| Agent | Actual task | Scope | Status | Report |",
                      "|---|---|---|---|---|"])
        for agent in sorted(data["agents"], key=lambda entry: entry["id"]):
            if agent["wave"] != number:
                continue
            scope = agent["scope"].replace("\\", "\\\\").replace("|", "\\|")
            scope = scope.replace("\r", " ").replace("\n", " ")
            link = (f"[report](development-20261123/W{number}-{agent['id']}.md)"
                    if agent["status"] == "completed" else "pending")
            lines.append(f"| {agent['id']} | `{agent['task_name']}` | {scope} | "
                         f"{agent['status']} | {link} |")
        lines.append("")
    return "\n".join(lines)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest="command", required=True)
    commands.add_parser("init", help="create an empty ledger without replacing existing files")
    register = commands.add_parser("register")
    register.add_argument("id")
    register.add_argument("task_name")
    register.add_argument("scope")
    complete = commands.add_parser("complete")
    complete.add_argument("ids", nargs="+")
    check = commands.add_parser("validate")
    check.add_argument("--require-complete", action="store_true")
    sync = commands.add_parser("sync")
    sync.add_argument("--check", action="store_true")
    args = parser.parse_args()
    try:
        # Check both write targets before a possible ledger update.
        ledger, index = checked_path(LEDGER), checked_path(INDEX)
        if args.command == "init":
            if ledger.exists() or index.exists():
                raise ValueError("campaign ledger or index already exists")
            data = {"schema_version": 1, "requested_agents": 100, "waves": 5,
                    "agents_per_wave": 20, "agents": []}
        else:
            data = read()
            validate(data)
        if args.command == "register":
            number = wave(args.id)
            data["agents"].append({"id": args.id, "wave": number, "task_name": args.task_name,
                                   "scope": args.scope, "status": "running",
                                   "report": report(args.id), "report_sha256": None})
        elif args.command == "complete":
            for identifier in args.ids:
                agent = next((a for a in data["agents"] if a["id"] == identifier), None)
                if agent is None:
                    raise ValueError(f"unknown assignment: {identifier}")
                if agent["status"] == "completed":
                    raise ValueError(f"{identifier}: already completed; report hash is immutable")
                path = checked_path(agent["report"])
                if not path.is_file():
                    raise ValueError(f"{identifier}: missing regular report")
                agent.update(status="completed", report_sha256=hashlib.sha256(path.read_bytes()).hexdigest())
        validate(data, getattr(args, "require_complete", False))
        expected = render(data)
        if args.command == "sync" and args.check:
            if not index.is_file() or index.read_text(encoding="utf-8") != expected:
                raise ValueError("development index is stale; run tools/development_campaign.py sync")
        elif args.command != "validate":
            if args.command in ("init", "register", "complete"):
                atomic(LEDGER, json.dumps(data, indent=2) + "\n")
            atomic(INDEX, expected)
        completed = sum(a["status"] == "completed" for a in data["agents"])
        print(f"Development campaign valid: {completed}/100 completed in five waves.")
        return 0
    except (OSError, ValueError, KeyError, TypeError, RecursionError) as exc:
        print(f"Development campaign error: {exc}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
