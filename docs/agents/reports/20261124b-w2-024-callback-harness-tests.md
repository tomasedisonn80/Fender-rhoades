# Worker 024 — callback comparison harness tests

## Scope and context

Actual worker `w2_024_callback_harness_tests`, continuation B wave 2,
24 November 2026. Assigned and changed only
`tests/python/test_callback_compare.py` and this report. The integrator owns
Git, canonical state, evidence registration and acceptance decisions. No
product module, benchmark, binary, retained capture or existing report changed.

Read `AGENTS.md`, `README.md`, `docs/PROJECT_STATE.md`, `docs/ROADMAP.md`,
`project/state.json`, the frozen comparison harness, native benchmark schedule
and summary implementation, worker 020's implementation report, and existing
matrix tests. Initial commands passed:

- `python3 tools/project.py validate`: 14 tasks, 26 evidence records;
  fidelity remains `unvalidated`.
- `python3 tools/project.py sync --check`: generated state current.
- `python3 tools/project.py next --json`: revision 32, no available task;
  specimen and acquired-data blockers remain.

## Added verification

Added **30 standard-library unittest methods**, with adversarial subcases.
Fixtures are constructed directly from the benchmark contract; they do not
load retained measurements, import external packages, build a target, or execute
a native benchmark. Explicit tables supply all twelve profiles' note events,
parameter points and owner counts. Boundary cases independently distinguish
blocks 375/376 and 94/95; warmup is excluded from measured accounting.

Plan checks freeze the twelve-profile matrix, default counts, maximum caps,
canonical subset order, ABBA positions and even-cycle profile reversal. They
check exact argv, deterministic plans, absent-binary operation, strict bounds,
canonical schema/hash enforcement and exact retained plan bytes. JSON and
benchmark checks reject duplicate keys, non-finite values, invalid UTF-8,
trailing data, extra/missing fields, type coercion, changed Q6 identity,
incomplete counters, forged summaries and inconsistent output presence.

Hand-checkable timing vectors distinguish nearest-rank percentiles from
interpolation, preserve observations immediately below/equal/above a deadline,
and verify sorted native accumulation. Ten binary64 `0.1` observations require
native mean `0.09999999999999999`, while the separately reported `fsum` mean is
`0.1`; substituting the latter into the native summary is rejected. Zero-key
silence and the single initial Q6 frame are accepted only for their respective
workloads. Pair checks distinguish the median of adjacent 1/2 and 4/3 ratios
from a ratio of cross-run medians and retain explicit null-denominator reasons.

Capture tests mock `Popen` using in-memory pipes and deterministic clocks,
while exercising real temporary evidence files and input identity snapshots.
They verify a complete four-run capture and failures for stdout/stderr caps,
timeout, a late zero exit, launch error, nonzero exit and duplicate-key output.
Each failure retains hashed raw streams/execution records, lists unattempted
runs, stops without retry and omits `comparison.json`. One-ULP checksum/peak
changes, sample counts and fingerprints detect candidate/repeat mismatches.
Source, both binaries, frozen plan, build metadata and same-byte source
replacement mutations are detected. Existing files/directories/dangling
symlinks are refused; retained-artifact mutation and final-byte reserve are
also checked.

## Actual commands and results

Python was **3.12.14**. Both focused commands passed with **30 tests, no skips**:

```sh
python3 -m unittest discover -s tests/python -p test_callback_compare.py -v
python3 -S -m unittest discover -s tests/python -p test_callback_compare.py -v
```

Reported unittest durations were 0.212 seconds and 0.207 seconds respectively.
No failing regression or product correction was required. No full suite,
native build, full performance capture, real timeout, affinity change or
non-Linux execution was performed. These tests establish harness behavior
under controlled inputs, not measured speed, host deadlines or fidelity.

## Tested identities and handoff

| Item | SHA-256 |
| --- | --- |
| Added test file | `e4298955bc0115e8e8b8d3c3ec4e3e60ccb757ef656efc3e93b1bbfed43961bb` |
| Frozen harness | `341b92d74331c00c2e9d3598dc31c1e6a53e0ffdd426c471f55e9bb9a8c30dcd` |
| Read-only native benchmark | `2129c52bda0337b4cfa71fc39eab31e66696883977dfbfc3c8d52e69ebccc2ed` |

Next step: the integrator can include this file in ordinary Python discovery,
register the immutable report and retain native observations separately.
RT-001 and PHY-002 are not completed by this test evidence.
