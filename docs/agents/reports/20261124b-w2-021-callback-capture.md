# Worker 021 — initial paired callback capture

## Result and scope

Actual worker `w2_021_callback_capture`, continuation B wave 2, 24 November 2026.
Executed the **single predeclared full capture** with the frozen
[worker 020 harness](20261124b-w1-020-callback-compare.md). All **96 processes,
48 matched pairs, 24,576 measured callbacks and 1,536 warmup callbacks** completed
and verified. There were no failed, timed-out, truncated, missing, discarded or
retried runs. The capture contains 3,932,160 measured host frames.

The comparison is **observational candidate/baseline callback duration** for the
synthetic Q6 model-3 processor with six quadrature nodes and 48-frame latency.
It does not establish causal speedup, WCRT, DAW/device behavior, allocation or
locking guarantees, numerical convergence, calibrated fidelity, or completion of
RT-001. Missing historical baseline build flags remain a material limitation.
No cross-profile timing distribution or overall mixed-workload speedup is formed.

Capture UTC: `2026-11-24T08:28:43.145418+00:00` to `2026-11-24T08:29:07.938061+00:00`;
launcher manifest interval 24.792643 seconds, which includes orchestration
and is **not** a callback statistic. Exit status 0; manifest `complete=true`,
`errors=[]`, no unattempted run IDs. The root was notified immediately after the
launcher finished, before archive packaging or renewed heavy work.

## Assignment and preparation

Read `AGENTS.md`, `README.md`, `docs/PROJECT_STATE.md`, `docs/ROADMAP.md`,
`project/state.json`, worker 020's handoff, `docs/BENCHMARKING.md`, the comparator,
and the relevant native benchmark implementation. Read-only state commands passed:

- `python3 tools/project.py validate`: 14 tasks, 26 evidence records;
  fidelity `unvalidated`.
- `python3 tools/project.py sync --check`: generated state current.
- `python3 tools/project.py next --json`: revision 32, available work `[]`;
  existing PHY-002/RT-001 ownership remains with `continuation-b-root`.

The integrator supplied GO only after publishing source revision
`caed228aec545460660bd239458123b3239d1c7e` and ending its builds/tests. Git HEAD was checked
against that revision immediately before launch. The launch context recorded
`?? tools/validate_continuation_b.py`; this untracked campaign validator was
outside the capture's declared source inventory. The tracked capture inputs
remained stable throughout.
The integrator reported 300 native tests and 700 Python tests passing, with the
optional probe-environment skip separately resolved. Those are the integrator's
checks, not tests rerun by this worker. This worker performed no builds, source
edits, Git mutations, task claims, evidence registration or canonical state edits.

CPU 8 was chosen **before any timing** as the highest ID in the observed permitted
Linux mask `[0,1,2,3,4,5,6,7,8]`. The fixed choice keeps every baseline/candidate
subprocess on the same CPU to reduce migration; it was not selected by comparing
timings or observed load. The integrator accepted this choice before GO. Every
pre-exec child record verified actual affinity `[8]`. Affinity provides no CPU
isolation or exclusive use.

## Frozen plan and actual commands

Plan generation, with defaults left unchanged except the explicit observed CPU:

```sh
python3 -I -S tools/compare_vst3_callbacks.py --plan --cpu 8
```

Its stdout was exclusively retained as
`build/w2-021-callback-capture-20261124b/plan.json`, then used without override:

```sh
python3 -I -S tools/compare_vst3_callbacks.py \
  --run-plan build/w2-021-callback-capture-20261124b/plan.json \
  --output-dir build/w2-021-callback-capture-20261124b/capture \
  --competing-activity 'Root confirmed all native/Python builds and tests ended, and source/binaries/build metadata frozen before GO. Root will avoid source/metadata edits, builds and heavy commands until capture ends. Workers 022 and 023 are restricted to light text/JSON work. Shared-host external load is uncontrolled; CPU 8 affinity is not exclusive CPU isolation.'
```

The archived launch context preserves the exact Python executable and argv;
each run row preserves native and wrapper argv, timestamps, working directory,
status, errors, whole-process duration, exact raw stdout/stderr, output identity
and independently checked callback statistics. Whole-process durations are not
used in any callback ratio.

Every process measures 256 callbacks after 16 warmup callbacks. There are two
ABBA cycles per profile: baseline, candidate, candidate, baseline. Profile order
is reversed in cycle 2. Adjacent pairs are positions 1/2 and 4/3, yielding four
candidate/baseline ratios per profile. There is no adaptive repetition, outlier
removal, timer-overhead subtraction, priority change or speed-based acceptance
threshold. The per-process timeout is 60 seconds; 5,760 seconds is the sum of
configured budgets, not a bound on total wall time.

| Profile | Rate (Hz) | Frames | Selected keys | Schedule | Sample bits |
| --- | --- | --- | --- | --- | --- |
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

This is the frozen representative subset, not a factorial design or a population
sample of real sessions. All 73 physical-key slots and pickup branches remain in
the synthetic processor even in zero-selected-key profiles.

## Environment and build provenance

The visible CPU label was `AMD EPYC 9V74 80-Core Processor`; the container exposed
9 logical CPUs. The OS was Debian GNU/Linux 13.7 (trixie), kernel 6.18.57, x86_64,
glibc 2.41. Python was 3.12.14. The CPU label does not establish exclusive access
to 80 physical cores. Machine snapshots retain `/proc/cpuinfo`, memory, load,
process/cgroup observations, OS release and availability errors before/after.

Observed `/proc/loadavg` was `1.17 1.99 4.08 2/251 9`
before and `1.89 2.09 4.06 4/270 297` after.
These are host observations, not an attribution of callback outliers. Both
`/sys/fs/cgroup/cpu.max` and `cpuset.cpus.effective` were unavailable at the probed
paths; no unlimited-quota claim is inferred. CPU 8's frequency governor was also
unavailable. Priority/scheduler and power policy were inherited; child records
reported nice values `[0]` and scheduler codes `[0]`. No policy was
changed. Steady-clock resolution was not independently probed; microsecond
output units do not establish timer resolution. Flush-to-zero was not inspected.

The competing-activity declaration was:

> Root confirmed all native/Python builds and tests ended, and source/binaries/build metadata frozen before GO. Root will avoid source/metadata edits, builds and heavy commands until capture ends. Workers 022 and 023 are restricted to light text/JSON work. Shared-host external load is uncontrolled; CPU 8 affinity is not exclusive CPU isolation.

Both executables report Release, GCC 14.2.0, `NDEBUG=true`, and
`fast_math_macro=false`. Both embedded source revision/diff and complete flags
are `unrecorded`; sanitizer and LTO fields are `not recorded`. The raw native
`affinity=uncontrolled` field is preserved unchanged; the external same-process
pre-exec records provide the observed CPU-8 affinity evidence.

Available candidate CMake cache, all 263 compile-command entries, target flags
and link command are retained exactly. Its observed benchmark command contains
`-O3 -DNDEBUG -std=gnu++20`, `-Wall -Wextra -Wpedantic -Wconversion -Wshadow
-Werror -DRELEASE=1`, and the quadrature/Release definitions. Those are available
current-tree command records, not reconstruction of all historical compiled
objects or toolchain defaults. The full SDK/include/link details remain in the
archive.

The baseline's CMake cache, compile commands, Ninja files, target flags and link
command are absent. Its two retained provenance manifests claim source commit
`8813b2680a49c07c277f0d0e932843fdb356c3ee`; this report preserves that claim and the
original bytes without authenticating a fresh historical build. **Complete
baseline flags and historical source/build correspondence remain unknown.** No
flags are inferred from the Release/compiler labels, current candidate metadata,
output identity or timing ratios.

| Frozen identity | SHA-256 |
| --- | --- |
| Harness | `341b92d74331c00c2e9d3598dc31c1e6a53e0ffdd426c471f55e9bb9a8c30dcd` |
| Source inventory before and after | `986fdb90de698ab230d0224def54fb08c0095ca96d0394dd5c3a7e43ff57f78d` |
| Baseline binary before and after | `f6ffbc20500a173528d911c58997fb0be1569f0fb072dfd66c57fc8716518b31` |
| Candidate binary before and after | `e97af0f971eb38e713b57713cb9bb435381758432b6f15e2fea2b12ff7ac1c6f` |
| Exact original capture manifest | `445a0b6b6199d64c0831e44e59f06b5cc64deac7a96bd338adad768c6e177cfa` |

Both binary files are 308,144 bytes. The 27-file source inventory includes
`CMakeLists.txt`, this harness, the benchmark, `include`, `src`, `plugins/vst3`
and `cmake`; it excludes SDK source and historical baseline reconstruction.
Its digest binds the per-file hashes **and filesystem identity metadata**, not
just Git content. Source, both binaries, frozen plan and every available build
metadata file were unchanged in the before/after snapshots and the harness's
checks around every subprocess. This does not prove that every compiled object
corresponds to the captured source inventory.

Available build metadata was inspected before embedding for credential-like
keys/assignments, authenticated URL userinfo and private-key blocks. No findings
required omission; every available capture artifact is retained. No process
environment values or credentials were copied. Raw native output follows the
source-inspected benchmark schema.

## Per-profile matched ratios

Values below are **medians of four adjacent candidate/baseline ratios**. Lower
values indicate shorter observed durations. Native p50/p99 use nearest rank;
the median across four pair ratios averages the central two values. These are
not ratios of the displayed run-statistic medians, ratios of global maxima,
confidence intervals or independent-sample causal estimates. Full pair ratios
are in the separate comparison JSON. No denominator was zero for duration ratios.

| Profile | p50 ratio | p99 ratio | Maximum ratio | Mean ratio |
| --- | --- | --- | --- | --- |
| p01 | 0.424387 | 0.651342 | 1.334414 | 0.480750 |
| p02 | 0.578089 | 0.403980 | 0.453911 | 0.581316 |
| p03 | 0.513783 | 0.514171 | 0.612343 | 0.516451 |
| p04 | 0.980489 | 0.952492 | 0.910966 | 0.971637 |
| p05 | 0.431185 | 0.413895 | 0.313553 | 0.419258 |
| p06 | 0.441751 | 0.462283 | 1.083162 | 0.450160 |
| p07 | 0.515736 | 0.535882 | 0.849370 | 0.523496 |
| p08 | 1.014221 | 1.012298 | 0.955909 | 0.998207 |
| p09 | 0.529321 | 0.707375 | 0.912586 | 0.537849 |
| p10 | 0.991050 | 0.985538 | 1.159540 | 0.992228 |
| p11 | 0.498048 | 0.428355 | 0.429212 | 0.478998 |
| p12 | 1.007614 | 1.018082 | 0.962016 | 1.024544 |

For context, these are separate medians of the four per-run p50/p99 statistics
for each variant; they do not pool raw callbacks:

| Profile | Baseline median run p50 (µs) | Candidate median run p50 (µs) | Baseline median run p99 (µs) | Candidate median run p99 (µs) |
| --- | --- | --- | --- | --- |
| p01 | 302.6585 | 128.5050 | 546.8205 | 325.9885 |
| p02 | 298.5475 | 171.4990 | 611.8265 | 267.9265 |
| p03 | 301.2710 | 152.1300 | 487.5475 | 254.5875 |
| p04 | 301.7975 | 297.5760 | 527.0115 | 516.1945 |
| p05 | 1258.4845 | 529.8705 | 1961.7160 | 778.7640 |
| p06 | 1210.8945 | 538.7585 | 1678.4170 | 1005.2945 |
| p07 | 1227.0180 | 627.2090 | 1864.6875 | 911.0650 |
| p08 | 1214.7100 | 1245.6910 | 1845.5695 | 1929.4690 |
| p09 | 1364.1210 | 712.8415 | 2474.8315 | 1389.9345 |
| p10 | 332.1370 | 325.3315 | 585.4375 | 541.6575 |
| p11 | 304.8315 | 151.9850 | 593.5395 | 248.2830 |
| p12 | 1209.1865 | 1211.8750 | 1735.6515 | 1727.9505 |

The sparse/silent and 12-key profiles have shorter observed candidate p50
medians of paired ratios in this capture. The 73-key p50 ratios are near one.
Tail behavior is not uniformly shorter: matched maximum ratios exceed one in
p01, p06 and p10. These observations do not identify a cause, separate build
configuration effects, or establish performance on another host or workload.

## Every profile's maximum and nominal-period misses

Each variant contributes four runs / 1,024 measured callbacks per profile.
A miss means a recorded callback duration **strictly exceeded**
`1e6 * block_frames / sample_rate_hz`; it is not a measured device underrun.
No callback, outlier or miss was removed.

| Profile | Nominal period (µs) | Baseline maximum (µs) | Candidate maximum (µs) | Baseline misses / 1,024 | Candidate misses / 1,024 |
| --- | --- | --- | --- | --- | --- |
| p01 | 1333.333333 | 683.313 | 3438.455 | 0 | 2 |
| p02 | 1333.333333 | 3218.188 | 5191.082 | 7 | 3 |
| p03 | 1333.333333 | 1378.502 | 601.120 | 1 | 0 |
| p04 | 1333.333333 | 1698.567 | 1832.525 | 1 | 1 |
| p05 | 1333.333333 | 4680.545 | 1279.906 | 364 | 0 |
| p06 | 1333.333333 | 5568.330 | 4573.336 | 289 | 5 |
| p07 | 1333.333333 | 5405.109 | 6702.621 | 242 | 4 |
| p08 | 1333.333333 | 4358.828 | 3628.537 | 293 | 314 |
| p09 | 5333.333333 | 4191.024 | 4680.455 | 0 | 0 |
| p10 | 333.333333 | 1109.804 | 3700.002 | 457 | 389 |
| p11 | 333.333333 | 1496.307 | 398.440 | 358 | 4 |
| p12 | 5333.333333 | 4021.408 | 2437.912 | 0 | 0 |

For example, p01 has two candidate misses despite the shorter p50 paired ratio;
p08 has 314 candidate misses versus 293 baseline misses; and p10 has substantial
miss counts in both variants at its 333.333333-µs nominal period. Zero misses
elsewhere apply only to those finite offline observations. Observed maxima are
not WCRT bounds and do not support a universal tail improvement claim.

### Complete run maximum/miss inventory

Every entry below is retained, including all zeros. Within each row, values are
in chronological run order for that profile and variant: baseline positions
`c01/1, c01/4, c02/1, c02/4`; candidate positions
`c01/2, c01/3, c02/2, c02/3`. Each listed run has 256 measured callbacks. Table
values are displayed to 0.001 µs; exact numeric spellings remain in raw stdout.

| Profile | Variant | Four run maxima (µs) | Four miss counts |
| --- | --- | --- | --- |
| p01 | baseline | 580.110, 508.183, 683.313, 612.687 | 0, 0, 0, 0 |
| p01 | candidate | 3438.455, 1091.488, 356.008, 247.286 | 2, 0, 0, 0 |
| p02 | baseline | 569.123, 854.977, 559.118, 3218.188 | 0, 0, 0, 7 |
| p02 | candidate | 5191.082, 225.314, 360.234, 289.088 | 3, 0, 0, 0 |
| p03 | baseline | 653.418, 494.072, 780.716, 1378.502 | 0, 0, 0, 1 |
| p03 | candidate | 360.465, 332.523, 601.120, 386.682 | 0, 0, 0, 0 |
| p04 | baseline | 575.923, 1698.567, 769.400, 649.693 | 0, 1, 0, 0 |
| p04 | candidate | 1832.525, 561.381, 434.354, 816.921 | 1, 0, 0, 0 |
| p05 | baseline | 2803.644, 1818.174, 4680.545, 3094.795 | 60, 60, 139, 105 |
| p05 | candidate | 817.191, 799.905, 1279.906, 1038.709 | 0, 0, 0, 0 |
| p06 | baseline | 1586.440, 5568.330, 2495.067, 2582.276 | 28, 168, 55, 38 |
| p06 | candidate | 931.781, 3553.875, 3812.699, 4573.336 | 0, 3, 1, 1 |
| p07 | baseline | 5405.109, 4131.907, 2248.782, 3072.382 | 76, 7, 84, 75 |
| p07 | candidate | 6702.621, 1247.669, 1031.488, 4142.557 | 3, 0, 0, 1 |
| p08 | baseline | 4358.828, 1519.031, 4173.344, 1659.810 | 67, 30, 145, 51 |
| p08 | candidate | 3281.903, 2074.284, 3628.537, 1730.124 | 95, 52, 127, 40 |
| p09 | baseline | 2713.942, 4191.024, 3757.647, 2559.593 | 0, 0, 0, 0 |
| p09 | candidate | 2658.448, 3544.011, 4680.455, 1145.828 | 0, 0, 0, 0 |
| p10 | baseline | 1109.804, 630.194, 659.618, 486.130 | 130, 112, 151, 64 |
| p10 | candidate | 989.957, 3700.002, 712.266, 602.443 | 106, 88, 156, 39 |
| p11 | baseline | 523.025, 1496.307, 839.464, 763.772 | 191, 37, 38, 92 |
| p11 | candidate | 367.354, 351.641, 282.689, 398.440 | 1, 2, 0, 1 |
| p12 | baseline | 1674.732, 2596.287, 4021.408, 1831.484 | 0, 0, 0, 0 |
| p12 | candidate | 1827.397, 2437.912, 1762.001, 1804.072 | 0, 0, 0, 0 |

## Output compatibility

All eight runs of each profile had identical workload records, FNV-1a64 output
hash, checksum, peak, nonzero sample count and output-integrity booleans across
baseline/candidate and both cycles. All callbacks returned success; finite
output, stereo identity, silence flags, expected output presence, owner/cursor
state and host getter counts verified. Zero-key profiles correctly remained
silent. This exact synthetic output compatibility is separate from numerical
reference convergence and calibrated fidelity.

| Profile | Output FNV-1a64 | Checksum sum squares | Peak digital output | Nonzero channel samples |
| --- | --- | --- | --- | --- |
| p01 | `9c735bed0a722325` | 0 | 0 | 0 |
| p02 | `1beaabcc9f6a83f5` | 102.07359302368687 | 0.11216475074575984 | 32766 |
| p03 | `835936dc601f34c1` | 1177.4919075689334 | 1.2977678667314 | 32766 |
| p04 | `3cc87872e249a7b5` | 7505.641406074976 | 7.975943097862642 | 32766 |
| p05 | `a96777069d622325` | 0 | 0 | 0 |
| p06 | `3756038ecad946a9` | 408.6706088701243 | 0.11205471757008655 | 131070 |
| p07 | `b57d78de0ad627e5` | 4736.317589046908 | 1.3153060515975583 | 131070 |
| p08 | `c0303d0c66c73539` | 30136.363606164257 | 8.061397874279333 | 131070 |
| p09 | `0f4c9b1a5e75d995` | 456.39241893437367 | 1.2975518141549311 | 131070 |
| p10 | `904425856185601d` | 7212.3416248406775 | 8.060991287231445 | 32766 |
| p11 | `842a3145a1408699` | 1771.4608853204315 | 1.7563961744308472 | 32766 |
| p12 | `9befece42524d07d` | 50223.60648499602 | 11.106350232070422 | 131070 |

## Durable evidence and verification

| Artifact | Bytes | SHA-256 |
| --- | --- | --- |
| [callback-paired-initial-20261124b.json](../../validation/callback-paired-initial-20261124b.json) | 2832730 | `a8860915e155246c2746ea58cd826004d05714e519aaed200f4fbe5c341d733f` |
| [callback-paired-initial-20261124b-plan.json](../../validation/callback-paired-initial-20261124b-plan.json) | 63722 | `0476e5f559038937341d12cfd6375ebb8edaa3e7e36b0da5ce56857af2073891` |
| [callback-paired-initial-20261124b-comparison.json](../../validation/callback-paired-initial-20261124b-comparison.json) | 59537 | `c0cc1a5c5fcf13c357741226b0a0ad05ecd3909736353527bfae78618b1532d2` |
| [callback-paired-initial-20261124b-verification.json](../../validation/callback-paired-initial-20261124b-verification.json) | 1266 | `7c5c7019318363709e03db85cbefea9c075435d5ab7970b0ac00330df328a1ca` |

The compact archive contains **399 exact capture files**: all 96 raw stdout and
96 raw stderr streams, 96 execution records, 96 child-affinity records, the exact
manifest/plan/comparison, input-hash and machine snapshots, available build
metadata and the initial attempt record. It also includes the preparation and
launch records, exact launcher stdout/stderr, and the scratch packaging script.
Native stderr was empty for all 96 runs; launcher progress/error streams were
retained separately. The original manifest lists 398 hashes before itself and
1,732,251 retained bytes before manifest serialization.

`runs[*].stdout.raw_utf8` and `runs[*].stderr.raw_utf8` contain the exact raw
streams. Other capture files are in `retained_other_artifacts[*].raw_utf8`.
Encoding those strings as UTF-8 reconstructs the exact bytes, checked against
the accompanying byte counts and SHA-256 values. The current working build
directory is not needed to read or recompute the evidence. The separate plan and
comparison are byte-identical copies of the capture originals.

Actual archive verification used standard-library Python only, with no benchmark
execution. It checked all 398 manifest-listed artifact hashes; serialized and
parsed the compact archive; reconstructed all 399 files byte-for-byte; recomputed
all 96 native nearest-rank p50/p95/p99, sorted-accumulation means, maxima and miss
counts from all 24,576 raw durations; recomputed all 192 duration ratios and 48
miss-ratio records; and checked all 12 profile summaries and all eight matching
output identities per profile. The packager source and a separate machine-readable
verification record retain these checks. Missing/failed attempts would have been
retained; none occurred in this capture.

`python3 tools/check_docs.py` passed. A separate readback from the durable archive
checks its hash, reconstructs the 399 files without reading the build directory,
checks the byte-identical standalone plan/comparison and resolves this report's
local links. During that review, a draft claim of a clean working tree was
corrected to the actual untracked-validator observation above; the archived
launch context already retained the correct status. No timing or archive data
was rerun or replaced for that documentation correction.

## Handoff and limits

Only the four validation artifacts listed above and this report were added for
integration. Local capture/preparation/packaging intermediates remain under
`build/w2-021-callback-capture-20261124b`. No passing capture was rerun for more
favorable outliers. No new dependencies were added.

A later predeclared experiment with fresh baseline/candidate builds, exact
source/SDK identities and complete matching compiler/link commands can address
the missing build-control evidence. It must remain a separate result and must
preserve this observation. Scheduling, shared-host contention, frequency,
thermal/cache conditions, timer resolution, short-run tails, unsupported
platforms, DAW/device deadlines, allocation/locking, numerical errors and
physical-model validation remain outside this capture's claims. RT-001 remains
in progress under its existing acceptance criteria.
