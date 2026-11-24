# Experimental VST3 instrument

**Rhodes Research Prototype 0.1.0**, **Rhodes Research Filtered 0.2.0** and
**Rhodes Research Quadrature 0.3.0** wrap the sample-free synthetic
`ResearchEngine` in separate VST3 instruments. All three provide ordinary host
parameter controls; there is no custom editor. Their sound uses impulse excitation,
switched-loss release, a polynomial pickup and a synthetic resistive network.
The separate finite-mass contact reference is offline and does not run inside
any of these plug-ins. No specimen calibration or measured fidelity is established.

| Variant | Bundle / build target | Audio and saved-state contract |
|---|---|---|
| Direct research model | `RhodesResearch.vst3` / `rhodes_vst3` | Original class IDs, model-1 state, zero reported latency |
| Filtered research model | `RhodesResearchFiltered.vst3` / `rhodes_vst3_filtered` | Separate class IDs, model-2 state, 48-frame FIR delay |
| Quadrature research model | `RhodesResearchQuadrature.vst3` / `rhodes_vst3_quadrature` | Separate class IDs, model-3 state, six fractional observations per host cell, 48-frame delay |

Each variant has its own processor/controller FUID pair, defined in
[plugin_ids.hpp](../plugins/vst3/plugin_ids.hpp). The original model-1 and model-2
identities and saved-state interpretation remain unchanged. A project can load
the three instruments together; there is no live sound-path or latency switch.

The filtered variant uses 4x internal rate through 48 kHz, 2x above 48 through
96 kHz, and 1x above 96 kHz. A 385/193/97-tap Kaiser FIR processes the combined
loaded harp after nonlinear pickup conversion. Mechanics and filter history
persist across note releases/retriggers. Coefficients, timing and numerical
limits are fixed in the [filtered audio contract](specs/FILTERED_AUDIO_20261123.md).
The current measured filter response is not a guarantee for every transient or
internal-rate alias, and it does not establish audibility or Rhodes fidelity.

The quadrature variant observes the same mechanical trajectory at six
nonuniform Gauss–Legendre points within each host-frame interval, then advances
the authoritative mechanics once at host rate. Six fixed 96-tap branches combine
completed cells with a 48-frame group delay. Ordinary release, retrigger and
pedal events preserve mechanics and convolution history; a hard reset clears
both. The first output after reset is zero because no cell has yet completed;
the response can become nonzero on the next frame. The
[quadrature contract](specs/QUADRATURE_AUDIO_20261123.md) fixes the timing,
coefficients, state identity and verification limits.

The [retained full study](validation/quadrature-audio-full-20261123.json) covers
2,097 cases and 2,534,400 loaded-harp voltage frames at nine exact rates. Every
independent Q6 and resolved continuous-Q64 comparison passed; the largest
continuous-reference error was `8.318901123516298e-12 V`, against the unchanged
`1e-4 V` peak limit. This establishes numerical behavior for the declared
synthetic matrix. The [legacy compatibility record](validation/quadrature-legacy-compatibility-20261123.json)
also retains 24 byte-identical direct/filtered WAV reproductions.

The implementation contract is [HOST_PROTOTYPE_20261123](specs/HOST_PROTOTYPE_20261123.md).
The [dependency inventory](DEPENDENCIES.md) records the official SDK revision,
submodules and notices. [NUMERICS.md](NUMERICS.md) describes the underlying core.

## Build the optional adapter

Requirements are Git, a C++20 compiler with lock-free 64-bit atomics, and
**CMake 3.25 or later** for the optional SDK. Use the platform's normal C++ build
environment: GCC/Clang on Linux, Apple's developer toolchain on macOS, or an
x64 Visual Studio C++ environment on Windows. The default research build leaves
`RHODES_BUILD_VST3=OFF` and does not download the SDK.

The complete retained test build also requires a C++20 standard library with
`std::barrier` and `std::jthread`/`std::stop_token`. With Apple's toolchain, use
Xcode 26 or later: Apple's [Xcode 26 release notes](https://developer.apple.com/documentation/Xcode-Release-Notes/xcode-26-release-notes)
record that `jthread` and stop-token support no longer require the experimental
library flag. Selecting the C++20 language mode alone is insufficient with an
older standard library. This requirement is distinct from a successful native
macOS build or an established binary deployment baseline.

From the repository root, obtain a separate SDK checkout. These Git commands
work in a POSIX shell or PowerShell; the destination must not already exist:

```sh
git clone --no-checkout https://github.com/steinbergmedia/vst3sdk.git ../vst3sdk
git -C ../vst3sdk checkout --detach 3cdf9ca5d1f5b1b21e0a86832aa4abe55607bd96
git -C ../vst3sdk submodule update --init -- base cmake pluginterfaces public.sdk
git -C ../vst3sdk rev-parse HEAD
git -C ../vst3sdk submodule status base cmake pluginterfaces public.sdk
```

The root commit is the inspected `v3.8.1_build_84` revision. Configuration verifies
that root commit and all four submodule revisions against
[DEPENDENCIES.md](DEPENDENCIES.md), and rejects missing checkouts or changed
tracked SDK contents. This keeps the compiled dependency tied to the inspected
source. Untracked build directories and unused optional submodules are allowed.
Use a separate clean SDK checkout when experimenting with SDK patches; the build
never resets or modifies your files. Updating the pins is a separate compatibility
decision.
VSTGUI, SDK plug-in examples and graphical/audio hosting examples are disabled.

### Linux or macOS

Run from the repository root with the SDK in the sibling directory above:

```sh
cmake -S . -B build/vst3 -G "Unix Makefiles" -DCMAKE_BUILD_TYPE=Release \
  -DRHODES_BUILD_VST3=ON -DBUILD_TESTING=ON \
  -DRHODES_VST3_SDK_ROOT="${PWD}/../vst3sdk"
cmake --build build/vst3 --config Release \
  --target rhodes_vst3_checks --parallel 2
ctest --test-dir build/vst3 -C Release -R '^vst3\.' \
  --output-on-failure --no-tests=error
```

`rhodes_vst3_checks` exists when `BUILD_TESTING=ON` and builds all three bundles,
all three callback benchmarks, the SDK validator, `moduleinfotool` and every
retained host-test executable. The test command above runs both validators
against each actual bundle and includes the Linux-only triple-module loader
checks where available. The metadata checks compare generated `moduleinfo.json`
with the loadable factory; resource-presence checks remain separate. For a
plug-in-only build, select `--target rhodes_vst3`,
`--target rhodes_vst3_filtered` or `--target rhodes_vst3_quadrature`; tests can be
disabled with `-DBUILD_TESTING=OFF`. To build the three modules without the
test aggregate, pass all three target names after `--target`.

On macOS, add `-DCMAKE_OSX_DEPLOYMENT_TARGET=11.0` to the configure command to
match the configured CI job. Leave `CMAKE_OSX_ARCHITECTURES` unset for the native
architecture. Use a new build directory when changing generators. This guide
uses Unix Makefiles because the pinned SDK's Xcode branch adds fast-math and
code-signing settings requiring separate review; it does not recommend Xcode
as an interchangeable configuration. The native macOS ARM job in the
[24 November checkpoint](#native-platform-checkpoint--24-november-2026) passed
with that CI configuration. This does not establish a tested minimum macOS
deployment version or a universal binary. Signing, notarization and distribution
are separate release work.

### Windows PowerShell

Use a new build directory with an x64 Visual Studio generator:

```powershell
$rhodesSdkPath = (Resolve-Path ../vst3sdk).Path
cmake -S . -B build/vst3 -A x64 -DRHODES_BUILD_VST3=ON -DBUILD_TESTING=ON "-DRHODES_VST3_SDK_ROOT=$rhodesSdkPath"
cmake --build build/vst3 --config Release --target rhodes_vst3_checks --parallel 2
ctest --test-dir build/vst3 -C Release -R '^vst3\.' --output-on-failure --no-tests=error
```

If selecting Ninja instead, supply its matching compiler environment, add
`-DCMAKE_BUILD_TYPE=Release`, and omit Visual Studio's `-A x64`; do not mix
generators in one build directory. The observed Windows CI result below is bound
to a specific implementation commit and x64 build; actual DAW use still needs
its own verification.

## Find and inspect the bundle

With the Release configurations above, the complete bundles are:

```text
build/vst3/VST3/Release/RhodesResearch.vst3
build/vst3/VST3/Release/RhodesResearchFiltered.vst3
build/vst3/VST3/Release/RhodesResearchQuadrature.vst3
```

The observed Linux x86_64 modules are `Contents/x86_64-linux/RhodesResearch.so`,
`Contents/x86_64-linux/RhodesResearchFiltered.so` and
`Contents/x86_64-linux/RhodesResearchQuadrature.so` inside their respective bundles. The pinned SDK uses
the platform's bundle layout on macOS and Windows; retain the whole `.vst3`
directory, including `Contents/Resources/LICENSE.txt`, `THIRD_PARTY_NOTICES.md`
and `moduleinfo.json`.
The resources include the inspected SDK MIT notice. This does not select a
project-wide license for this repository's original code; see [RIGHTS.md](../RIGHTS.md).

The [VST3 CI workflow](../.github/workflows/vst3.yml) archives all three complete
bundles after its host tests and validators succeed. Each platform artifact
contains a `.tar.gz` archive inside GitHub's artifact ZIP. The archive check
verifies every entry, file hash, mode and internal symlink; keep the complete
bundle directories when extracting or installing. CI artifacts have a seven-day
retention period and are experimental build outputs, not a published release.

To find a tested build, open an
[Experimental VST3 CI run](https://github.com/baabi-boo/Fender-rhoades/actions/workflows/vst3.yml),
then check its source commit and the desired platform job, including successful
archive/upload steps. In the run's artifacts, choose
`experimental-vst3-<OS>-<architecture>-<full commit SHA>` for that platform.
Match the SHA suffix to the run's tested commit; a later Foundation CI result or
the current `main` revision does not change an older artifact's source identity.
The [retained successful run](https://github.com/baabi-boo/Fender-rhoades/actions/runs/45334821145)
is an example. If its artifacts have expired, use the source build instructions
above and retain the commit and validation results for the new build.

The build disables automatic links into user plug-in directories. To evaluate
the result in a DAW, use that host's normal installation/scanning procedure for
the complete bundle, then insert **Rhodes Research Prototype**,
**Rhodes Research Filtered** or **Rhodes Research Quadrature** on an instrument
track. Their category is `Instrument|Synth|Stereo`. A host may show its generic
parameter editor because the prototype supplies no custom view.

For a reproducible host trial, use the proposed
[manual DAW evaluation protocol](DAW_VALIDATION.md). It explains what to record
and which observations remain separate from the automated harness results.

## Playing and controlling the prototype

| Behavior | Current contract |
| --- | --- |
| Keyboard | MIDI pitches **28–100 inclusive**: 73 persistent physical-key states; valid pitches outside that range are ignored |
| MIDI input | One event bus accepting channels 1–16; channels share the same physical keys |
| Note ownership | Up to 256 simultaneous holders; overlapping holders may address one key, so this is not 256 independent resonators |
| Retrigger and release | Retrigger preserves mechanical state; only the last holder's release damps its key |
| Output | One stereo bus with identical left/right signal; no audio input |
| Processing | Float32 or float64; sample rates 32–192 kHz inclusive; at most **8192 frames** per host call |
| Monitoring Gain | Host parameter `0`, displayed multiplier `0..32x`, default `16x`; normalized value `0..1`, default `0.5` |
| Sustain | Host parameter `1`, `Off`/`On`; normalized values at least `0.5` select `On`; MIDI CC64 maps to it on all channels |
| Latency and tail | Direct: 0 frames; filtered and quadrature: 48 frames (1 ms at 48 kHz); all advertise an infinite asymptotic tail |

Monitoring gain multiplies synthetic harp voltage; it is not a calibrated
instrument amplifier. Gain automation is linearly interpolated and sustain is
stepped. Gain points are quantized to binary32 using nearest, ties-to-even;
the preceding block's final gain anchors interpolation at offset `-1`, and the
last duplicate point wins. At an equal sample offset, parameter changes precede
notes, which preserve their input-list order. The adapter
accepts at most 256 input note events, two parameter queues and 8192 points per
queue; each sample has a separate 256-event combined note/pedal limit. Invalid
input rejects the block while preserving configuration, mechanical and ownership
state. With a supported output declaration (float32/float64, one stereo bus,
1–8192 frames), rejection clears each supplied non-null channel buffer in full
and sets stereo silence flags. This includes malformed events/automation and a
configured-block-limit breach within the global 8192-frame limit. Unsupported
output frame, bus or channel counts, or sample formats, leave samples and silence
flags untouched. Zero-frame flushes access no sample pointers; an empty bus gets
silence flags `0`, a stereo bus gets `3`, and no output bus requires no work.
The [host contract](specs/HOST_PROTOTYPE_20261123.md) specifies full ordering,
pointer requirements and rejection behavior.

Pitch bend, note expression, physical half-pedal, CC120/CC123 mappings, effects
and additional historical circuits are outside this version. Host naming of
octaves differs, so use the MIDI pitch numbers when checking the range.

## Saving and resetting state

The exact 16-byte version-1 state stores monitoring gain and sustain for
synthetic model ID `1` in the direct variant, `2` in the filtered variant and `3`
in the quadrature variant. Every variant rejects both other model IDs: all six
ordered cross-model recalls are rejected without conversion. Rejection also
preserves any valid own-model recall already waiting for an audio boundary.
The format version remains 1; the model field
identifies the separate sound path. It does not store ringing notes. A valid recall is
queued through a lock-free mailbox and installed at a valid audio boundary:
mechanics, note holders, cursor, FIR history when present, and interpolation history reset before the
recalled configuration is applied. A malformed, short, extended or incompatible
chunk is rejected. Saving takes a coherent configuration snapshot without
resetting the sound.

Control-value conversion and state encoding preserve their bit-level contract
under supported host rounding and FTZ/DAZ modes without changing the host's
floating-point environment. This does not promise identical audio underflow
behavior across those modes; see the [control audit](agents/development-20261123/W3-A053.md)
and [actual host checks](agents/development-20261123/W3-A055.md).

Activation and processing lifecycle transitions also reset mechanics and
ownership while preserving configuration. Ordinary note release preserves
mechanical state. Arithmetic/output-conversion faults silence the failed sample
and remainder, preserve an already rendered prefix, and require a lifecycle
reset before processing continues.

## Observed verification and remaining gates

### Native platform checkpoint — 24 November 2026

Commit [015767f](https://github.com/baabi-boo/Fender-rhoades/commit/015767fee5635ff85a717739188ff4af60fe84f2)
passed all ten jobs across
[Foundation CI](https://github.com/baabi-boo/Fender-rhoades/actions/runs/45334821142)
and [Experimental VST3 CI](https://github.com/baabi-boo/Fender-rhoades/actions/runs/45334821145).
Each VST3 platform job built, validated, archived and uploaded all three bundles.
The [native confirmation](validation/native-platforms-20261124.md) and
[structured record](validation/native-platforms-20261124.json) bind these results
to their source hashes, job IDs and retained logs.

| Observed job | Passed | Skipped | Registered |
| --- | ---: | ---: | ---: |
| Linux core | 150 | 0 | 150 |
| Windows core | 147 | 0 | 147 |
| macOS ARM core | 141 | 6 | 147 |
| Linux VST3 | 138 | 0 | 138 |
| Windows x64 VST3 | 134 | 0 | 134 |
| macOS ARM VST3 | 125 | 9 | 134 |
| Linux address/undefined-behavior sanitizers | 150 | 0 | 150 |

The macOS skips explicitly exclude x86 FTZ/DAZ environment modes. They are not
passes. Linux additionally covers independently loaded module coexistence;
the corresponding Windows/macOS real-host scenario remains unverified. The
local Linux SDK-integrated run passed 288 of 288 CTests without skips.

All three platforms passed these six CTest entries:

| Variant | Official SDK validator | Metadata validator |
| --- | --- | --- |
| Direct | `vst3.sdk_validator` | `vst3.moduleinfo_validator` |
| Filtered | `vst3.filtered_sdk_validator` | `vst3.filtered_moduleinfo_validator` |
| Quadrature | `vst3.quadrature_sdk_validator` | `vst3.quadrature_moduleinfo_validator` |

The current macOS build propagates CoreFoundation from the pinned SDK's `base`
target to the benchmark/test executables. The control helpers apply Clang-only
precision and floating-point exception settings, and the quadrature FIR has a
function-scoped contraction guard. The previously failing native prepare test
passes with its exact equality assertion unchanged; the
[earlier one-ULP observation](validation/quadrature-clang-contraction-20261124.md)
remains recorded. These results confirm the tested repair, without establishing
a particular compiler-defect diagnosis or arbitrary compiler-flag behavior.

These are source-bound synthetic core and adapter results. Actual DAW use,
audio-device deadlines, measured fidelity and release qualification require
their own evidence.

### Historical three-variant checkpoint — 23 November 2026

At commit [3f08573](https://github.com/baabi-boo/Fender-rhoades/commit/3f08573cf5d40d8c23ab1c92e38b987aa1b4e14c),
the following jobs completed successfully on 2026-11-23. Each built all three
modules, ran its retained host tests and all three SDK validators, then verified
and uploaded the complete three-bundle archive:

| Platform job | Observed result |
|---|---|
| [Linux](https://github.com/baabi-boo/Fender-rhoades/actions/runs/45280519074/job/138063450540) | Build, host tests, three validators and archive/upload passed |
| [Windows x64](https://github.com/baabi-boo/Fender-rhoades/actions/runs/45280519074/job/138063450736) | Build, host tests, three validators and archive/upload passed |

The macOS job in that workflow was cancelled without recorded execution steps;
the overall workflow therefore has a cancelled conclusion. The two completed
job results above remain successful. These observations apply to the linked
commit; inspect the CI result for the particular checkout or artifact being
evaluated. They do not establish native DAW operation or device deadlines.

The retained [three-module loader fixture](agents/transient-campaign/w2_a33.md)
passed locally on Linux with all six load orders, both output formats and
48/192 kHz rates. It checks six distinct FUIDs, independent output/state,
all six cross-model recall directions and duplicate-module rejection. Its
CTest registrations are Linux-only; Windows/macOS coexistence in a real host
remains separate coverage. The [dedicated quadrature host fixture](agents/transient-campaign/w2_a32.md)
records local observation/convolution, pending-recall, reset and lifecycle
checks, with each report retaining the tested source and scope.

### Earlier retained host evidence

Before the quadrature addition, the integrator built the original Linux x86_64
bundle with GCC 14.2 and the pinned SDK and observed **47 SDK validator tests
passed, zero failed**. In CTest, those SDK checks are contained within
`vst3.sdk_validator`; the current build also registers
`vst3.filtered_sdk_validator` and `vst3.quadrature_sdk_validator`. Earlier host
suites exercise concrete processor/controller classes and include:

| Coverage | Retained evidence |
| --- | --- |
| Scalar output, initial ownership/automation/lifecycle/state coverage and allocation-counter calibration | [A024](agents/development-20261123/W2-A024.md); its 89 instrumented processor calls observed zero C++ allocation attempts |
| Control conversions across supported rounding and flush modes | [A053](agents/development-20261123/W3-A053.md) |
| Actual host control/state regression under FTZ/DAZ modes | [A055](agents/development-20261123/W3-A055.md) |
| Inactive buses, zero-frame flushes, malformed buffers and setup rollback | [A063](agents/development-20261123/W4-A063.md) |
| Bounded rejected-output cleanup, unchanged unsupported output declarations and preserved state | [W1-004](agents/continuation-20261123/w1-004.md); supersedes the earlier oversized-rejection clearing policy |
| Dense-automation rejection assertions and allocation-guarded host regression after the bounded cleanup change | [W1-010](agents/continuation-20261123/w1-010.md) |
| Model-1/model-2 wire and stream isolation, unchanged original golden bytes | [W1-015](agents/continuation-20261123/w1-015.md) |
| Independent filtered adapter event/control samples, rate/latency, recall and reset behavior | [W1-017](agents/continuation-20261123/w1-017.md) |
| Partial reads/writes, stream failures and transactional state recall | [A066](agents/development-20261123/W4-A066.md) |
| Independent automation model, ties, flushes and rejection | [A073](agents/development-20261123/W4-A073.md) |
| Independent note-ownership model, capacity, recall and randomized sequences | [A074](agents/development-20261123/W4-A074.md) |
| Actual processor state callbacks concurrent with audio processing, including pending-recall preservation | [A081](agents/development-20261123/W5-A081.md); two Release/ThreadSanitizer scenarios, with plugin-owned sources instrumented and SDK archives uninstrumented |

These are the scopes recorded by each report; the earlier allocation observation
does not instrument every later test or prove a universal allocation bound.
The concurrency checks use one audio owner and thread-local state streams;
lifecycle changes remain outside concurrent processing. Executed interleavings
do not establish exhaustive scheduling or native DAW coverage.
Use `ctest --test-dir build/vst3 -C Release -N -R '^vst3\.'` to enumerate the
tests configured in your own checkout.

The [export audit](agents/development-20261123/W4-A062.md) reduced the observed
Linux module's defined dynamic exports from 137 to 14 and its Rhodes
implementation exports from 120 to zero, while retaining `GetPluginFactory`,
`ModuleEntry` and `ModuleExit`. Remaining exports were standard-library symbols.
That binary still required `GLIBC_2.38`; the
[packaging audit](agents/development-20261123/W3-A058.md) also records
`GLIBCXX_3.4.29`. These are observed runtime requirements of the retained Linux
build, not a selected distribution baseline or a portable-binary guarantee.

The [actual callback benchmark](BENCHMARKING.md#actual-vst3-callback-benchmark)
adds measured calls through the concrete VST3 processor in float32/float64.
Its [retained matrix](validation/vst3-callback-benchmark-20261123.json) includes
nominal-period misses and does not establish device deadline compliance.

These results establish the reported engineering checks. Native DAW use,
device deadline behavior and release distribution remain
separate verification. The synthetic pickup still has the documented nonlinear
aliasing limits; the [retained diagnostic](agents/development-20261123/W4-A068.md)
does not establish an audibility bound. No antialiasing or physical-fidelity
acceptance follows from a successful SDK validator run. Follow the [project state](PROJECT_STATE.md) for
current milestone decisions.
