# Wave 2, assignment 34 — contact peak retention error boundaries

**Status: completed bounded test assignment; both discovered defects were
repaired by the source owner and passed targeted verification.**
This is a bounded software test assignment under PHY-002. The analytic peak
diagnostic and PHY-002's continuous-extrema acceptance remain **incomplete**;
constructed JSON and injected filesystem failures are not physical evidence.

## Scope and evidence basis

Owned paths are
`tests/python/test_contact_peak_retention_errors.py` and this report. No CLI,
schema, oracle, production solver, old auditor, canonical state, or Git operation
was changed or performed by this worker. Required repository context was read,
including `AGENTS.md`, `README.md`, `docs/PROJECT_STATE.md`, `docs/ROADMAP.md`, and
`project/state.json`. Work follows the frozen
[four-fixture contract](../../specs/CONTACT_ANALYTIC_PEAK_SLICE_20261123.md) and
the [Wave 1 CLI](20261124-w1-08-cli.md) and
[Wave 1 retention](20261124-w1-09-retention.md) recommendations.

The existing `test_contact_peak_cli.py` covers ordinary retained raw bytes,
hashes, comparisons, exclusive output creation, declared-producer hashing and
exit precedence. The new tests exercise additional in-process storage and
serialization boundaries. Inputs reuse the existing valid constructed
`forced_clamp` transport helper. Every candidate, raw file, intermediate
manifest and injected-failure artifact is created inside a
`TemporaryDirectory` and removed after its test. No supplied executable runs.

## Initial observations

The first seven-test run, against CLI SHA-256
`ba25e476be93aecc1ccdbedfa020f457369bdad62c0ce16d5f749f79fc42e3ef`, produced
**five passing tests, one assertion failure and one error**:

| Boundary | Initial result and observed behavior |
| --- | --- |
| Interrupted manifest replacement after the first retained success | Passed: exit 3, first raw/comparison/record bytes preserved, complete plan retained, remaining two inputs explicitly listed as unattempted, and no repeated input read. |
| Replacement error followed by temporary cleanup error | Failed: the secondary cleanup exception masks the primary replacement exception. |
| Partial raw write under the collection byte cap | Error: missing partial raw metadata; detailed reproduction below also confirms inaccurate retained-byte accounting. |
| Comparison serialization of a nonfinite result | Passed: strict JSON serialization fails operationally with exit 3; raw evidence and other successful comparisons remain retained, and later inputs are attempted. |
| Final manifest replacement error | Passed: exit 3 and explicit stderr, no success summary, previous manifest bytes unchanged, prior case evidence intact, and the temporary file removed. |
| Source identity change | Passed: exit 3, differing before/after identities retained, source-change explanation retained, successful case comparison preserved. |
| FIFO and final symlink candidate guards | Passed on this Linux host: both inputs rejected as operational failures before opening, and a subsequent regular input still compares successfully. |

### Defect 1: cleanup masks the original retention failure

The minimal test calls `_replace_manifest` with an existing manifest. Injected
`os.replace` raises `OSError(ENOSPC, "injected primary replacement failure")`;
temporary `Path.unlink` then raises
`PermissionError(EACCES, "injected secondary cleanup failure")`. The existing
manifest stays byte-for-byte unchanged, but the caller receives only the
secondary cleanup exception. The test fails because its exception string lacks
the primary replacement failure. Wave 1 retention guidance explicitly requires
best-effort cleanup not to mask the original error.

Requested correction: preserve the active replacement/write exception if
cleanup also fails. A cleanup failure with no prior error must still be reported
as an operational failure.

### Defect 2: a partial raw file is omitted from retained-byte metadata

Each constructed input contains 2,961 bytes. The test sets the collection cap
to 5,922 bytes and supplies three inputs. After the first success, the second
raw writer stores and flushes 17 bytes and then raises an injected `ENOSPC`.
The CLI correctly returns 3, records the second case's operational error and
rejects the third input at the consumed-input collection cap. However:

- `candidate-0002.raw` exists with the 17-byte prefix, while its record omits
  `raw_file`, `raw_complete`, `retained_bytes` and `retained_sha256`.
- The manifest claims `retained_candidate_bytes: 5922`; the actual sum of all
  retained raw file sizes is 2,978 bytes: 2,961 + 17 + 0.

The original run encountered `KeyError: 'raw_file'`. The assertion was then
made more explicit using `.get`, producing
`None != 'candidate-0002.raw'` in a targeted diagnostic reproduction. This is a
test-diagnostic improvement, not a change to the required retention behavior.

Requested correction: describe the actual retained prefix, including its
length/hash and incomplete status, without claiming a whole-input hash. Keep
consumed-input accounting separate from retained-output accounting so a failed
write cannot reopen the collection allowance. Preserve the original write error
and exit 3 even when a prefix is retained.

## Commands and actual results

Repository context checks, all exit 0:

```sh
python3 tools/project.py validate
python3 tools/project.py sync --check
python3 tools/project.py next --json
```

Observed: 14 tasks, 16 evidence records, fidelity `unvalidated`, generated state
current, and no independently available task. This worker performed the assigned
bounded review and did not claim or update canonical work.

Initial focused suite, exit 1:

```sh
python3 -m unittest discover -s tests/python -p test_contact_peak_retention_errors.py -v
```

Observed: seven tests in 0.230 s; five passed, one failed, one errored. The test
file's SHA-256 for that initial run was
`1e1d54aaea7823be4576580c865ecfa90c02cadc63a08b0a592a097a137b03b8`.
A subsequent temporary-directory diagnostic invoked only the failing partial
write test, caught its expected assertion to inspect the manifest, printed the
2,961 / 5,922 / 2,978 byte values above, and cleaned up. That diagnostic wrapper's
exit 0 is **not** a passing regression-test result.

The source owner repaired both defects. Targeted verification ran only the two
previously failing tests, exit 0:

```sh
PYTHONPATH=tools:tests/python python3 -m unittest -v \
  test_contact_peak_retention_errors.ContactPeakRetentionErrorTests.test_manifest_cleanup_failure_does_not_mask_primary_replacement_error \
  test_contact_peak_retention_errors.ContactPeakRetentionErrorTests.test_partial_raw_write_accounts_for_evidence_without_reopening_byte_budget
```

Observed: **two tests in 0.047 s, both passed** on Python 3.12.14, Linux x86_64.
CLI and test SHA-256 values were recorded before and after this command and were
unchanged. The five initially passing tests were not rerun after the repair;
this report does not claim a complete post-repair seven-test run or any whole
Python/C++ suite run.

The verified repair preserves the primary replacement error when cleanup also
fails. It also records the second candidate's actual 17-byte prefix, its SHA-256,
`raw_complete: false`, and its original operational write failure. The final
retained total is correctly 2,978 bytes. The third input still exceeds the
collection limit, proving that the failed raw write does not refund consumed
input allowance. Existing whole-input hashes are not assigned to the partial
prefix. The writer test double delegates ordinary handle operations, while only
its `write` method injects the failure.

The assigned Python file also passed an AST parse, and both owned files passed
scoped trailing-whitespace/newline checks. No broad validation run was needed
to diagnose these two storage defects.

## Tested identities

These hashes bind the repaired-source targeted run and its fixture/helper
dependencies. The initial CLI and test hashes remain above with their original
failing observation.

| Path | SHA-256 |
| --- | --- |
| `tools/validate_contact_peaks.py` | `5bfc879d0a4a42d48667cc2370bafc85a2bd6c6b08c18071c1d60ab7b85be717` |
| `tests/python/test_contact_peak_retention_errors.py` | `9e899b5a5d5c4d19dca7f99cb7a0dc99c3067b3620d3c7dd3c127b78954911e9` |
| `tests/python/test_contact_peak_cli.py` | `02cdf8a827c67962ea1b52307debe30ddee4e8e8082a487eb8a537c1d6865450` |
| `tests/python/test_contact_peak_schema.py` | `8556d104c5492257c2be33e022040c66429dd447b832ea116d51dd571ac2edec` |
| `tools/contact_peak_schema.py` | `af5cbf87d155ecb40c176b78a7527b2a7bd686c7fd515be621af93d910db86c4` |
| `tools/contact_peak_oracles.py` | `74030851ce9511cd50c6952a11b43bfdf5e8da5d7fd10428c009be0210060789` |
| `tools/validate_contact_reference.py` | `c910da5da6b6ad957b709a414cef39a08ed6e8b75c6e36c9d73ef535a064dcdc` |
| `docs/specs/CONTACT_ANALYTIC_PEAK_SLICE_20261123.md` | `56e5caefaebc03596b2ed566d50578b2be2e4dc54e37bf7fdce31dab31243f03` |
| `docs/agents/reports/20261124-w1-08-cli.md` | `0d38119ffc1bd5be952f9bbb1f6f6685759c3d4c789bd693b9cd057819d34292` |
| `docs/agents/reports/20261124-w1-09-retention.md` | `3eabda73cbb2586f7cccc3736cd6584a28d767be0a248f49aeeb1445a5cedb51` |

## Remaining limitations and handoff

The tests emulate an `ENOSPC` write failure, permission-denied cleanup and
replacement errors; they do not exhaust real storage, emulate a kernel crash,
or establish power-loss durability. An unreadable partial artifact after a
storage failure is not separately exercised. Source-change detection uses controlled
before/after hash maps and does not mutate repository source. FIFO/symlink
behavior was observed only on this Linux host. Final-manifest failure can leave
the last successfully written manifest marked `running`; the tested CLI reports
the operational failure on stderr and returns 3 without claiming completion.

No optional timing backend, C++ solver, binary producer, real instrument,
acoustic measurement or cross-platform environment was exercised. Existing
ordinary CLI coverage was inspected rather than repeatedly rerun. Both reported
defects are closed for the exercised boundaries. The integration owner can
include the new test file in the next planned combined verification and retain
this report's original failing observations alongside the repair result.
PHY-002 and general continuous-extrema acceptance remain incomplete.
