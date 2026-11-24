# HOST-004 final acceptance review — 24 November 2026

## Decision and frozen scope

**Pass for the recorded Linux/X11 native editor and synthetic processing scope.**
All three acceptance clauses below have relevant observed evidence. Register this
review and the numerical record before completing HOST-004. This decision does
not complete HOST-003, PHY-002 or RT-001, promote physical fidelity, or publish a
plug-in release. The specification requires each observed platform to be reported
separately; it does not require a three-platform native-window or named-DAW matrix.

Evaluator: `photo-editor-root`, with independent bounded reviews from workers
83–94. Review contract: [ACCEPTANCE_REVIEW.md](../agents/ACCEPTANCE_REVIEW.md).
Task title: **Build the owner-reference native photo editor and functional panel controls**.
Dependency: **HOST-001**, completed in the evaluated canonical state.
Required evidence kinds: **documentary**, **numerical**.
Target: **One documented 73-key Mark I Stage specimen at its specified loaded electrical output; identity and measurements pending.**

Definition SHA-256: `79a999d6b9d4a22e987f305cea4bb7b52f7ce21d5f778be9e362fb48043d2c1b`.
The exact serialized definition projection is retained in the
[numerical aggregate](photo-editor-linux-final-20261124.json). Its acceptance
clauses are reproduced without amendment in the table below.

## Source and observed modules

The evaluated production/fixture commit is
[`8c5d819e9d99767372cf1a74a971433edce6da35`](https://github.com/baabi-boo/Fender-rhoades/commit/8c5d819e9d99767372cf1a74a971433edce6da35).
The [final applicability bridge](photo-continuation-20261124/final-source-bridge.json)
compares committed blobs, the original run manifests, current files and actual
module bytes. Production code and reference assets remain those of
`4369547403735c957689b547f478540e3277a79c`. Subsequent relevant CMake changes only
register the new loaded-panel fixture, dependencies, CTest and macOS target setup.
The later MSVC-only geometry-test budget experiment changes `CMakeLists.txt`
without changing production source, assets, test assertions or runtime logic.
Its configuration and eventual Windows outcome require their own additive
record; the historical `8c5d819` bridge is preserved unchanged.

The strengthened audio execution began with Git HEAD `4f658c2` and later uncommitted
CMake/fixture edits. Its explicit file hashes match the subsequently committed
`8c5d819` files. This is file-bound applicability, not a claim that the process
started from a clean `8c5d819` checkout. All 44 captured source inputs and all
three module binaries remained unchanged during that configure/build/test run.

| Final Linux module | SHA-256 |
| --- | --- |
| Direct | `5facf72c9367a5849c124833673d68e2d74b8b64c88b22e95d66663fa65d1959` |
| Filtered | `aa698bc10af7df69bb7c8d1f5f16cbb3ff1472fa6794abc17316fc22e367f2cd` |
| Quadrature | `d2213f5a635a2f819f8932c8cd18575ddd3c07bc3fdc386f99115ed72cd4078e` |

## Criterion mapping

| Criterion and exact acceptance text | Relevant observed evidence and decision | Remaining limits |
| --- | --- | --- |
| **HOST-004/C01:** Preserve the supplied original image and implement a native editor using its full composition and aligned controls. | **Pass.** The unchanged 1536 × 1152 source and all three packaged PNGs preserve decoded RGB and ICC bytes. The final Direct native replay has exact initial and final full-size idle equality across all 1,769,472 pixels. Actual screenshots show the cabinet, legs, background and pedal. Shared transforms align the seven pictured controls and 73-key perspective geometry; the replay exercises controls and all white-key highlights. Independent [C01 review](../agents/reports/photo-continuation-20261124/w5-083.md). | Whole-image native equality was observed on Linux Direct at the full-size, unit-scale view. Other sizes retain the full composition; this does not assert unscaled pixel equality at resampled sizes or unobserved OS/DPI settings. |
| **HOST-004/C02:** Connect panel controls to bounded synthetic audio processing and coherent state recall while preserving default legacy output. | **Pass.** The component panel/effect matrix, loaded state/control fixture, actual three-bundle nonneutral audio observation, and fresh default-panel legacy comparison establish distinct complementary contracts. The strengthened fixture discriminates Bass/Treble, stereo modulation and every disable-ramp sample. The pending integration gap recorded in [worker 84's review](../agents/reports/photo-continuation-20261124/w5-084.md) is closed by the passing source-bound run and [independent result review](../agents/reports/photo-continuation-20261124/w5-090.md). | Synthetic digital effects and bounded fixture workloads; no measured Peterson/Janus circuit or acoustic authenticity. The legacy matrix alone covers 16-byte states and default panel/Expression unity. Version-2/version-3 recall and nonneutral controls have separate loaded/component evidence. Host parameter delivery remains required. |
| **HOST-004/C03:** Verify native editor lifecycle, interaction, resizing and packaged resources; retain actual screenshots and platform-specific results. | **Pass.** Final Direct XTEST gives 136/136 passes and 66 screenshots plus a mask; all three variants give 39/39 native lifecycle cases. Three SDK validators pass 47/47 tests each, three metadata validators pass, and all three bundle resource sets retain the exact reference. Independent [C03 review](../agents/reports/photo-continuation-20261124/w5-085.md). | Native Windows/macOS editor interaction, non-unit DPI and named DAWs remain unobserved. The native lifecycle fixture explicitly drains registered descriptors and does not establish every readiness-only host loop. Earlier platform CI records retain their own source and feature scope. |

## Numerical observations and their distinct executions

The [aggregate numerical record](photo-editor-linux-final-20261124.json) indexes
raw commands, inputs, outputs and hashes. It combines existing observations; it is
not a new larger test-suite execution.

- **Final native replay:** 136/136 pass, verifier and accepting SDK host exit zero.
  Each of 43 held white keys is compared with 30 black-key interiors: 1,290 regions,
  818,978 pixel instances, zero changes. Minimum/default/maximum sizes are
  768 × 576, 1024 × 768 and 1536 × 1152. Both full-size idle captures are exact.
  See [raw outcome](photo-continuation-20261124/final-native/outcome.json).
- **Native lifecycle:** Direct 13/13, followed by Filtered 13/13 and Quadrature
  13/13. Cases include removal within accepted begin/perform/end callbacks,
  nested dispatch, timer refusal and retry, parent destruction, host-reference
  release, focus removal, ten same-view reopen cycles and sibling-window
  survival. Registration retirement and final host reference count are checked.
  See [final module gates](photo-continuation-20261124/final-module-gates/summary.json)
  and [Direct observation](photo-continuation-20261124/lifecycle-after/final-13.log).
- **Panel math and state:** 26 component commands pass 16,042,445 checks, including
  response/reference, smoothing, faults, monitored allocation, parameter text,
  stream versions and coherent recall. The separate final factory-loaded
  control/state run passes 65,644 checks. Neither result is substituted for the
  actual waveform integration observation below.
- **Loaded nonneutral audio:** 96 fresh renders, 36,000 callbacks, 24,173,604 scalar
  comparisons and 12,698,530 checks across three models and float32/float64 stereo
  at 48 kHz. Separate spectral probes, opposed-channel sums and separation,
  meaningful nonzero output, finite samples and the conservative 32B peak bound
  pass. At one second, the switch-off point is offset 128 inside a 256-frame
  callback; all 480 envelope samples and exact neutral output at frame 48479
  pass. See the [complete observation](photo-continuation-20261124/final-loaded-audio/README.md).
- **Neutral compatibility:** 144 fresh current captures are paired with 72 retained
  baseline captures; all 608,256 paired stereo frames and legacy checkpoints are
  exact. This covers three variants, float32/float64, 48/192 kHz and the recorded
  partition/Expression/gain scenarios, with 5,328 fresh process callbacks.
  Baselines were revalidated without re-execution. The explicit unity writes
  target Expression only; panel IDs 3–7 are not written by this matrix. All its
  state packets are 16 bytes. See [fresh comparison](photo-continuation-20261124/final-legacy/fresh-comparison.json)
  and the [independent scope audit](../agents/reports/photo-continuation-20261124/w5-087.md).

## Preserved failures, review corrections and limits

The original callback-removal crash, intermediate unsuccessful host probes,
Cairo/reopen failure and sibling-child leak observations remain retained with their
source and outcome. The intermediate 41-pass/one-missing-executable log is not a
final module result. The first loaded-panel fixture passed weaker assertions;
worker 80 found a setup-order issue and coverage holes. Those source and raw logs
remain in [before-review](photo-continuation-20261124/final-loaded-audio/before-review/README.md),
separate from the strengthened final pass. No failed observation was relabeled.

The [final capture archive](photo-continuation-20261124/captures/final-native-pixels.zip)
preserves exact decoded RGB and dimensions, plus byte-exact raw records. It does
not reproduce original PNG encoded bytes, ancillary metadata or image modes.
Worker 86 found delivery-path collisions and missing audit companions; worker 89
corrected the [repository delivery index](photo-continuation-20261124/captures/delivery-manifest.json),
retained the historical producer manifest, and verified all 16 mapped entries.
All earlier archives and roundtrip observations remain unchanged.

The accepting SDK editor host has no audio-processing loop; UI interaction and
loaded audio are observed in separate appropriate fixtures. Host changes must
be delivered to the processor, including a zero-frame flush where required;
withheld delivery is deliberately not treated as successful recall. The observed
bounds and allocation checks do not establish audio-device deadline performance.
No physical specimen identity, authenticated measurements, calibrated fidelity,
all-platform native UI qualification or release qualification is claimed.

The later [8c5d819 CI snapshot](photo-continuation-20261124/ci-8c5d819.json)
records a Windows Foundation C1001 compiler failure before its test step, with
Linux Foundation checks passed, VST3 jobs still building and macOS jobs queued.
The [bounded compiler-budget experiment](photo-continuation-20261124/msvc-budget-experiment.json)
changes only the MSVC geometry-test target; its initial record makes no Windows
success claim. This platform result remains distinct from the Linux native and
audio observations evaluated above.
