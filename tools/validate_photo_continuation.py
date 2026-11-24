#!/usr/bin/env python3
"""Read-only metadata and report-integrity checks for the photo continuation.

The ledger is a progress snapshot, not proof of agent execution or chronology.
"""
from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import re
import stat
import sys

# Keep this read-only command from creating caches for its local helper import.
sys.dont_write_bytecode = True
if __package__:
    from .validate_continuation_b import read_regular, strict_json
else:
    from validate_continuation_b import read_regular, strict_json

ROOT = Path(__file__).resolve().parents[1]
LEDGER = "project/photo-continuation-20261124.json"
REPORT_PREFIX = "docs/agents/reports/photo-continuation-20261124"
LIMITATION = ("Checks metadata and report bytes only; does not attest agent execution, "
              "execution order, scientific acceptance, or physical fidelity.")


def check_report_path(root: Path, relative: str) -> None:
    """Reject unsafe existing components even when a running report is absent.

    Callers supply the exact canonical report path derived from an agent ID.
    Completed reports additionally use the descriptor-based read_regular helper.
    """
    path = root
    parts = relative.split("/")
    for index, part in enumerate(parts):
        path = path / part
        try:
            info = path.lstat()
        except FileNotFoundError:
            return
        if (stat.S_ISLNK(info.st_mode) or
                getattr(info, "st_file_attributes", 0) &
                getattr(stat, "FILE_ATTRIBUTE_REPARSE_POINT", 0)):
            raise ValueError(f"symlink or reparse report path: {relative}")
        expected = stat.S_ISREG if index == len(parts) - 1 else stat.S_ISDIR
        if not expected(info.st_mode):
            raise ValueError(f"invalid report path component: {relative}")


def validate_records(data: object, root: Path, require_complete: bool = False) -> dict:
    expected = {"schema_version": 1, "requested_agents": 100,
                "waves": 5, "agents_per_wave": 20}
    if type(data) is not dict or set(data) != {*expected, "agents"}:
        raise ValueError("ledger must contain exactly the schema fields and agents")
    for key, value in expected.items():
        if type(data[key]) is not int or data[key] != value:
            raise ValueError(f"{key} must be the integer {value}")
    records = data["agents"]
    if type(records) is not list or len(records) > 100:
        raise ValueError("agents must be a list of at most 100 rows")
    fields = {"id", "wave", "task_name", "scope", "status", "report", "report_sha256"}
    ids, names = set(), set()
    completed_by_wave = [0] * 5
    latest_wave = 1
    for row in records:
        if type(row) is not dict or set(row) != fields:
            raise ValueError("agent row must contain exactly the seven schema fields")
        agent_id = row["id"]
        if (type(agent_id) is not str or not re.fullmatch(r"A[0-9]{3}", agent_id)
                or not 1 <= int(agent_id[1:]) <= 100):
            raise ValueError("agent id must be A001..A100")
        if agent_id in ids:
            raise ValueError(f"duplicate agent id: {agent_id}")
        ids.add(agent_id)
        number = int(agent_id[1:])
        wave = (number - 1) // 20 + 1
        if type(row["wave"]) is not int or row["wave"] != wave:
            raise ValueError(f"{agent_id}: wave must be the integer {wave}")
        latest_wave = max(latest_wave, wave)
        name = row["task_name"]
        if type(name) is not str or not re.fullmatch(r"/root/[a-z0-9_]+(?:/[a-z0-9_]+)*", name):
            raise ValueError(f"{agent_id}: task_name must be a canonical /root/... task path")
        if name in names:
            raise ValueError(f"{agent_id}: duplicate task_name: {name}")
        names.add(name)
        if type(row["scope"]) is not str or not row["scope"].strip():
            raise ValueError(f"{agent_id}: scope must be a nonempty string")
        status = row["status"]
        if type(status) is not str or status not in {"running", "completed"}:
            raise ValueError(f"{agent_id}: status must be running or completed")
        report = f"{REPORT_PREFIX}/w{wave}-{number:03d}.md"
        if type(row["report"]) is not str or row["report"] != report:
            raise ValueError(f"{agent_id}: report must be {report}")
        check_report_path(root, report)
        digest = row["report_sha256"]
        if status == "running":
            if digest is not None:
                raise ValueError(f"{agent_id}: running report_sha256 must be null")
            continue
        if type(digest) is not str or not re.fullmatch(r"[0-9a-f]{64}", digest):
            raise ValueError(f"{agent_id}: completed report_sha256 must be 64 lowercase hex digits")
        content = read_regular(root, report, 1024 * 1024)
        if hashlib.sha256(content).hexdigest() != digest:
            raise ValueError(f"{agent_id}: report SHA256 mismatch")
        completed_by_wave[wave - 1] += 1
    for wave in range(1, latest_wave):
        if completed_by_wave[wave - 1] != 20:
            raise ValueError(f"wave {wave} must have all 20 completed rows before later waves")
    completed = sum(completed_by_wave)
    if require_complete and completed != 100:
        raise ValueError("campaign incomplete: require all 100 completed rows")
    return {"recorded_agents": len(records), "completed_agents": completed,
            "completed_by_wave": completed_by_wave, "limitation": LIMITATION}


def validate(root: Path = ROOT, require_complete: bool = False, ledger: str = LEDGER) -> dict:
    root = root.resolve(strict=True)
    if not root.is_dir():
        raise ValueError("root must be a directory")
    content = read_regular(root, ledger, 256 * 1024)
    result = validate_records(strict_json(content), root, require_complete)
    result["ledger_sha256"] = hashlib.sha256(content).hexdigest()
    return result


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, default=ROOT, help="repository or isolated fixture root")
    parser.add_argument("--ledger", default=LEDGER, help="canonical ledger path relative to --root")
    parser.add_argument("--require-complete", action="store_true", help="require all 100 rows completed")
    parser.add_argument("--json", action="store_true", help="print the integrity summary as JSON")
    args = parser.parse_args(argv)
    try:
        result = validate(args.root, args.require_complete, args.ledger)
    except (OSError, ValueError, RecursionError, RuntimeError) as exc:
        print(f"Photo continuation error: {exc}", file=sys.stderr)
        return 1
    if args.json:
        print(json.dumps(result, indent=2, allow_nan=False))
    else:
        print(f"Photo continuation metadata valid: {result['recorded_agents']}/100 recorded; "
              f"{result['completed_agents']}/100 completed reports.")
        print(LIMITATION)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
