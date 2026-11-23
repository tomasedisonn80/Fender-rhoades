# Experimental VST3 instrument

**Rhodes Research Prototype 0.1.0** wraps the sample-free synthetic
`ResearchEngine` in a VST3 instrument. It provides ordinary host parameter
controls; there is no custom editor. Its sound uses impulse excitation,
switched-loss release, a polynomial pickup and a synthetic resistive network.
The separate finite-mass contact reference is offline and does not run inside
this plug-in. No specimen calibration or measured fidelity is established.

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

`rhodes_vst3_checks` exists when `BUILD_TESTING=ON` and builds the bundle,
SDK validator, callback benchmark and every retained host-test executable. It
keeps the test command complete as coverage grows. For a plug-in-only build,
use `--target rhodes_vst3`; tests can be disabled with `-DBUILD_TESTING=OFF`.

On macOS, add `-DCMAKE_OSX_DEPLOYMENT_TARGET=11.0` to the configure command to
match the configured CI job. Leave `CMAKE_OSX_ARCHITECTURES` unset for the native
architecture. Use a new build directory when changing generators. This guide
uses Unix Makefiles because the pinned SDK's Xcode branch adds fast-math and
code-signing settings requiring separate review; it does not recommend Xcode
as an interchangeable configuration. The observed test result below is
specifically Linux. Signing, notarization and distribution are separate release
work.

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
generators in one build directory. Native
Windows verification is still required before claiming Windows compatibility.

## Find and inspect the bundle

With the Release configurations above, the complete bundle is:

```text
build/vst3/VST3/Release/RhodesResearch.vst3
```

The observed Linux x86_64 module is
`Contents/x86_64-linux/RhodesResearch.so` inside that bundle. The pinned SDK uses
the platform's bundle layout on macOS and Windows; retain the whole `.vst3`
directory, including `Contents/Resources/LICENSE.txt`, `THIRD_PARTY_NOTICES.md`
and `moduleinfo.json`.
The resources include the inspected SDK MIT notice. This does not select a
project-wide license for this repository's original code; see [RIGHTS.md](../RIGHTS.md).

The build disables automatic links into user plug-in directories. To evaluate
the result in a DAW, use that host's normal installation/scanning procedure for
the complete bundle, then insert **Rhodes Research Prototype** on an instrument
track. Its category is `Instrument|Synth|Stereo`. A host may show its generic
parameter editor because the prototype supplies no custom view.

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
| Latency and tail | Zero advertised latency; asymptotically decaying resonators advertise an infinite tail |

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
synthetic model ID `1`. It does not store ringing notes. A valid recall is
queued through a lock-free mailbox and installed at a valid audio boundary:
mechanics, note holders, cursor and interpolation history reset before the
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

On 2026-11-23, the integrator built the actual Linux x86_64 bundle with GCC 14.2
and the pinned SDK, then observed **47 SDK validator tests passed, zero failed**.
In CTest, those SDK checks are contained within `vst3.sdk_validator`. The
retained host suites exercise concrete processor/controller classes and include:

| Coverage | Retained evidence |
| --- | --- |
| Scalar output, initial ownership/automation/lifecycle/state coverage and allocation-counter calibration | [A024](agents/development-20261123/W2-A024.md); its 89 instrumented processor calls observed zero C++ allocation attempts |
| Control conversions across supported rounding and flush modes | [A053](agents/development-20261123/W3-A053.md) |
| Actual host control/state regression under FTZ/DAZ modes | [A055](agents/development-20261123/W3-A055.md) |
| Inactive buses, zero-frame flushes, malformed buffers and setup rollback | [A063](agents/development-20261123/W4-A063.md) |
| Bounded rejected-output cleanup, unchanged unsupported output declarations and preserved state | [W1-004](agents/continuation-20261123/w1-004.md); supersedes the earlier oversized-rejection clearing policy |
| Dense-automation rejection assertions and allocation-guarded host regression after the bounded cleanup change | [W1-010](agents/continuation-20261123/w1-010.md) |
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
macOS/Windows runs, device deadline behavior and release packaging remain
separate verification. The synthetic pickup still has the documented nonlinear
aliasing limits; the [retained diagnostic](agents/development-20261123/W4-A068.md)
does not establish an audibility bound. No antialiasing or physical-fidelity
acceptance follows from a successful SDK validator run. Follow the [project state](PROJECT_STATE.md) for
current milestone decisions.
