# Expression, state and output-layout verification

This guide covers HOST-003 on the three synthetic VST3 instruments. The
[frozen contract](specs/HOST_EXPRESSION_MONO_20261124.md) defines global CC11
Expression, independent gain/Expression interpolation, adaptive RHST state and
mono/stereo output. [Runtime and assets](specs/RUNTIME_AND_ASSETS.md#persistence-and-determinism)
contains the exact wire-format table and stream boundary; [VST3 setup](VST3.md)
describes user controls and host behavior. The contract's interpolation anchor
is the audio owner's canonical **live** value after any selected recall, at
offset `-1`; an already newer mailbox snapshot is not that anchor.

Commands below are reproduction instructions. Registered fixtures, local
observations and final native acceptance are separate evidence. Use the
[live project state](PROJECT_STATE.md) for task status and the
[dated observations below](#retained-observations--24-november-2026) for what
actually ran. HOST-002's [older verification record](HOST_MIDI_VERIFICATION.md)
retains its own source revisions and 2→18-control comparison.

## Build and select the checks

Start at the repository root with C++20, CMake 3.25+, Python 3.11+, and the
[pinned SDK and four submodules](DEPENDENCIES.md#exact-submodule-revisions).
The complete test build needs `std::barrier` and `std::jthread`; see
[development prerequisites](DEVELOPMENT.md). On Linux/macOS, with the SDK at
`../vst3sdk`, use a fresh build directory:

```sh
cmake -S . -B build/host-expression -G "Unix Makefiles" \
  -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON -DRHODES_BUILD_VST3=ON \
  -DRHODES_VST3_SDK_ROOT="${PWD}/../vst3sdk"
cmake --build build/host-expression --config Release --parallel 2
ctest --test-dir build/host-expression -C Release --show-only=json-v1 \
  -R '^(expression_state\.|vst3\.((filtered_|quadrature_)?(expression_|mono_)|loaded_expression_))'
ctest --test-dir build/host-expression -C Release --output-on-failure --no-tests=error \
  -R '^(expression_state\.|vst3\.((filtered_|quadrature_)?(expression_|mono_)|loaded_expression_))'
```

The first CTest command inventories the exact selection without executing it.
The second executes it and rejects an empty selection. For a focused rebuild,
`--target rhodes_vst3_checks rhodes_expression_state_tests` builds all host
checks plus the independent core state executable; the host aggregate alone
does not build that core target. The full build above also prepares existing
core and host regressions for a complete run:

```sh
ctest --test-dir build/host-expression -C Release --output-on-failure --no-tests=error
```

Use [the Windows generator recipe](VST3.md#build-the-optional-adapter) on Windows,
substitute `python` as appropriate, and put wrapped commands on one PowerShell
line. Multi-configuration executables normally reside in `Release/`. Preserve
the same native generator/compiler/flags for baseline and candidate. The tested
macOS recipe uses a non-Xcode generator; changing floating-point flags needs
new evidence, as explained in [development](DEVELOPMENT.md#floating-point-build-configuration).

## Registered selectors and targets

[Core registration](../CMakeLists.txt) and [VST3 registration](../cmake/Vst3.cmake)
are authoritative for the current build. In this table, `<s>` is one literal
selector in the adjacent column. `<p>` expands to the empty string for direct,
`filtered_` for filtered, or `quadrature_` for quadrature; these are table
placeholders, not command-line arguments. Thus
`vst3.filtered_expression_audio_nondyadic_curve` invokes
`rhodes_vst3_filtered_expression_audio_tests nondyadic_curve`.

| CTest family | Executable target | Every registered selector |
| --- | --- | --- |
| `expression_state.<s>` | `rhodes_expression_state_tests` | `defaults_and_goldens`, `codec_domains`, `malformed_packets`, `model_isolation`, `mailbox_words`, `mailbox_transactions`, `floating_point_modes` |
| `vst3.expression_stream_<s>` | `rhodes_expression_stream_tests` | `roundtrip`, `malformed`, `transfers`, `overwrite` |
| `vst3.<p>expression_audio_<s>` | `rhodes_vst3_<p>expression_audio_tests` | `default_queues`, `anchor_duplicates`, `combined_curves`, `nondyadic_curve`, `muted_resume`, `flush_recall`, `partition_curves` |
| `vst3.<p>mono_<s>` | `rhodes_vst3_<p>mono_tests` | `negotiation`, `scalar`, `pointers`, `omission`, `flush`, `rejection`, `lifecycle` |
| `vst3.<p>expression_control_<s>` | `rhodes_vst3_<p>expression_control_tests` | `metadata`, `conversion`, `recall`, `lifecycle`, `fp_rounding`, `fp_flush` |
| `vst3.<p>expression_transaction_<s>` | `rhodes_vst3_<p>expression_transaction_tests` | `queue_shapes`, `expression_points`, `late_co_batches`, `ownership_recall`, `flush_recall`, `inactive_transactions` |
| `vst3.<p>expression_bounds_<s>` | `rhodes_vst3_<p>expression_bounds_tests` | `queue_limits`, `persistent_limits`, `maximum_points`, `command_events`, `dispatch`, `allocations` |
| `vst3.<p>expression_concurrency_<s>` | `rhodes_vst3_<p>expression_concurrency_tests` | `mailbox_ordered`, `host_preflight`, `adaptive_snapshots`, `live_anchors`, `callback_stress` |
| `vst3.loaded_expression_<s>` | `rhodes_vst3_loaded_expression_tests` | `controls_state`, `audio_layout`, `coexist` |

The common state and stream fixtures exercise models 1/2/3 internally. Most
per-variant host targets compile the controller/processor implementation into
their test executable; controller-only targets omit the processor. The
[loaded-expression target](../tests/cpp/vst3_loaded_expression_test.cpp) instead
links only SDK hosting/loader support and reaches all three actual bundles
through their factories. CTest supplies its three bundle paths. Its coexistence
case covers all six module creation orders at both tested rates and sample
formats. It does not prove OS image unmapping; the
[loaded lifecycle boundary](HOST_MIDI_VERIFICATION.md#loaded-module-lifecycle-boundary)
still applies.

| Fixture source | Principal evidence boundary |
| --- | --- |
| [State codec/mailbox](../tests/cpp/expression_state_test.cpp) | Literal legacy/v2 bytes, all model directions, malformed packets, signed zero/subnormal domains, whole-word publication and floating-point environments |
| [State stream](../tests/cpp/vst3_expression_stream_test.cpp) | Adaptive 16/20-byte writes, strict remaining lengths, partial/error transfers and the absence of output-stream truncation |
| [Audio](../tests/cpp/vst3_expression_audio_test.cpp) | Independent scalar scaling/interpolation, non-dyadic curves, zero-expression continuation, recall/flush order and equivalent partition curves in both host sample formats |
| [Controller](../tests/cpp/vst3_expression_control_test.cpp) | Nineteen parameters, global CC11 mapping, strict text, own-model recall, lifecycle and rounding/flush modes |
| [Transactions](../tests/cpp/vst3_expression_transaction_test.cpp) | Late malformed queues/events, ownership rejection, pending-recall preservation, zero-frame order and inactive-output transactions |
| [Bounds](../tests/cpp/vst3_expression_bounds_test.cpp) | Nineteen queues, three 8192-point budgets, command/event limits, physical dispatch bounds and calibrated allocation observations |
| [Concurrency](../tests/cpp/vst3_expression_concurrency_test.cpp) | Whole-configuration publication, newer recall precedence, adaptive snapshots, live interpolation anchors and concurrent callback/state stress |
| [Mono](../tests/cpp/vst3_mono_contract_test.cpp) | Negotiation, scalar identity, one/two-pointer bounds, omitted inactive output, zero-frame forms, bounded cleanup and lifecycle |
| [Loaded modules](../tests/cpp/vst3_loaded_expression_test.cpp) | Actual factory category/controls/state, audio/layout behavior, protected mono pointer bounds and model coexistence |

Keep the existing regressions and all six external validators in the acceptance
selection. This focused validator command is useful after metadata/layout work:

```sh
ctest --test-dir build/host-expression -C Release --output-on-failure --no-tests=error \
  -R '^vst3\.((filtered_|quadrature_)?(sdk_validator|moduleinfo_validator)|loaded_expression_.*)$'
```

The validator names are `vst3.sdk_validator`, `vst3.filtered_sdk_validator`,
`vst3.quadrature_sdk_validator`, `vst3.moduleinfo_validator`,
`vst3.filtered_moduleinfo_validator` and `vst3.quadrature_moduleinfo_validator`.
Their executable targets are `validator` and `moduleinfotool`. The aggregate
`rhodes_vst3_checks` also builds `rhodes_vst3`, `rhodes_vst3_filtered`,
`rhodes_vst3_quadrature` and their required resources. A generated metadata file
alone does not show that its actual module loaded and passed validation.

## Legacy unity comparison

The frozen baseline is **`6c91dab00855b67a559e92edb79ae49e567be469`**, from before
HOST-003. It exposes eighteen controls; the current instruments expose nineteen.
Build an exact separate checkout of that revision with the same native compiler,
generator, flags, architecture and pinned SDK as the candidate. Do not relabel
a candidate bundle or a different source revision as this baseline.

For the examples below, place that clean checkout at `../legacy-source`. Verify
its revision and tracked cleanliness before building. Run from the candidate
repository root, and choose fresh diagnostics/build directories:

```sh
git -C ../legacy-source rev-parse HEAD
git -C ../legacy-source diff --exit-code HEAD --
cmake -S ../legacy-source -B build/legacy-expression -G "Unix Makefiles" \
  -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=OFF -DRHODES_BUILD_VST3=ON \
  -DRHODES_VST3_SDK_ROOT="${PWD}/../vst3sdk"
cmake --build build/legacy-expression --config Release --parallel 2 \
  --target rhodes_vst3 rhodes_vst3_filtered rhodes_vst3_quadrature
python3 tools/capture_ci_diagnostics.py baseline-manifest \
  --source-root ../legacy-source --bundle-root build/legacy-expression/VST3/Release \
  --build-dir build/legacy-expression --sdk-root ../vst3sdk \
  --output-dir build/expression-baseline-evidence
cmake -S . -B build/host-expression \
  -DRHODES_VST3_LEGACY_BUNDLE_ROOT="${PWD}/build/legacy-expression/VST3/Release"
python3 tools/capture_ci_diagnostics.py verify-toolchains \
  --baseline-build build/legacy-expression --current-build build/host-expression \
  --output-dir build/expression-baseline-evidence
cmake --build build/host-expression --config Release --parallel 2 \
  --target rhodes_vst3_checks
ctest --test-dir build/host-expression -C Release --output-on-failure --no-tests=error \
  -R '^vst3\.legacy_unity_comparison$'
```

Inspect each exit status and stop the comparison setup on failure. The helper
requires the baseline's exact clean source commit, all SDK pins, a build cache
and one nonempty native module per variant. It retains rejected attempts in
`baseline-manifest.json` under the diagnostics directory and writes the bundle
root's `manifest.json` only after those checks succeed. Both manifests retain
source/build/environment provenance. The toolchain check compares the recorded
compiler, generator, flags, architecture and platform fields; it does not prove
that arbitrary external build steps or untracked headers were identical.

The baseline root directly contains `RhodesResearch.vst3`,
`RhodesResearchFiltered.vst3`, `RhodesResearchQuadrature.vst3` and `manifest.json`.
The comparator requires that manifest's `source_commit` to equal the fixed
revision and `modules` to contain exactly three entries, each with `path` and
`sha256` matching an actual native binary. Paths may be absolute or relative
to the baseline root; additional provenance is retained. Native layouts are
Linux `Contents/*-linux/<name>.so`, Windows `Contents/*-win/<name>.vst3`, and
macOS `Contents/MacOS/<name>`. Use the manifest helper instead of inventing hashes
or treating a commit label as proof of binary provenance.

`RHODES_VST3_LEGACY_BUNDLE_ROOT` defaults to empty. Empty leaves
`vst3.legacy_unity_comparison` **unregistered**, not skipped or passed. A nonempty
root without `manifest.json` fails configuration; malformed manifest contents
or mismatching module bytes fail the comparison. CTest has a 300-second timeout.
The SDK-only recorder target is `rhodes_vst3_legacy_capture`; its Python driver
can also be run directly on Linux/macOS:

```sh
python3 tools/compare_vst3_legacy.py \
  --capture build/host-expression/rhodes_vst3_legacy_capture \
  --baseline-root build/legacy-expression/VST3/Release \
  --current-root build/host-expression/VST3/Release \
  --output-dir build/host-expression/legacy-comparison/Release \
  --sdk-root ../vst3sdk --source-root . --timeout 30
```

On Windows, point `--capture` to the actual `Release/rhodes_vst3_legacy_capture.exe`.
The [driver](../tools/compare_vst3_legacy.py) uses only Python's standard library.
It launches old/new same-identity bundles in separate processes. Its 72 fixtures
span three variants, binary32/binary64, 48/192 kHz, whole/fixed64/ragged blocks
and dense/sparse gain curves. Each fixture has baseline, current default and
current explicit-Expression-1 captures: **216 processes and 144 exact pairs**,
each pair covering 4,224 stereo frames, for **608,256 paired stereo frames**.
Each capture includes notes, pressure, CC123, gain/sustain, recalls and flushes.

Both musical epochs must contain nonzero finite audio; the driver independently
checks raw sample bytes, stereo identity, legacy state bytes, metadata, silence
flags and final recalled silence. The declared differences are capture ID,
lane name, total parameter count and explicit Expression-point count. Dense
gain supplies equivalent per-sample curves and adds **36 exact partition groups**.
Sparse endpoints depend on block size, so only matching partitions are paired.
This is within-platform exact replay, not cross-platform bit identity or
non-unity/mono verification.

Exit 0 means the complete matrix and retained-byte checks passed; a failed,
missing, partial, malformed, timed-out or changed capture cannot pass. Each
invocation creates a unique `run-*` directory and replaces the output root's
`report.json` with an incomplete current-attempt record before validating inputs.
The final report names the retained raw directory, with per-process stdout JSON,
stderr and binary traces, `commands.json`, a hash/size `manifest.json` and another
report copy. Preserve the full directory and failed attempts. A summary report
without its raw files is not a fresh execution or complete raw-evidence archive.

## Retain source-bound diagnostics

The existing [diagnostics CLI](../tools/capture_ci_diagnostics.py) is used by both
[Foundation CI](../.github/workflows/ci.yml) and
[VST3 CI](../.github/workflows/vst3.yml). It requires no Python packages beyond
the standard library. Use it before and after an acceptance run on a clean,
identified source; `--expected-sha` is the actual full commit being tested.
For a new attempt, adapt the paths and literal commit below:

```sh
python3 tools/capture_ci_diagnostics.py metadata --phase before \
  --source-root . --expected-sha FULL_CANDIDATE_COMMIT \
  --baseline-source ../legacy-source --sdk-root ../vst3sdk \
  --output-dir build/expression-evidence-01
python3 tools/capture_ci_diagnostics.py run --label build \
  --output-dir build/expression-evidence-01 -- \
  cmake --build build/host-expression --config Release --parallel 2
python3 tools/capture_ci_diagnostics.py ctest --label ctest \
  --output-dir build/expression-evidence-01 -- \
  ctest --test-dir build/host-expression -C Release --output-on-failure --no-tests=error
python3 tools/capture_ci_diagnostics.py metadata --phase after \
  --source-root . --expected-sha FULL_CANDIDATE_COMMIT \
  --baseline-source ../legacy-source --sdk-root ../vst3sdk \
  --build-dir build/legacy-expression --build-dir build/host-expression \
  --bundle-root build/legacy-expression/VST3/Release \
  --bundle-root build/host-expression/VST3/Release \
  --output-dir build/expression-evidence-01
```

Run final metadata even after a failed command, retaining the command's failure.
Before metadata may precede configuration; wrap configuration with `run` too
when capturing a fresh build. If baseline comparison is not part of the attempt,
omit `--baseline-source` and the baseline build/bundle arguments and state that
coverage gap. Do not substitute a nonexistent expected commit or edit away a
dirty-source error. Exploratory dirty-tree results need explicit source-file
hashes and their limited provenance, as in the first retained Linux observation.

| Subcommand | Retained evidence and interpretation |
| --- | --- |
| `metadata` | `metadata-before.json` / `metadata-after.json`: source commit/tree/tracked-file hashes and cleanliness, SDK pins, platform/tool versions, selected CI identity, optional build settings and module hashes. The after phase checks unchanged source against the before phase. Invalid metadata returns failure. |
| `run` | `<label>.command.json` plus raw merged stdout/stderr `<label>.log`; preserves the child return code, logs and interrupted/could-not-start outcome. |
| `ctest` | Same-selection `--show-only=json-v1` inventory, command records, raw logs, output log and JUnit. Inventory alone never represents execution. |
| `baseline-manifest` | Validates fixed baseline provenance and writes its actual module manifest; rejected attempts remain diagnostic evidence. |
| `verify-toolchains` | `toolchain-comparison.json`; fails missing or unequal recorded baseline/current toolchain fields. |

Labels use lowercase letters/digits, hyphens or underscores and must start with
a letter/digit. Existing labels and metadata files are refused rather than
overwritten. Use a fresh output directory for a rerun and retain the first attempt.
The workflows upload diagnostics on failure as well as success, with OS,
architecture, source SHA, run ID and attempt in the artifact name. Diagnostics
are retained for 90 days and successful experimental bundle archives for seven
days in the current configuration; an expired artifact remains missing evidence.

### Interpret native results and skips

Read each platform's exact source/build metadata, CTest selection, raw output,
JUnit and legacy report together. Record registered, passed, failed and skipped
counts separately for the core suite, host suite, Python checks and sanitizers.
A green CI job or “100% tests passed” line can coexist with skipped conditions;
an optional comparison absent from inventory is untested, not a skip. A job
cancelled, timed out or never started has no completed result for its missing
steps. Linux execution does not supply Windows or macOS coverage, and an older
native checkpoint does not cover later source changes.

`expression_control_fp_rounding` and `expression_control_fp_flush` (including
both variant prefixes) use CTest skip return code **77** when no applicable
environment runs. Individual unavailable environments can also be printed as
`SKIP` while other environments in that selector pass. The core
`expression_state.floating_point_modes` reports its own executed/skipped mode
counts and may pass with some modes skipped. Retain those internal counts as
well as CTest's totals. x86 FTZ/DAZ coverage must not be inferred on another
architecture. Existing native filesystem and optional numerical-dependency
skips in the broader suites likewise retain their reasons.

Source fingerprints describe observed files; module and executable hashes
identify executed bytes. Neither alone attests that a compiler produced those
bytes. Clean before/after source snapshots, recorded configure/build commands,
matched toolchains and actual loaded-module results provide the required chain
for review. Exact binary reproducibility, named-DAW operation, OS deployment
minimums, device deadlines and physical fidelity require their own evidence.

## Retained observations — 24 November 2026

These are distinct local Linux/GCC Release observations, each bound to its
recorded source and binaries. They are **not one enlarged full-suite run** and
do not constitute final three-platform HOST-003 acceptance.

| Retained observation | Actual outcome and limit |
| --- | --- |
| [Initial integration](validation/host-expression-initial-linux-20261124.json), [raw log](validation/host-expression-initial-linux-20261124.log), [JUnit](validation/host-expression-initial-linux-20261124.xml) | 359/359 CTests passed, zero failures/skips, including 32 new state/stream/audio cases. The seven audio selectors include `nondyadic_curve` in all variants. Incremental Linux build; source hashes identify the tested working tree. |
| [Additional mono, loaded and concurrency fixtures](validation/host-expression-additional-host-fixtures-20261124.json) | Separate 24-case mono/loaded and 15-case concurrency selections passed, zero failures/skips: 39 unique additional cases. Retains the initial build attempt before reconfiguration as well. |
| [Later control, transaction and bounds integration](validation/host-expression-input-contracts-20261124.json), with [control](agents/reports/continuation-d/w2-030.md), [transaction](agents/reports/continuation-d/w2-031.md) and [bounds](agents/reports/continuation-d/w2-032.md) worker reports | Each eighteen-case family passed, zero failures/skips. These 54 and the preceding 39 cover 93 unique additional focused cases. The worker reports distinguish their standalone checks from the integrated CTest results. These counts are not added to 359 as a claimed single CTest invocation. |
| [Factory category correction](validation/host-expression-category-correction-20261124.json) | The added loaded-factory assertion first failed. After changing the category to `Instrument\|Synth`, three loaded-expression selectors plus six SDK/module validators passed: nine targeted checks, no failures/skips. This overlaps already registered checks. |
| [Legacy comparison](validation/host-expression-legacy-linux-20261124.json), [raw manifest](validation/host-expression-legacy-linux-manifest-20261124.json), [worker review](agents/reports/continuation-d/w2-033.md) | One CTest executed 216 complete captures: 144/144 exact pairs, 608,256 paired stereo frames and 36/36 dense partition groups. Recorded module/recorder/manifest/source bytes remained unchanged; the raw manifest matched the compared files. |

The original [native platform checkpoint](validation/native-platforms-20261124.md)
and later HOST-002 results retain their historical scope. Consult subsequent
source-bound reports and criterion reviews before making a current native
acceptance claim; merely configuring the new native workflows supplies no pass.

## Remaining fault, host and acceptance scope

Ordinary zero-expression output and continued tails do not inject an engine
failure, nonfinite monitored value or final host-format overflow. The frozen
contract requires these faults to remain visible even at zero Expression and
with an inactive output omitted; attenuation of a finite double into float32
range must remain valid. Acceptance needs explicit fault-path evidence for
those distinctions, including reset fencing/recovery and transactional
preflight. Existing core fault fixtures and malformed-input tests each retain
their own boundary; they do not become all three production adapters' complete
fault-injection result by association.

Allocation counters observe the intercepted calls in their executed workloads.
Concurrency stress and source inspection do not prove all host/library code
allocation-free, establish a lock-free external host, or provide worst-case CPU
or device deadlines. Preserve the exact allocation calibration, stress schedule,
toolchain and sanitizer coverage in the associated report. The independent
audio fixtures share the synthetic source engine where stated and do not
measure a specimen or repeat every offline numerical-reference study.

The [manual DAW protocol](DAW_VALIDATION.md) requires a named DAW/version, actual
bundle hashes, OS/architecture, routing, device/driver, sample rate and callback
conditions. Evaluate discovery of nineteen controls and both offered layouts;
global CC11 delivery on multiple channels; constant and automated Expression;
continued release tails through zero Expression; pre-pan scalar mono/stereo
equality; own-model v1/v2 recall; strict text; and the existing sustain/CC123/
pressure cases. Preserve delivered automation values and offsets where observable.
Raw CC11 value 64 is not an exact-half-scale fixture, and a host's graphical
curve alone does not prove the delivered interpolation points.

Track mute/bypass or absent sound does not prove positive-frame processing with
an omitted inactive output. If a DAW cannot expose that condition or if callback
continuation is unknown, mark it **inconclusive** or **not run**. Malformed pointers,
active-output omission rejection, cleanup masks, mailbox races and arithmetic
fault injection belong to controlled harnesses. Record each actual DAW condition
as observed pass, observed failure, inconclusive or not run; no named-DAW pass
is supplied by this guide.

Final review follows the [frozen acceptance criteria](specs/HOST_EXPRESSION_MONO_20261124.md#acceptance-review)
for all three models and actual Linux, Windows and macOS sources. Retain the
full affected core/host suites, all six validators, exact unity replay,
platform/mode skips and unresolved limits. Register new immutable results and
corrections; preserve earlier attempts, historical contracts and evidence.
HOST-002, PHY-002, RT-001, measured fidelity and release qualification keep
their separately defined acceptance decisions.
