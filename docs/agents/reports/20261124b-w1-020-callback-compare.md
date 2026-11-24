# Worker 020 — matched callback comparison harness

## Assignment and boundary

Actual worker `w1_020_callback_compare`, 24 November 2026, continuation B wave 1.
Implemented only `tools/compare_vst3_callbacks.py` and this report. The shared
integrator owns Git and canonical task state. No Git writes, task claims,
evidence registration, model changes, latency changes, dependency additions,
builds, or full performance experiment were performed by this worker.

Read `AGENTS.md`, `README.md`, `docs/PROJECT_STATE.md`, `docs/ROADMAP.md`,
`project/state.json`, `apps/vst3_benchmark.cpp`, `docs/BENCHMARKING.md`, the
existing matrix launcher/validator interfaces, and the retained quadrature
matrix and paired whole-process CPU records. The latter includes setup and
reporting CPU time; the new harness compares the native callback wall-duration
arrays and does not substitute whole-process time for callback duration.

Initial read-only state checks passed:

- `python3 tools/project.py validate`: 14 tasks, 26 evidence records;
  fidelity remains `unvalidated`.
- `python3 tools/project.py sync --check`: generated state current.
- `python3 tools/project.py next --json`: no independently available task;
  PHY-002 and RT-001 remain assigned to `continuation-b-root`.

## Implemented contract

The tool uses only the Python standard library. `--plan` does not inspect or
execute either benchmark binary. It emits a deterministic plan containing all
profiles, exact argv arrays, run order, counts, caps, analysis rules, affinity
policy, limitations, and the harness SHA-256. `--run-plan` reconstructs the plan
from its bounded settings and requires canonical equality with this harness's
schema and hash. Execution cannot override frozen settings.

Each profile runs **baseline, candidate, candidate, baseline**, repeated for the
declared number of cycles. Profile order reverses in even cycles. Adjacent
positions 1/2 and 4/3 are the two matched pairs. There are no retries, adaptive
repetitions, discarded runs, callback removal, or speed-based acceptance gates.

| Profile | Rate (Hz) | Frames | Keys | Workload | Sample bits |
| --- | ---: | ---: | ---: | --- | ---: |
| p01 | 48000 | 64 | 0 | held | 64 |
| p02 | 48000 | 64 | 1 | held | 64 |
| p03 | 48000 | 64 | 12 | held | 64 |
| p04 | 48000 | 64 | 73 | held | 64 |
| p05 | 192000 | 256 | 0 | held | 64 |
| p06 | 192000 | 256 | 1 | held | 64 |
| p07 | 192000 | 256 | 12 | held | 64 |
| p08 | 192000 | 256 | 73 | held | 64 |
| p09 | 48000 | 256 | 12 | pedal | 64 |
| p10 | 192000 | 64 | 73 | pedal | 32 |
| p11 | 192000 | 64 | 12 | gain | 32 |
| p12 | 48000 | 256 | 73 | gain | 64 |

This is a fixed representative subset, not a full factorial matrix. The default
plan has two cycles, 256 measured blocks and 16 warmup blocks per process:
**96 processes, 48 matched pairs, 24,576 measured callbacks, 1,536 warmup
callbacks, and 3,932,160 measured host frames**. These are prospective counts;
this worker did not execute that plan.

### CLI and importable helpers

| Option | Meaning and bound |
| --- | --- |
| `--plan` | Print prospective JSON to stdout; mutually exclusive with execution |
| `--run-plan FILE` | Read and verify the exact frozen plan |
| `--output-dir NEW_DIR` / `--output NEW_DIR` | Required for execution; parent must exist; refuse any existing final path, including symlinks |
| `--competing-activity TEXT` | Required for execution; nonempty honest description, at most 4,096 characters |
| `--baseline PATH` | Plan only; default `build/baseline/rhodes_vst3_quadrature_benchmark` |
| `--candidate PATH` | Plan only; default `build/vst3/rhodes_vst3_quadrature_benchmark` |
| `--profile pNN` | Plan only; repeat to select distinct profiles from the fixed 12; default all |
| `--blocks N` | Plan only; 1–1,024; default 256 |
| `--warmup-blocks N` | Plan only; 0–64; default 16 |
| `--cycles N` | Plan only; 1–4; default 2 |
| `--timeout-seconds N` | Plan only; 1–120 per subprocess; default 60 |
| `--cpu N` | Plan only; explicit Linux CPU ID; requires an observed permitted mask and working affinity API at execution |

Principal independently testable helpers are `make_plan`, `load_plan`,
`strict_json`, `timing_statistics`, `validate_benchmark`, `matched_comparison`,
`source_snapshot`, `input_snapshot`, `affinity_snapshot`, `run_process`, and
`execute_plan`. `EvidenceStore` enforces exclusive files, total byte limits and
final retained-byte verification. `_child` is an internal wrapper invocation,
not a separate user-facing protocol.

The maximum is 12 profiles / four cycles / 192 subprocess attempts. Each stdout
is capped at 256 KiB and stderr at 64 KiB. The capture has a 128 MiB total budget,
including an 8 MiB final-record reserve. Oversize streams retain their prefix,
are explicitly marked incomplete, and invalidate the capture. Source inventory
is capped at 4,096 files / 128 MiB. Individual retained metadata files are
capped at 16 MiB. Timeout totals in the plan are sums of configured budgets,
not a scheduler-independent bound on launcher wall time; bounded process and
stream cleanup follows a kill.

Exit 0 requires every planned run, final input hashes, and retained artifacts to
verify. Invalid/incomplete attempted captures return 1; argument/preflight
validation outside capture uses 2, and operational setup/storage failures use 3.
The manifest records actual exit status, errors, attempted runs and unattempted
run IDs. A malformed frozen plan encountered after exclusive directory creation
is retained as an operational failure manifest rather than a successful run.

### Evidence and validation

Each run retains exact raw `.stdout.json` and `.stderr.txt` bytes, child affinity
metadata, and an execution JSON with command arrays, working directory, return
code, timeout status, whole-process elapsed time, stream limits/errors, and
independently verified callback statistics. Whole-process elapsed time is
labeled separately and is not used as a callback statistic.

Strict JSON rejects duplicate keys, non-finite constants/numbers, truncated
documents, trailing content and invalid UTF-8. The benchmark validator requires
the exact expected schema, strict integer/boolean types, finite numeric fields,
the Q6 model-3/six-node/48-frame identity, the frozen machine label, expected
workload and host-getter accounting, complete warmup/measurement, owner/cursor
state, finite stereo output and correct output/silence observations. It
recomputes sorted native mean, nearest-rank p50/p95/p99, maximum, period and
strictly-greater-than-period miss count from every raw timing.

Every repeat and both binaries must produce the same workload and exact output
record: FNV-1a64, checksum, peak, nonzero sample count, finite/stereo/silence and
output-presence flags. A mismatch stops the capture and prevents a comparison
summary. The validator handles the initial one-frame Q6 silence rule; the
declared 64/256-frame matrix always spans completed cells for excited inputs.
Zero-key profiles explicitly expect silent output. The raw executable's first
zero sample and reported 48-frame kernel latency are preserved.

The final comparison contains every adjacent candidate/baseline p50, p99,
maximum and mean ratio; per-profile medians of those paired ratios; separate
per-variant run-statistic medians, observed maxima and miss inventories; and
the individual miss counts. A zero baseline denominator is null with an
explicit reason. There is no pooled percentile or cross-profile speedup.
All comparisons are labeled **observational, not WCRT or a causal speedup**.

Current working-tree source and binary SHA-256 identities are captured before,
between and after processes, including file identity metadata to detect
replacement. The source scope is `CMakeLists.txt`, this harness, the callback
benchmark, and `include`, `src`, `plugins/vst3`, `cmake`. Available nearby CMake,
compile-command, flags/link and baseline provenance files are preserved and
rechecked; missing files are explicitly unavailable. Hardware, kernel,
container/load/power observations and Python details are captured before/after;
compiler, flags, sample format and source labels are preserved from raw output.

No CPU is selected by default. The launcher and pre-exec child report the
observed inherited Linux mask. Explicit `--cpu` is checked/applied before exec
and verified by the same process; failure does not silently fall back. Other
operating systems explicitly record unsupported affinity. Scheduling priority
and power settings remain inherited. No native non-Linux or explicit-pinning
execution was performed in this worker's checks.

## Actual verification

1. `python3 -I -S` parsed the tool with `ast.parse` and invoked `--plan` with
   `/nonexistent/baseline` and `/nonexistent/candidate`. Passed with site
   packages disabled; verified 12 profiles, 96 runs, ABBA order, reversed
   second-cycle profile order, default inherited affinity and all default
   totals. No benchmark was executed.
2. Created `build/w1-020-callback-smoke-20261124b.plan.json` exclusively using:

   ```sh
   python3 -I -S tools/compare_vst3_callbacks.py --plan --profile p02 \
     --blocks 2 --warmup-blocks 0 --cycles 1 --timeout-seconds 10
   ```

   Ran the frozen plan with:

   ```sh
   python3 -I -S tools/compare_vst3_callbacks.py \
     --run-plan build/w1-020-callback-smoke-20261124b.plan.json \
     --output-dir build/w1-020-callback-smoke-20261124b \
     --competing-activity "Implementation smoke only: other campaign workers may perform bounded tests or edit unrelated files; no performance conclusion."
   ```

   Exit 0. Four ABBA processes, **eight total measured callbacks**, no warmup;
   all run/output/input/artifact checks passed. Capture UTC:
   `2026-11-24T08:19:15.200650+00:00` to
   `2026-11-24T08:19:15.454176+00:00`. Retained 30 hashed artifacts before the
   manifest, 335,547 bytes. The inherited CPU mask was `[0,1,2,3,4,5,6,7,8]`,
   observed on Linux 6.18.57 x86_64 with an AMD EPYC 9V74 CPU label. This small
   smoke is implementation verification and supports no performance conclusion.
3. An isolated standard-library Python driver loaded the smoke's native JSON
   and verified rejection of a boolean duration, wrong p99, missing workload
   field, truncated JSON, duplicate JSON key and NaN. A synthetic
   `[1, 2, 10000]`-microsecond vector retained its maximum/p99 and one nominal
   100-microsecond-period miss. These are parser/statistics tests, not measured
   timing observations.
4. Reusing the smoke output path returned 2 and left its manifest byte-identical.
5. Two mock executable captures under
   `build/w1-020-callback-failure-checks-20261124b` exercised malformed `{}`
   stdout and a one-second timeout against a five-second sleeper. Both returned
   1, stopped after the first attempt, retained raw streams and execution errors,
   listed three unattempted runs and omitted `comparison.json`. No further
   actual benchmark process was executed.

The full default paired experiment was not run. The larger independent durable
test suite is assigned to worker 024; full capture is assigned to worker 021
after the integrator freezes source, binaries and provenance activity.

## Frozen identities

| Item | SHA-256 |
| --- | --- |
| Harness, also the version used for all checks above | `341b92d74331c00c2e9d3598dc31c1e6a53e0ffdd426c471f55e9bb9a8c30dcd` |
| `apps/vst3_benchmark.cpp` | `2129c52bda0337b4cfa71fc39eab31e66696883977dfbfc3c8d52e69ebccc2ed` |
| `docs/BENCHMARKING.md` read for this task | `02439bddb473c9abeb46714ee48c84030f2e06bafe8144e70a2b61214b82a4e8` |
| Immutable baseline binary | `f6ffbc20500a173528d911c58997fb0be1569f0fb072dfd66c57fc8716518b31` |
| Current candidate binary observed by smoke | `e97af0f971eb38e713b57713cb9bb435381758432b6f15e2fea2b12ff7ac1c6f` |
| Smoke source manifest, 27 files including identity metadata | `6fa8596b52ae14fd4eaafeaeeaf7e584407bf296c59a55bd69b3a0d94b003ba8` |
| Exact smoke plan bytes | `3948325239ebac4b7ae6833a6b249e1f20b024b8ee7e62529748c7fc3b176ffc` |
| Smoke manifest | `2d51e89a3c2e988504f4abcfae7c8329ee8cae8f56fabf1539d64cf4f2492342` |
| Smoke comparison | `005426fb9b3ab8a837db877eaf10ffb4831a925540f6d5f2930b23861ee7aa47` |

All four smoke outputs had FNV-1a64 `7f79e623a8dea0b9`, checksum
`0.7240022927611058`, peak `0.11216475074575984` and 254 nonzero channel
samples, with finite/stereo/silence/presence checks true. Source, binaries,
plan and available build metadata remained unchanged throughout the capture.

The baseline reports Release / GCC 14.2.0 / NDEBUG true / fast-math macro false,
but complete flags and source revision/diff are `unrecorded`; sanitizer/LTO
fields are `not recorded`. Its absent `compile_commands.json` was not inferred.
The retained baseline provenance manifest claims source commit
`8813b2680a49c07c277f0d0e932843fdb356c3ee`; this harness preserves that claim and
its bytes, without reconstructing or authenticating a historical source build.
Current source hashing excludes SDK source and does not prove correspondence
between every compiled object and source file. These boundaries remain explicit
when interpreting the later experiment.

## Handoff

The tool is frozen at the hash above. Worker 021 should generate a **new** plan
using `--plan`, retain its exact bytes, then execute `--run-plan` with a new
output directory and an accurate competing-activity statement. The integrator
should establish the intended quiet interval first. Worker 024 should audit
byte-cap, mutation, affinity, child metadata, interruption and ratio edge paths
with mocks and retain any required correction as a separately reported version.

This delivery does not close RT-001: allocation/locking evidence, numerical
convergence/error gates, calibrated model work and host/device timing remain
separate requirements.
