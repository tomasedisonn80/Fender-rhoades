# W2-031 — independent retained-extrema boundary tests

Date: 24 November 2026. Actual worker: `w2_031_extrema_raw_tests` (031/100).
Assigned paths: `tests/python/test_contact_extrema_raw.py` and this report only.
Worker 030 owns the product hardening; the integrator owns Git, canonical state,
evidence registration, and existing test-fixture updates. No Git command ran.

## Result and scope

Added **37 focused test methods** with additional adversarial subcases. Final
normal execution passed all 37. The final `python3 -S` execution passed 33 and
explicitly skipped the four NumPy-dependent replay/cache tests. All standard-
library archive inspection, worker protocol, and final provenance tests ran
under `-S`.

Read `AGENTS.md`, `README.md`, `docs/PROJECT_STATE.md`, `docs/ROADMAP.md`,
`project/state.json`, the relevant implementations and existing tests, and the
W2-027 bounded audit. Startup read-only commands returned exit 0:

```sh
python3 tools/project.py validate
python3 tools/project.py sync --check
python3 tools/project.py next --json
```

The observed state was revision 32, 14 tasks, 26 evidence records, no available
unclaimed task, and fidelity `unvalidated`. This was an integrator-assigned
bounded review within the ongoing work; no task claim or state mutation was
made by this worker.

## Independent fixtures and actual coverage

The new fixture encoder uses `struct`, `zipfile`, and the standard library to
write NPY headers and raw numeric payloads. It does not use the production NPZ
serializer, NumPy serialization, `generate_reference`, or a numerical solver.
The usual constant-state fixture has **1,120 array bytes** and a **2,127-byte
compressed NPZ**. It is protocol test data, not an integrated contact trajectory.

| Boundary | Durable checks |
| --- | --- |
| Storage limits | Independently checks the default 163,200,112-byte array cap; derives tightened zero-step and two-step caps; rejects raised, noninteger, boolean, negative, unknown, and nonfinite controls. Header and ZIP allowances remain separately bounded. |
| Exact retained representation | Requires all 11 array names; rejects extra, missing, duplicate, traversal, and extension-altered ZIP members. Checks all 11 shapes and row-count relationships, exact float/int signedness and endianness, object/complex/datetime/bool alternatives, malformed shape fields and SHA-256 strings, and independent archive/payload hash mismatches. |
| Complete and partial records | Accepts a complete mesh/cubic/candidate record, an all-empty capped record, an initial-only mesh, an initial candidate, and partial records with one unreconstructed accepted endpoint. Rejects impossible mesh/dense relations, incomplete records claiming completion, absent candidates in completed records, counter disagreement, and tightened limits exceeded by retained rows. Existing constructed-cubic style records may omit counters and limits while remaining bounded. |
| Header preflight | A tiny NPY header declaring `2**40` elements is rejected before `np.load`; no such array is allocated. A 1 GiB advertised header length is rejected while intercepted ZIP reads remain at most 4,096 bytes. Additional cases cover long literal headers, invalid dimensions/literals, magic/version errors, truncation/trailing bytes, Fortran order, and a central-directory member advertising 2 GiB of uncompressed data before any member is opened. |
| Stable bounded file snapshot | Symlink leaves, symlink ancestors, directories, missing paths, and FIFOs are refused. The FIFO test intercepts `os.open`; it never opens or reads the FIFO. A 111,441-byte ordinary file exceeds the tightened compressed cap and is rejected before opening. A 69,080-byte valid stored NPZ exercises two reads with a 65,536-byte first chunk and no sentinel read. A pathname replacement during the bounded read is rejected. |
| Exact replay and cache identity | NumPy loads the same in-memory bytes that were hashed even when the pathname changes immediately before `np.load`. A later evaluation rejects the changed pathname bytes. Complete and partially retained cubics replay as expected, with uncovered partial time refused. Appended raw bytes cannot reuse a successful pointwise cache entry, and replacing all three reference identities forces fresh evaluation and exposes changed numerical values. |
| Worker protocol and final artifacts | Real `run_reference_worker` validation receives mocked process output: complete and capped records remain inspectable, while wrong schema/kind, missing/extra/wrong source maps, changed source files, malformed/missing/negative/nonfinite peak records, invalid candidate references, and malicious NPY headers cannot complete. Final reports fail provenance when originally retained NPZ, reference JSON, or candidate bytes change or disappear. A mutation between successful raw validation and worker inventory makes the worker inconclusive and preserves the original validated digest in the final expected-identity map. |

The final provenance harness supplies one synthetic parsed candidate and mocks
numerical comparisons, collection results, and coordinate scaling. It runs the
actual six worker protocol validations and the actual filesystem inventories on
tiny archives. The input matrix remains incomplete, and all reports retain
`phy002_acceptance=incomplete`; these tests do not simulate scientific acceptance.
Git identity and optional version queries are replaced with explicit test values.

### Shared valid-worker helper

The test module exports this standard-library fixture helper for the integrator's
existing driver-test updates:

```python
worker_record(path, *, fixture="supported_impact", level="R0", complete=True,
              arrays=None, source_sha256=None)
```

It writes the tiny valid NPZ and returns the matching complete worker protocol.
An isolated fake source directory can supply `source_sha256`. The static fixture
definition comes from the existing independent fixture model; settings,
representation, counter, raw-byte, and peak records are constructed in the test
module. No reference trajectory is generated. A direct `python3 -S` check of this
helper against the actual strict worker validator succeeded before the existing
tests were handed the helper.

## Actual final commands and outcomes

Environment observed: Linux, Python **3.12.14**. Commands ran from the repository
root. Both final commands returned exit 0:

```sh
PYTHONDONTWRITEBYTECODE=1 python3 -m unittest discover \
  -s tests/python -p test_contact_extrema_raw.py -v
```

Observed: `Ran 37 tests in 0.824s` and `OK`; **37 passed, zero skips**.

```sh
PYTHONDONTWRITEBYTECODE=1 python3 -S -m unittest discover \
  -s tests/python -p test_contact_extrema_raw.py -v
```

Observed: `Ran 37 tests in 0.470s` and `OK (skipped=4)`; **33 passed, four explicit
optional NumPy skips**. These are test elapsed times, not performance evidence.

During authoring, the first `-S` run had one test-fixture assertion fail because
the malformed short header was rejected before the intended header-read guard;
adding 16 harmless bytes exercised that guard. The next normal and `-S` runs
each had one harness assertion fail because the new implementation rejected
the intentional validation-to-inventory mutation earlier than the harness
expected. The harness now requires that stronger early rejection and verifies
the final original-digest preservation. No product defect was observed in the
final focused runs. No product module was edited by this worker.

## Tested source identities

These SHA-256 values were captured immediately before the final focused runs and
were unchanged in the after-run check:

| File | SHA-256 |
| --- | --- |
| `tools/contact_extrema_reference.py` | `39bed1d73f4482455c9268846987aa91a4200188b0eb83ff0efbc5759a71263e` |
| `tools/validate_contact_extrema.py` | `65e38326fd3f07566ca37d84513b34a0be24bf916f8d5f4ff541fb2225325dfe` |
| `tests/python/test_contact_extrema_raw.py` | `cda6314a9237f3df73930d83ec6b072eb3ec7fd6610e07799a5e1fead0b6bf2f` |

## Limits and handoff

This is Linux-local behavioral coverage for the retained raw-reference trust
boundary. It does not prove the absence of all filesystem races or malicious
archive forms, and the archive checks do not establish ODE correctness or
continuous-extrema coverage. No huge arrays, decompression bombs, blocking FIFO
reads, live workers, complete R0/R1/R2 references, builds, performance experiments,
or full repository suites were run. Periodic checkpoint NPZs use their separate
format and are outside this final-record suite. Non-Linux execution remains
unverified here; symlink/FIFO capability skips are explicit on unsupported hosts.

Next action: the integrator should combine worker 030's product changes, this
new suite, and the existing-test fixture adjustments, then run the focused
cross-file regression checks and register the actual reports by hash. PHY-002,
RT-001, physical measurements, and fidelity remain at their existing gates.
