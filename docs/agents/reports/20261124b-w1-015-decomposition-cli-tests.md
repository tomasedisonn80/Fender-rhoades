# Worker 015 — decomposition CLI and provenance tests

## Changes

Added `tests/python/test_filtered_decomposition_cli.py` with 12 focused tests.
Public subprocess checks exercise help/plan without site packages, canonical
plan/case hashes, frozen-case drift rejection, invalid arguments, and refusal of
existing directories, files, live symlinks and dangling symlinks.

Fault fixtures verify exact malformed/partial probe-byte retention, terminal
error summaries, manifest hashes, exclusive writes, probe timeout handling,
worker exit-code forwarding, and timeout process-group kill/reaping requests.
Constructed-array fixtures verify retained file/array hashes, source or probe
mutation preventing completion, unresolved-reference exit 2, and successful
bookkeeping preserving comparison B's failure and exit 1.

Reviewed the existing
`docs/validation/filtered-decomposition-initial-20261124b.json`: its completed
bookkeeping retains failed comparison B. Its comparison records supply retention
fixtures; no waveform or quadrature sweep was repeated.

## Verification

- `python3 tools/project.py validate` — passed; 14 tasks, 26 evidence records.
- `python3 tools/project.py sync --check` — passed.
- `python3 tools/project.py next --json` — passed; no unclaimed available work.
- `python3 -B -m unittest discover -s tests/python -p 'test_filtered_decomposition_cli.py' -v`
  — 12 passed, 1.829 seconds.
- `python3 -S -B -m unittest discover -s tests/python -p 'test_filtered_decomposition_cli.py' -v`
  — 9 passed, 3 optional numerical checks skipped, 1.318 seconds.

No product defect was observed in the exercised paths. Timeout cleanup uses
mocks; these tests do not establish operating-system process cleanup or physical
accuracy. No production source, numerical gate, canonical state, or existing
evidence was changed. Next: integrator reviews and registers these two new files.
