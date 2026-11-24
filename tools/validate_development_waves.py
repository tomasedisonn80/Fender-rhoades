#!/usr/bin/env python3
"""Read-only metadata and byte-integrity checks for the 2026-11-24 campaign.

Only actually spawned workers belong in a ledger, including its unfinished
entries. This tool cannot verify that a named agent executed, judge its work, or
establish scientific acceptance. It never creates entries or rewrites hashes.
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
DATE = "20261124"
WAVES = 5
PER_WAVE = 20
MAX_LEDGER_BYTES = 1024 * 1024
MAX_REPORT_BYTES = 4 * 1024 * 1024
LIMITATION = ("Metadata and report bytes only; does not attest actual agent execution, "
              "scientific acceptance, or physical fidelity.")


def ledger_name(wave: int) -> str:
    return f"project/wave{wave}-agent-ledger-{DATE}.json"


def checked_path(root: Path, relative: str) -> Path:
    """Require a canonical root-relative POSIX path without symlink components."""
    if type(relative) is not str:
        raise ValueError("path must be a string")
    parts = PurePosixPath(relative)
    if (not relative or not parts.parts or parts.is_absolute() or
            parts.as_posix() != relative or ".." in parts.parts or
            "\\" in relative or ":" in relative or
            any(ord(character) < 32 or ord(character) == 127 for character in relative)):
        raise ValueError(f"unsafe path: {relative!r}")
    path = root
    for part in parts.parts:
        path /= part
        if path.is_symlink():
            raise ValueError(f"symlink path component: {relative}")
    return path


def read_regular(root: Path, relative: str, limit: int) -> bytes:
    """Read a bounded regular file; reject special files before consuming bytes."""
    path = checked_path(root, relative)
    if not path.is_file():
        raise ValueError(f"missing regular file: {relative}")
    flags = os.O_RDONLY | getattr(os, "O_BINARY", 0) | getattr(os, "O_NOFOLLOW", 0)
    flags |= getattr(os, "O_NONBLOCK", 0)
    descriptor = os.open(path, flags)
    with os.fdopen(descriptor, "rb") as source:
        before = os.fstat(source.fileno())
        if not stat.S_ISREG(before.st_mode):
            raise ValueError(f"not a regular file: {relative}")
        if before.st_size > limit:
            raise ValueError(f"file exceeds {limit}-byte limit: {relative}")
        content = source.read(limit + 1)
        after = os.fstat(source.fileno())
    if len(content) > limit:
        raise ValueError(f"file exceeds {limit}-byte limit: {relative}")
    if (before.st_size != len(content) or before.st_size != after.st_size or
            before.st_mtime_ns != after.st_mtime_ns):
        raise ValueError(f"file changed while reading: {relative}")
    checked_path(root, relative)
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


def nonempty_string(value: object, name: str) -> None:
    if type(value) is not str or not value.strip():
        raise ValueError(f"{name} must be a nonempty string")


def agent_name(value: object, wave: int) -> str:
    if (type(value) is not str or len(value) > 1024 or
            not re.fullmatch(r"/root/(?:[a-z0-9_]+/)*[a-z0-9_]+", value)):
        raise ValueError("invalid canonical actual agent name")
    if any(not part.startswith(f"w{wave}_") for part in value.split("/")[2:]):
        raise ValueError("actual agent name has incorrect wave prefix")
    return value


def validate_wave(data: object, wave: int, root: Path, seen: dict[str, set]) -> dict:
    required = {"schema_version", "wave", "requested_actual_agents", "status", "scope", "agents"}
    optional = {"completed_actual_agents", "coordinator_additional_actual_spawns",
                "concurrency_limit_total", "coordinator", "phy002_acceptance"}
    if type(data) is not dict or not required <= set(data) or set(data) - required - optional:
        raise ValueError(f"wave {wave}: invalid ledger fields")
    for name, expected in (("schema_version", 1), ("wave", wave),
                           ("requested_actual_agents", PER_WAVE)):
        if type(data[name]) is not int or data[name] != expected:
            raise ValueError(f"wave {wave}: invalid {name}")
    if type(data["status"]) is not str or data["status"] not in {"in_progress", "complete"}:
        raise ValueError(f"wave {wave}: invalid wave status")
    nonempty_string(data["scope"], f"wave {wave} scope")
    records = data["agents"]
    if type(records) is not list or not 1 <= len(records) <= PER_WAVE:
        raise ValueError(f"wave {wave}: expected 1..20 recorded actual agents")
    completed = 0
    wave_names = set()
    for offset, record in enumerate(records, 1):
        number = (wave - 1) * PER_WAVE + offset
        fields = {"assignment", "wave", "id", "agent_task_name", "status"}
        extra = {"report", "sha256", "followup"}
        if type(record) is not dict or not fields <= set(record) or set(record) - fields - extra:
            raise ValueError(f"assignment {number}: invalid fields")
        if type(record["assignment"]) is not int or record["assignment"] != number:
            raise ValueError("assignments must be ordered consecutively within their wave")
        if type(record["wave"]) is not int or record["wave"] != wave:
            raise ValueError(f"assignment {number}: incorrect wave")
        if record["id"] != f"W{wave}-{number:02d}":
            raise ValueError(f"assignment {number}: incorrect ID")
        name = agent_name(record["agent_task_name"], wave)
        if name in seen["names"]:
            raise ValueError(f"assignment {number}: duplicate actual agent name")
        seen["names"].add(name)
        wave_names.add(name)
        status = record["status"]
        if type(status) is not str or status not in {"pending", "running", "completed"}:
            raise ValueError(f"assignment {number}: invalid agent status")
        if "followup" in record:
            nonempty_string(record["followup"], f"assignment {number} followup")
        if status == "completed":
            completed += 1
            if not {"report", "sha256"} <= set(record):
                raise ValueError(f"assignment {number}: completed agent requires report and sha256")
        if "sha256" in record and "report" not in record:
            raise ValueError(f"assignment {number}: sha256 requires a report")
        if "report" in record:
            report = record["report"]
            pattern = rf"docs/agents/reports/{DATE}-w{wave}-{number:02d}-[a-z0-9]+(?:-[a-z0-9]+)*\.md"
            if type(report) is not str or not re.fullmatch(pattern, report):
                raise ValueError(f"assignment {number}: incorrect report path")
            checked_path(root, report)
            if report in seen["reports"]:
                raise ValueError(f"assignment {number}: reused report path")
            seen["reports"].add(report)
        if "sha256" in record:
            digest = record["sha256"]
            if type(digest) is not str or not re.fullmatch(r"[0-9a-f]{64}", digest):
                raise ValueError(f"assignment {number}: invalid SHA256")
            if digest in seen["digests"]:
                raise ValueError(f"assignment {number}: reused report content")
            content = read_regular(root, record["report"], MAX_REPORT_BYTES)
            if not content.strip():
                raise ValueError(f"assignment {number}: empty report")
            if hashlib.sha256(content).hexdigest() != digest:
                raise ValueError(f"assignment {number}: report SHA256 mismatch")
            seen["digests"].add(digest)
    if data["status"] == "complete" and (len(records) != PER_WAVE or completed != PER_WAVE):
        raise ValueError(f"wave {wave}: complete requires exactly 20 completed agents")
    if "completed_actual_agents" in data:
        if type(data["completed_actual_agents"]) is not int or data["completed_actual_agents"] != completed:
            raise ValueError(f"wave {wave}: completed_actual_agents mismatch")
    if "concurrency_limit_total" in data:
        if type(data["concurrency_limit_total"]) is not int or data["concurrency_limit_total"] != 4:
            raise ValueError(f"wave {wave}: concurrency_limit_total must be 4")
    if "coordinator" in data:
        coordinator = agent_name(data["coordinator"], wave)
        if coordinator not in wave_names:
            raise ValueError(f"wave {wave}: coordinator must be a recorded actual agent")
    if "coordinator_additional_actual_spawns" in data:
        count = data["coordinator_additional_actual_spawns"]
        if "coordinator" not in data or type(count) is not int:
            raise ValueError(f"wave {wave}: invalid coordinator spawn count")
        descendants = sum(name.startswith(data["coordinator"] + "/") for name in wave_names)
        if count != descendants:
            raise ValueError(f"wave {wave}: coordinator spawn count mismatch")
    if "phy002_acceptance" in data:
        nonempty_string(data["phy002_acceptance"], "phy002_acceptance")
    return {"wave": wave, "status": data["status"], "recorded_agents": len(records),
            "completed_agents": completed}


def validate(root: Path, require_complete: bool = False) -> dict:
    """Validate present sequential ledgers, without modifying any input file."""
    root = root.resolve(strict=True)
    if not root.is_dir():
        raise ValueError("root is not a directory")
    project = checked_path(root, "project")
    expected_names = {Path(ledger_name(wave)).name for wave in range(1, WAVES + 1)}
    for path in project.glob(f"wave*-agent-ledger-{DATE}.json"):
        if path.name not in expected_names:
            raise ValueError(f"unexpected campaign wave ledger: {path.name}")
    seen = {"names": set(), "reports": set(), "digests": set()}
    waves = []
    missing = False
    for wave in range(1, WAVES + 1):
        relative = ledger_name(wave)
        path = checked_path(root, relative)
        if not path.exists():
            missing = True
            continue
        if missing:
            raise ValueError(f"wave {wave}: missing earlier wave ledger")
        if waves and waves[-1]["status"] != "complete":
            raise ValueError(f"wave {wave}: previous wave is not complete")
        data = strict_json(read_regular(root, relative, MAX_LEDGER_BYTES))
        waves.append(validate_wave(data, wave, root, seen))
    if not waves:
        raise ValueError("missing wave 1 ledger")
    completed_waves = sum(wave["status"] == "complete" for wave in waves)
    if require_complete and completed_waves != WAVES:
        raise ValueError("campaign not complete: require five waves of 20 completed agents")
    return {"schema_version": 1, "campaign": DATE,
            "status": "complete" if completed_waves == WAVES else "in_progress",
            "recorded_agents": sum(wave["recorded_agents"] for wave in waves),
            "completed_agents": sum(wave["completed_agents"] for wave in waves),
            "completed_waves": completed_waves, "waves": waves, "limitation": LIMITATION}


def main(argv=None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, default=ROOT, help="repository root (default: this tool's repository)")
    parser.add_argument("--require-complete", action="store_true", help="require all five waves and 100 completed reports")
    parser.add_argument("--json", action="store_true", help="print the validation summary as JSON")
    args = parser.parse_args(argv)
    try:
        result = validate(args.root, args.require_complete)
    except (OSError, ValueError, RecursionError) as exc:
        print(f"Development wave error: {exc}", file=sys.stderr)
        return 1
    if args.json:
        print(json.dumps(result, indent=2, allow_nan=False))
    else:
        print(f"Development wave metadata valid: {result['recorded_agents']}/100 recorded agents; "
              f"{result['completed_agents']}/100 completed reports; "
              f"{result['completed_waves']}/5 completed waves.")
        print(LIMITATION)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
