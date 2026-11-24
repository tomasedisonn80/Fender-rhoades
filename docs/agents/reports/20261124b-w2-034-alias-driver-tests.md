# W2-034 — Independent stationary-alias driver tests

2026-11-24; actual worker 034/100, wave 2. Bounded RT-001 assignment from
the integrator. **The new standard-library suite passed 34 tests normally and
34 tests under `python3 -S`, with no skips.** These are protocol and failure-policy
checks, not a native four-case alias experiment or acceptance evidence.

## Assignment and starting state

Read `AGENTS.md`, `README.md`, `docs/PROJECT_STATE.md`, `docs/ROADMAP.md`, the
canonical RT-001 entry, the new driver, and its independent oracle contract.
Actually ran, each exit 0:

```sh
python3 tools/project.py validate
python3 tools/project.py sync --check
python3 tools/project.py next --json
```

The observed state was revision 32, 14 tasks, 26 evidence records, fidelity
`unvalidated`; generated documentation was current and no unclaimed task was
available. This worker used the integrator's bounded assignment and made no
claim, state, evidence-registration, Git, CMake, or product changes.

Changed paths are exclusively `tests/python/test_direct_alias_driver.py` and
this report. Temporary fixtures were automatically removed. Worker 032 owns
`tools/validate_direct_alias_toy.py`; worker 033 independently covers its oracle.

## Independent test construction

The test module constructs producer JSON directly from independently stated
wire constants. It does not call the production serializer or load the native
producer's output. The four protocol shapes have the frozen 1,024/2,048 frames
and one/two modal columns, but deliberately zero motion and voltage. Their
encoded sizes are 98,751; 198,079; 107,018; and 214,538 bytes. They establish
schema acceptance only; zero motion is not a solution for the declared phases.

Execution-policy tests use a fresh fake source tree, an unexecuted text probe,
an unexecuted text compiler, and explicitly synthetic build receipts. Probe
execution, document validation, numerical evaluation, and OS resource limits
are mocked for those tests. Consequently an injected four-case success means
only that the final reporting policy combines those injected results correctly.

Capture tests separately use scripted bounded-queue events, mock children,
mock pipe-reader threads, and intercepted process-group kills. They write real
small stdout/stderr/process files. An injected nine-byte stream with an
eight-byte test limit checks exact prefix retention; no megabyte flood or
unbounded process was started. The FIFO is real but both file-open routes are
guarded: it is rejected without opening or reading it. The descriptor identity
test substitutes two ordinary eight-byte temporary files.

## Covered behavior

- Read-only `--help` and `--plan` run in fresh `python3 -S` subprocesses with
  explicit guards against NumPy/SciPy imports; their working directories stay
  empty. Repeated, abbreviated, incomplete, or mixed CLI forms fail before
  execution. Failure/incomplete results have nonzero CLI status.
- Exact schema/key sets, synthetic provenance, case identity, success/status,
  frozen units/settings/modes, frame counts, sequential integer indices, modal
  dimensions, finite nonboolean values, and ordered displacement/velocity sums
  are checked. Finite wrong pickup voltage remains a numerical-stage question.
- Strict UTF-8 and complete JSON are required. Duplicate keys at either depth,
  NaN/infinities, oversized atoms/integers/strings/objects, nesting, and token
  caps fail. Integer-spelled `-0` retains its sign in a real-valued field while
  failing strict integer-index validation.
- Output directories and artifact names are exclusive. Existing sentinels,
  duplicate names, traversal, count limits, total-byte limits, and the final
  metadata reserve cannot overwrite or consume protected data. A failed write
  retains its exact prefix, digest, byte count, and incomplete-write flag.
- Array identities use independently packed little-endian row-major binary64
  bytes, including the distinction between positive and negative zero.
- Nonregular and oversized hash inputs reject before opening. Replacement
  between pathname metadata inspection and descriptor opening rejects on file
  identity. Source and executable changes invalidate final provenance while
  keeping the separately injected numerical result visible.
- Missing build provenance produces an incomplete result. Malformed, nonzero,
  mismatched, extra-source, response-file, wrong-output, or incorrect-compiler
  receipts stop before the probe and retain their original bytes. A consistent
  receipt explicitly does not prove that compilation occurred.
- A known numerical failure dominates missing metadata and unevaluated cases.
  Process failure blocks parsing and calculation. Timeout, spawn failure,
  nonzero exit, and stream overflow retain available bytes and block success;
  a JSON success flag cannot repair a nonzero process return code. Later cases
  are not run after failure.
- Changed retained plan or consumed probe output prevents a successful archive.
  Retained-output mutation also clears `reproducibility_complete`. Final
  artifact hashes and byte accounting agree with the actual synthetic archive.

The tests also preserve the declared boundary: no alias-acceptability threshold,
no historical acceptance change, no RT-001 completion, and no physical-fidelity
promotion follows from a scoped diagnostic success.

## Findings communicated during implementation

Review identified that opening before checking regular-file type could block
on a FIFO; worker 032 already had a precheck in progress and added descriptor
identity/nonblocking/no-follow handling. Review also identified that write-time
artifact hashes alone did not establish the final retained bytes. Worker 032
added bounded final rehashing, failure-dominant archive reporting, and required
archive integrity for `reproducibility_complete`. These are guarded regression
tests; no blocking FIFO or hostile process was executed, and no prior native
experiment was reported as having passed incorrectly.

## Actual final verification and identities

```sh
PYTHONDONTWRITEBYTECODE=1 python3 -m unittest discover -s tests/python -p test_direct_alias_driver.py -v
PYTHONDONTWRITEBYTECODE=1 python3 -S -m unittest discover -s tests/python -p test_direct_alias_driver.py -v
```

Both exited 0. Unittest reported **34 tests in 1.039 s** normally and **34 tests
in 1.080 s** under `-S`, with no skips. The surrounding subprocess wall times
were 1.179 s and 1.265 s. These are test-run durations, not DSP performance.
Earlier developmental passes covered 28 and then 31 tests; the final expanded
suite added the agreed signed-zero, descriptor-replacement, and final-integrity
regressions. No test failed in the final two runs.

All three hashes below were recorded before and after both final commands and
matched. Worker 032 then confirmed that its product source was frozen at this
tested digest.

| File | SHA-256 |
| --- | --- |
| `tests/python/test_direct_alias_driver.py` | `274531b01cddb40608dc22547397d55882c005d3cf2480e78c8630dd009df154` |
| `tools/validate_direct_alias_toy.py` | `02733c24673d70f80734959a2a2aa1f143c33187d409c2b44605dd4cda6de4b2` |
| `tools/direct_alias_oracle.py` | `2bfed9f0c3e75fce9fed9a866756f9caa4d45728a7150912790b4caf2a71ff6c` |

No native producer or compiler, actual four-case experiment, live resource-limit
enforcement, real concurrent pipe stress, platform CI, build, or full repository
suite was run. Next action: the integrator should combine these tests with the
oracle tests, then delegate the frozen native experiment using the matching
explicit build receipt. This report does not substitute for that experiment.
