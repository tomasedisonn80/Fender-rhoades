# Batched quadrature observation — 24 November 2026

The six-node observation now loads each modal state once and evaluates the six prepared fractional transitions together. The private transition storage keeps node coefficients adjacent. The nonlinear pickup still follows each key's modal sum, and all 73 electrical branches and their summation order remain present. Scalar observation remains available as a separately traversed comparison. State model 3, 48-frame latency, endpoint advancement, FIR processing and fault-prefix behavior are unchanged.

The [frozen contract](../specs/BATCHED_QUADRATURE_20261124.md) requires exact output compatibility and retained performance observations. No fast-math, platform-specific SIMD, quiet-key skipping, reference-budget widening or physical calibration was added. Compiler diagnostics on this GCC build did **not** report vectorized loops in `observe_cell`; contiguous storage alone is not a claim of SIMD execution.

## Audio and behavior

The [complete compatibility record](batched-quadrature-compatibility-20261124.json) contains all 2,097 case identities and actual stdout, stderr and production float64 hashes. Every hash and metadata record matches the earlier independently studied Q6 outputs: **2,534,400 frames, 2,097 exact matches**. All 24 historical direct/filtered WAV fixtures also match their full-file hashes: **44,160 frames, 24 exact matches**. Inputs, sources, baselines and executable hashes stayed unchanged during this replay.

This establishes that the retained independent Q6/Q64 comparison results still apply to these identical outputs. It does not claim that SciPy reference integration was recomputed or that a physical instrument was measured. Raw replay streams and WAVs remain at the reported local output directory; the committed report retains their complete hashes and the tool can reproduce them.

```sh
python3 tools/verify_audio_compatibility.py \
  --probe build/vst3/rhodes_quadrature_probe \
  --renderer build/vst3/rhodes_render \
  --output build/NEW-compatibility-directory
```

The final loop passed all **285 existing integrated CTests** locally, including analytic fractional observations, exact scalar/batch comparisons, plan rejection/nonmutation, callback failures, partition/event/state behavior, guarded allocations and all three SDK validators. Three additional CTests now run `moduleinfotool -validate -path` against each actual bundle; all three pass locally. Native CI results for the published revision must be checked separately.

## Full callback observations

Each capture uses the same frozen 72-process protocol: 12 profiles, three variants, two order-balanced rounds and 40,128 measured callbacks. Every run completed, all 36 within-capture repeat pairs matched, and compiled inputs/binaries stayed unchanged. The archives retain all 298 original files per capture, including every duration and outlier, child affinity, machine/load observations, build commands and source diffs.

| Capture | Direct misses | Filtered misses | Q6 misses | Largest Q6 callback |
| --- | ---: | ---: | ---: | ---: |
| [Original scalar traversal](vst3-matrix-20261124.json) | 33 | 62 | 2,947 | 91,441.456 µs |
| [First batched traversal](vst3-matrix-batched-20261124.json) | 50 | 149 | 3,852 | 96,939.447 µs |
| [Final independent-node loops](vst3-matrix-batch-final-20261124.json) | 18 | 21 | 1,716 | 99,882.967 µs |

These totals mix different block sizes and workloads; they are inventories of observed overruns, not comparable population miss rates. The first batch showed mixed timings and is preserved alongside the later refinement. Shared-host scheduling, frequency and unrelated processes were uncontrolled. The final capture may overlap a 75 ms parser review at startup; its exact overlap is unknown and explicitly retained.

For the final Q6 implementation, the 73-note 48 kHz/64-frame profile had medians **586.039 and 291.913 µs**, with no observed nominal-period misses in either round. At 192 kHz/64 frames, medians were **399.382 and 297.291 µs**, with **284 and 86** misses against a 333.333 µs period. At 192 kHz/one frame, medians were **4.877 and 4.857 µs**, with **23 and 43** misses against a 5.208 µs period. This variation prevents a universal latency or deadline claim.

The [supplemental CPU experiment](q6-cpu-paired-20261124.json) alternated the original and final binaries in a predeclared before/after/after/before order, repeated four times. Each process used 4,096 measured 64-frame callbacks, 192 kHz, 73 held keys and sample format 64 on CPU 0. Whole-child user CPU means were **1.532606 s before** and **1.422067 s after**, about 7.2% lower in this observation; medians were **1.486539 s** and **1.391038 s**. Every native integrity record matched. This timing includes setup, warmup and JSON output, and excludes descheduled CPU time. It still does not isolate cache/frequency effects or establish a causal speedup across machines. All 16 runs and raw streams are retained.

## Provenance and limits

The captures ran on a shared Linux x86_64 container using GCC 14.2 and the pinned official SDK. The final source was uncommitted at capture time; the archive binds the exact production diff, compiled-input hashes and binary identities. The supplemental CPU binaries match the original/final matrix hashes. Later module metadata test registration does not alter the DSP implementation. Archives were byte-compared against every original member after creation.

Passing this numerical compatibility check does not establish sample accuracy against a measured Rhodes. The finite-contact solver remains offline, and the existing direct/filtered reference findings remain visible. These observations do not complete RT-001 or the general contact-extrema gate.
