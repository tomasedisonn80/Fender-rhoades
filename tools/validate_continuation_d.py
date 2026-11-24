#!/usr/bin/env python3
"""Read-only integrity checks for the 2026-11-24 continuation-D ledger.

Checks assignment metadata and report bytes. It does not establish actual agent
execution, concurrency history, scientific acceptance, or physical fidelity.
"""
from __future__ import annotations

import argparse
from pathlib import Path
import sys

if __package__:
    from .validate_continuation_c import validate as validate_campaign
else:
    from validate_continuation_c import validate as validate_campaign

ROOT = Path(__file__).resolve().parents[1]
LEDGER = "project/continuation-d-20261124.json"
CAMPAIGN = "continuation-d-20261124"
REPORT_PREFIX = "docs/agents/reports/continuation-d"


def validate(root: Path = ROOT, require_complete: bool = False) -> tuple[int, int]:
    return validate_campaign(root, require_complete, ledger=LEDGER, campaign=CAMPAIGN,
                             report_prefix=REPORT_PREFIX, ledger_label="continuation-D")


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, default=ROOT, help="repository or isolated fixture root")
    parser.add_argument("--require-complete", action="store_true",
                        help="require complete status and all 100 completed reports")
    args = parser.parse_args(argv)
    try:
        recorded, completed = validate(args.root, args.require_complete)
    except (OSError, ValueError, RecursionError, RuntimeError) as exc:
        print(f"Continuation D error: {exc}", file=sys.stderr)
        return 1
    print(f"Continuation D records valid: {recorded}/100 recorded; {completed}/100 completed reports.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
