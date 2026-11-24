# Continuation B worker 022 — campaign validator

## Scope and observed inputs

Implemented a standard-library-only, read-only validator in
`tools/validate_continuation_b.py`, with synthetic temporary fixtures in
`tests/python/test_continuation_b.py`. These two files and this report are the
entire worker-owned change. The repository operating contract, required project
context, existing continuation validator, and integrator helper were inspected.
No Git, canonical state, ledger, historical report, or runtime source was edited.

The one live-ledger inspection, `cat project/continuation-b-20261124.json`,
returned schema version 1, status `in_progress`, 20 assignments, and 20 recorded
completions, all in wave 1. Its baseline was
`a17e665454c1daa1c1aff3d71b790b28fe145e12`. This is the observed initial metadata;
the integrator continued adding actual assignments concurrently. This worker did
not reread or claim validation of a later live-ledger snapshot.

## Implemented contract

Running entries retain exactly the helper's four metadata fields. Completed
entries add exactly `report` and `sha256`. Assignments must be consecutive
integers starting at 1, with the derived wave and unique canonical task name.
Completed filenames bind the date, wave, assignment, and task slug; their bounded,
nonempty UTF-8 text must match the recorded lowercase SHA256. Whitespace-only,
NUL-containing, changed, missing, and nonregular reports are rejected.

Exact campaign fields, integer types, fixed 100-worker/five-wave/four-slot
metadata, baseline format, completion counts, and `in_progress`/`complete`
consistency are checked. Unknown fields, duplicate JSON keys, nonfinite numbers,
and boolean-for-integer substitutions fail. The ledger is limited to 256 KiB,
each report to 1 MiB, and the record count to 100. Symlinks, reparse points,
noncanonical paths, and root escapes are refused. Descriptor-relative no-follow
opens are used where supported, with regular-file and observed-change checks.

`--root` and root-relative `--ledger` support isolated fixtures. `--json` includes
the SHA256 of the exact ledger bytes read. `--require-complete` demands 100
completed reports, exactly 20 per wave. Success explicitly states that hashes
cannot prove actual execution, concurrency history, scientific acceptance, or
physical fidelity.

## Commands and results

- `python3 tools/project.py validate`: exit 0; 14 tasks, 26 evidence records,
  fidelity `unvalidated`.
- `python3 tools/project.py sync --check`: exit 0; generated state current.
- `python3 tools/project.py next --json`: exit 0; revision 32, no available task;
  specimen identification and calibrated acquisition remain blocked.
- `PYTHONDONTWRITEBYTECODE=1 python3 -m unittest discover -s tests/python -p 'test_continuation_b.py' -v`:
  exit 0; 13 tests passed in 0.315 seconds. Fixtures cover protocol boundaries,
  complete campaigns, adversarial JSON, tampering, paths, symlinks, FIFO rejection,
  size/count bounds, and unchanged file bytes/mtime. No tests used the live ledger.
- `sha256sum tools/validate_continuation_b.py tests/python/test_continuation_b.py`:
  source `927cb1c734241df826e6e60ad3aa16c8aa5a4c2c06e8270d79414e4b8ee0eac0`;
  tests `5cec5be30b2fa4381ed1034103bb799bad2f9e11e2dfcc56c9904e0f3fe2dd2b`.

No build, benchmark, or native-platform claim was added. Next, the integrator
should run `python3 tools/validate_continuation_b.py --json` against its current
ledger, and add `--require-complete` when all 100 actual worker reports are
registered. This checker does not replace the separate acceptance review.
