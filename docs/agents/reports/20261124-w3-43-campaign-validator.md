# W3-43 — Current development-wave metadata validator

Agent: `/root/w3_43_campaign_validator`. Date: 2026-11-24.

## Change and boundary

Added `tools/validate_development_waves.py` and
`tests/python/test_development_waves.py`. The read-only validator supports this
campaign's existing nested coordinator names and five separate wave ledgers.
It does not launch agents, create placeholders, rewrite hashes, update canonical
state, or certify execution, scientific acceptance, or physical fidelity.

Each present ledger must follow the existing W1/W2 schema. Recorded assignments
are consecutive within their wave, with unique canonical names and distinct
report paths/content. Completed records require exact report hashes; pending or
running records still require a named worker but may omit their report/hash.
An unfinished record may reserve a report path; any supplied hash is verified.
Only actually spawned workers belong in these records; the integrator must check
that fact against actual collaboration results.

Future wave files may be absent. A later wave cannot follow a missing or
unfinished wave. A complete wave requires exactly 20 completed records, and
`--require-complete` requires all five waves and 100 completed reports. Unknown
fields, incorrect IDs/counts, duplicate JSON keys, nonfinite numbers (including
float overflow), unexpected extra wave files, reused content, and changed report
bytes are rejected. Existing optional coordinator/follow-up metadata is checked.

Reads are bounded to 1 MiB per ledger and 4 MiB per report. Paths must be canonical,
root-relative, and free of symlink components; special files are rejected.
File size/mtime changes during reads are rejected. These are local snapshot
checks, not a hostile concurrent-filesystem sandbox or acquisition attestation.

## Executed verification

| Command | Observed result |
| --- | --- |
| `python3 tools/project.py validate` | Exit 0; 14 tasks, 16 evidence records, fidelity unvalidated |
| `python3 tools/project.py sync --check` | Exit 0; generated state current |
| `python3 tools/project.py next --json` | Exit 0; root already owns active work; specimen/data blockers remain |
| `python3 -m unittest discover -s tests/python -p test_development_waves.py -v` | Exit 0; 24 tests passed in 0.223 seconds, no skips |
| `python3 tools/validate_development_waves.py --json` | Exit 0; 43 recorded agents, 40 completed reports, two completed waves |
| `python3 tools/validate_development_waves.py --require-complete` | Expected exit 1; campaign is not yet complete |

Tests use isolated synthetic metadata and exercise full 100-entry completion,
progress, sequencing, malformed JSON/schema/types, incorrect counters, unsafe
paths, final/intermediate/dangling symlinks, directories/FIFOs, changed/copied
reports, read limits, immutable inputs, and execution from another directory.
Synthetic test records are never written to the real campaign. Native Windows
and macOS execution was not performed by this agent; symlink tests explicitly
skip if the test filesystem cannot create symlinks.

## Source and input identities

SHA-256 identities recorded after the checks, with no intervening edits to the
two delivered Python files:

| Path | SHA-256 |
| --- | --- |
| `tools/validate_development_waves.py` | `5a93e12b045ddc7ee617de954f4a9cd8af625bca3bcc245e47d929bca4449e25` |
| `tests/python/test_development_waves.py` | `762424831ef9072ec516cb59072338f7a0d0d1777c90002a359372bec5ecef6c` |
| `project/wave1-agent-ledger-20261124.json` | `80b481147eea3e434684bb08a07fd94b3b4fd90db145c4efbf799d9e39db9e57` |
| `project/wave2-agent-ledger-20261124.json` | `c5f0fb6b4dfc5eaa86eec9449b049033f86e669118216db5ef1ec06df8e51e7b` |
| `project/wave3-agent-ledger-20261124.json` | `9d12cffa9f96534b2b40fab3a68d7825673266286ea7d716073f11cdc75cd38f` |

The W3 identity is the inspected three-running-worker snapshot, not its eventual
completed ledger. Root should use progress validation as workers return and add
the strict completion gate only after all five waves finish. No other source,
ledger, CI, or canonical-state file was modified by this assignment.
