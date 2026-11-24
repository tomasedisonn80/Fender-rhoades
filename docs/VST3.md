# Experimental VST3 instrument

**Rhodes Research Prototype 0.1.0**, **Rhodes Research Filtered 0.2.0** and
**Rhodes Research Quadrature 0.3.0** wrap the sample-free synthetic
`ResearchEngine` in separate VST3 instruments. All three provide ordinary host
parameter controls and an optional native editor using the owner's exact
photographic reference. The editor is under qualification; the
[current checkpoint](#current-photo-editor-checkpoint--24-november-2026) separates
implemented behavior from executed native-window evidence. Their sound uses impulse excitation,
switched-loss release, a polynomial pickup and a synthetic resistive network.
The separate finite-mass contact reference is offline and does not run inside
any of these plug-ins. No specimen calibration or measured fidelity is established.

| Variant | Bundle / build target | Audio and saved-state contract |
|---|---|---|
| Direct research model | `RhodesResearch.vst3` / `rhodes_vst3` | Original class IDs, model-1 state, zero reported latency |
| Filtered research model | `RhodesResearchFiltered.vst3` / `rhodes_vst3_filtered` | Separate class IDs, model-2 state, 48-frame FIR delay |
| Quadrature research model | `RhodesResearchQuadrature.vst3` / `rhodes_vst3_quadrature` | Separate class IDs, model-3 state, six fractional observations per host cell, 48-frame delay |

Each variant has its own processor/controller FUID pair, defined in
[plugin_ids.hpp](../plugins/vst3/plugin_ids.hpp). The MIDI interoperability,
Expression/mono and photo-editor updates preserve all six class identities, model IDs `1/2/3`
and reported latencies `0/48/48` frames. Version-1 configuration loads with
Expression at unity; both legacy formats load neutral panel defaults. Extended
configuration uses the adaptive format described below. A
project can load the three instruments together; there is no live sound-path
or latency switch.

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

The implementation contracts are [HOST_PROTOTYPE_20261123](specs/HOST_PROTOTYPE_20261123.md),
its [MIDI interoperability continuation](specs/HOST_MIDI_20261124.md), and the
reviewed [Expression and mono contract](specs/HOST_EXPRESSION_MONO_20261124.md).
The [photo-editor contract](specs/PHOTO_EDITOR_20261124.md) adds the photographic
surface, panel controls and their coherent configuration. The current
[panel codec](../include/rhodes/plugin_state.hpp) and
[effect implementation](../include/rhodes/front_panel_effects.hpp) define the
extended state and synthetic signal processing.
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
git -C ../vst3sdk submodule update --init -- base cmake pluginterfaces public.sdk vstgui4
git -C ../vst3sdk rev-parse HEAD
git -C ../vst3sdk submodule status base cmake pluginterfaces public.sdk vstgui4
```

The root commit is the inspected `v3.8.1_build_84` revision. Configuration verifies
that root commit and four core SDK submodule revisions plus the editor's
`vstgui4` revision `5db272256172557818b6158cf0bb2c4410bddb25`, and rejects missing checkouts or changed
tracked SDK contents. This keeps the compiled dependency tied to the inspected
source. Untracked build directories and unused optional submodules are allowed.
Use a separate clean SDK checkout when experimenting with SDK patches; the build
never resets or modifies your files. Updating the pins is a separate compatibility
decision.
`RHODES_BUILD_VST3_EDITOR=ON` is the default when the optional adapter is enabled.
It links the pinned VSTGUI editor binding; SDK examples, standalone examples,
scripting and the Wayland backend remain disabled. Linux uses the X11 backend.
Add `-DRHODES_BUILD_VST3_EDITOR=OFF` for a headless adapter using host generic
controls. That configuration needs only the four core SDK submodules and avoids
VSTGUI and its GUI system-library dependencies; the panel parameters, state
format and audio processing remain available.

For the editor on Debian/Ubuntu, install the development packages used by the
[workflow](../.github/workflows/vst3.yml) before configuring:

```sh
sudo apt-get install -y libx11-dev libfreetype6-dev libxcb1-dev libxcb-util-dev \
  libxcb-cursor-dev libxcb-keysyms1-dev libxcb-xkb-dev libxkbcommon-dev \
  libxkbcommon-x11-dev libglib2.0-dev libcairo2-dev libpango1.0-dev \
  libfontconfig1-dev libwayland-dev wayland-protocols libgtkmm-3.0-dev \
  libatkmm-1.6-dev libsqlite3-dev
```

The pinned VSTGUI configuration still needs Wayland development headers/protocols
while its Wayland backend is disabled. A nonstandard dependency installation may
also need `WAYLAND_PROTOCOLS_BASEDIR` pointing to its `share/wayland-protocols`
directory. Xvfb is useful for automated Linux window tests; it is not part of the
plug-in's audio engine. Windows/macOS use their platform SDKs and native VSTGUI
backends; their editor builds need separate executed qualification.

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
against each actual bundle. Dual-/triple-module loader checks,
`vst3.loaded_midi` and `vst3.loaded_expression_*` are configured for Linux,
Windows and macOS. The loaded MIDI and Expression fixtures load all three
production bundles through SDK-only host linkage. Direct host fixtures cover
Expression automation, controls, transactions, bounds, concurrent recall and
mono/stereo buffers alongside the existing MIDI checks. See
[HOST-MIDI verification](HOST_MIDI_VERIFICATION.md) for the earlier scope and
the [current Expression verification section](#current-expression-and-output-layout-verification--24-november-2026)
for the new fixtures and acceptance boundary. A configured platform job or
registered test is not an executed result.
The metadata checks compare generated
`moduleinfo.json` with the loadable factory; resource-presence checks remain
separate. For a plug-in-only build, select `--target rhodes_vst3`,
`--target rhodes_vst3_filtered` or `--target rhodes_vst3_quadrature`; tests can be
disabled with `-DBUILD_TESTING=OFF`. To build the three modules without the
test aggregate, pass all three target names after `--target`.

On macOS, add `-DCMAKE_OSX_DEPLOYMENT_TARGET=11.0` to the configure command to
match the configured CI job. Leave `CMAKE_OSX_ARCHITECTURES` unset for the native
architecture. Use a new build directory when changing generators. This guide
uses Unix Makefiles because the pinned SDK's Xcode branch adds fast-math and
code-signing settings requiring separate review; it does not recommend Xcode
as an interchangeable configuration. The native macOS ARM job in the
[historical 24 November checkpoint](#historical-native-platform-checkpoint--24-november-2026) passed
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

### Select Expression and output-layout checks

The optional host aggregate does not build the dependency-free state-codec
executable. Build both targets to run that codec together with the current host
fixtures:

```sh
cmake --build build/vst3 --config Release \
  --target rhodes_expression_state_tests rhodes_vst3_checks --parallel 2
ctest --test-dir build/vst3 -C Release -N \
  -R '^(expression_state\.|vst3\..*(expression_|mono_))'
ctest --test-dir build/vst3 -C Release \
  -R '^(expression_state\.|vst3\..*(expression_|mono_))' \
  --output-on-failure --no-tests=error
```

The `-N` invocation lists the tests in the configured checkout without running
them. The focused command includes codec/stream, control/audio, malformed
transaction, capacity, concurrency, mono-buffer and loaded-module fixtures.
Use the full `^vst3\.` selection above to include legacy host behavior and all
six SDK/module-info validators. Keep each run's source, registered count,
failures and skips with its output; a focused run does not replace a complete
current-source platform run. In PowerShell, put each wrapped command on one line.

### Retain diagnostics and compare legacy unity output

The [diagnostic helper](../tools/capture_ci_diagnostics.py) can retain the exact
CTest inventory, complete console output, CTest's raw log, JUnit XML and command
status. Choose a fresh output directory for each attempt; existing records are
not overwritten. For example, a logged run of the complete host selection is:

```sh
python3 tools/capture_ci_diagnostics.py ctest \
  --output-dir build/vst3/diagnostics-1 --label ctest -- \
  ctest --test-dir build/vst3 -C Release -R '^vst3\.' \
  --output-on-failure --no-tests=error
```

Native CI also captures `metadata-before.json` and `metadata-after.json`, binding
source/SDK revisions, compiler and build settings, and available module hashes
to the run. The [workflow](../.github/workflows/vst3.yml) contains the complete
metadata and build commands. Diagnostics may describe a failed or incomplete
attempt; check the command status, test failures and explicit skips.

Legacy unity comparison is optional in a local configuration. Its baseline is
the three complete bundles built from exactly
`6c91dab00855b67a559e92edb79ae49e567be469`, using the same native toolchain and
pinned SDK as the candidate. The baseline bundle root must also contain a
`manifest.json` recording that source commit and all three module paths/hashes.
The CI workflow builds this separate checkout with `BUILD_TESTING=OFF` and the
three module targets, then creates the manifest with the helper's
`baseline-manifest` subcommand. Do not identify a newer build as the baseline.

After preparing that baseline and configuring the current build as above, add
its absolute bundle-root path, build the checks and run the comparison:

```sh
cmake -S . -B build/vst3 \
  -DRHODES_VST3_LEGACY_BUNDLE_ROOT=/absolute/baseline/bundle-root
cmake --build build/vst3 --config Release \
  --target rhodes_vst3_checks --parallel 2
ctest --test-dir build/vst3 -C Release \
  -R '^vst3\.legacy_unity_comparison$' --output-on-failure --no-tests=error
```

Replace the example path with the directory containing the three `.vst3`
bundles and manifest. An empty `RHODES_VST3_LEGACY_BUNDLE_ROOT` means the comparison
test is **not registered**; it is not a passing skip. An explicit directory
without the manifest is a configure error. The
[comparison driver](../tools/compare_vst3_legacy.py) runs isolated module captures
for legacy, current default and current explicit-unity modes. With the build
directory above, it writes `build/vst3/legacy-comparison/Release/report.json` and
a unique run directory containing raw captures. Retain incomplete reports and
first failures as well as completed comparisons. This command is a reproduction
recipe, not a claim that the matrix has passed on any particular platform.

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
and `moduleinfo.json`. Editor-enabled bundles also require `rhodes-suitcase.png`,
`rhodes-suitcase.uidesc`, `VSTGUI-LICENSE.txt`, `RapidJSON-LICENSE.txt`,
`miniz-LICENSE.txt` and `Expat-LICENSE.txt` in that resource directory. Do not move the module binary
without its resources.
The resources include the inspected SDK MIT notice. This does not select a
project-wide license for this repository's original code; see [RIGHTS.md](../RIGHTS.md).

The [VST3 CI workflow](../.github/workflows/vst3.yml) archives all three complete
bundles after its host tests, configured legacy comparison and validators
succeed. Each platform bundle artifact
contains a `.tar.gz` archive inside GitHub's artifact ZIP. The archive check
verifies every entry, file hash, mode and internal symlink; keep the complete
bundle directories when extracting or installing. Complete bundle artifacts
have a seven-day retention period and are experimental build outputs, not a
published release.

Diagnostics upload is configured to run even after a failure and has a 90-day
retention period. It includes `ci-evidence/` test/metadata records, available
CTest/configure logs, the baseline manifest and legacy-comparison captures.
An uploaded diagnostics artifact does not imply that a usable bundle was built
or that any test passed.

To find a tested build, open an
[Experimental VST3 CI run](https://github.com/baabi-boo/Fender-rhoades/actions/workflows/vst3.yml),
then check its source commit and the desired platform job, including successful
archive/upload steps. New runs use these names:

| Artifact | Name | Retention |
| --- | --- | --- |
| Validated complete bundles | `experimental-vst3-<OS>-<architecture>-<full commit SHA>-<run ID>-<attempt>` | 7 days |
| Raw test/build diagnostics | `vst3-diagnostics-<OS>-<architecture>-<full commit SHA>-<run ID>-<attempt>` | 90 days |

Match the SHA, run ID and attempt to the job being evaluated; a later Foundation CI result or
the current `main` revision does not change an older artifact's source identity.
The [retained successful run](https://github.com/baabi-boo/Fender-rhoades/actions/runs/45334821145)
is an older example whose bundle artifact names predate the run/attempt suffix.
If its artifacts have expired, use the source build instructions above and
retain the commit and validation results for the new build.

The build disables automatic links into user plug-in directories. To evaluate
the result in a DAW, use that host's normal installation/scanning procedure for
the complete bundle, then insert **Rhodes Research Prototype**,
**Rhodes Research Filtered** or **Rhodes Research Quadrature** on an instrument
track. Their factory category is `Instrument|Synth`. The output defaults to
stereo and may negotiate standard mono or stereo with the host.
Editor-enabled builds provide the photographic custom view; hosts may also offer
their generic parameter editor. Headless builds provide only generic controls.
Confirm that a current build exposes **24 parameters**, including the five panel
controls. Older cached bundles may expose 18 or 19. The first 19 parameter
positions retain their previous IDs and order; the new five are appended.

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
| Output | One bus, stereo by default, with standard mono or stereo negotiation; neutral panel preserves the legacy scalar/duplicated signal, active modulation can separate stereo channels; no audio input |
| Processing | Float32 or float64; sample rates 32–192 kHz inclusive; at most **8192 frames** per host call |
| Monitoring Gain | Host parameter `0`, displayed multiplier `0..32x`, default `16x`; normalized value `0..1`, default `0.5` |
| Sustain | Host parameter `1`, `Off`/`On`; normalized values at least `0.5` select `On`; MIDI CC64 maps to it on all channels |
| Expression | Automatable continuous host parameter `2`, displayed multiplier `0..1x`, normalized value `0..1`, default `1` (`1x`); MIDI CC11 on every input channel addresses this same global control |
| Bass / Treble | IDs `3` / `4`; normalized `0..1` maps to `−12..+12 dB`, default `0 dB` (`0.5`); synthetic first-order shelves with 200 Hz / 2500 Hz split frequencies |
| Intensity | ID `5`; normalized/displayed `0..1x`, default `0`; sets the modulation depth while Vibrato is on |
| Speed | ID `6`; normalized `0..1` maps to `0.5..12 Hz`, default `6.25 Hz` (`0.5`) |
| Vibrato | ID `7`, `Off`/`On`, default `Off`; the photographed label controls opposed stereo amplitude modulation, not pitch modulation |
| All Notes Off | 16 writable, nonautomatable command parameters, `All Notes Off Ch 1` through `All Notes Off Ch 16`; IDs `0x1000..0x100f` map MIDI CC123 to the corresponding channel |
| Polyphonic pressure | Well-formed typed pressure events are validated and ignored; there is no pressure-responsive synthesis |
| Latency and tail | Direct: 0 frames; filtered and quadrature: 48 frames (1 ms at 48 kHz); all advertise an infinite asymptotic tail |

There are **24 host parameters** in total: eight persistent controls and sixteen
channel release commands. Every delivered CC123 parameter point
releases that channel's note holders, including zero and repeated unchanged
values. Values must be finite and normalized to `0..1`; they are not interpolated,
edge-triggered or automatically reset. Duplicate commands at the same channel
and offset have the same result as one. A physical key receives an ordinary
release only when its last holder disappears, so other channels' holders, the
shared sustain setting, mechanical motion and filter tails are preserved.
With sustain on, released keys can continue ringing. CC123 is not All Sound Off;
CC120 remains unmapped. A host may deduplicate unchanged controller values before
delivery, and the plug-in cannot recover omitted commands. Repeated CC123 delivery
therefore needs a separate check in each native DAW.

### Native photographic editor and panel effects

The editor uses the [full original reference](../assets/reference/rhodes-suitcase-reference.webp),
converted losslessly for VSTGUI loading. The default view is `1024 × 768`, with
`768 × 576` and `1536 × 1152` logical size limits and a fixed 4:3 aspect ratio.
The intended idle surface contains the photograph alone. Transient highlights,
focus indicators and value readouts identify an interaction without replacing
the photographed controls. Image drawing and hit testing share the same transform.

Volume changes Monitoring Gain; click the pictured pedal to toggle Sustain.
Drag a knob vertically or an EQ slider horizontally; Shift selects fine movement.
Wheel input and keyboard focus provide alternate control editing, and a double
click restores a control's default. The pictured keys send audition messages
through the VST3 controller/processor connection, using the same 73-key range.
The native host must provide that connection; failed delivery is shown as a
connection status, not as a successful sounding key. Audition commands use a
bounded audio-boundary queue and separate UI key ownership from host MIDI.
Exact native gestures and audible host behavior remain qualification gates.

The [digital effect](../include/rhodes/front_panel_effects.hpp) applies Bass and
Treble after Monitoring Gain, followed by opposed left/right amplitude modulation.
Changes in shelf gains, effective depth and speed are smoothed over 10 ms;
reset applies the saved configuration immediately. Phase runs while modulation
is bypassed. Settled neutral shelves and zero effective depth return the original
input bits, and the effects add no lookahead or reported latency. A nonzero
Intensity needs Vibrato `On` to affect sound. This circuit-independent recipe
supports the photographed controls without claiming a calibrated Peterson or
Janus preamplifier, amplifier, speaker or room model.

### Use Expression for performance dynamics

Set Monitoring Gain to the desired listening level, then automate Expression or
send CC11 to vary the whole instrument's output. Both controls can be automated
independently. Expression `1x` preserves the original unity-expression audio
path; `0.5x` halves the monitored signal amplitude; `0x` silences the output.
At zero, note ownership, mechanics, filtering and release tails keep advancing
while the host continues processing. Returning to `1x` reveals the continuing
sound. Expression changes neither note velocity nor the physical model and does
not model a historical volume circuit. Monitoring Gain is also a digital
multiplier of synthetic harp voltage, with no amplifier calibration.

CC11 on channels 1–16 maps to the same global parameter, so a message from any
channel affects notes held on every channel. The host delivers normalized
parameter values to the plug-in; raw MIDI CC11 value 64 is not a guaranteed
exact `0.5` fixture. CC43 is unmapped, and the plug-in performs no CC11/CC43 pairing
or 14-bit reconstruction. Use direct host automation at `0.5` when an exact
half-scale comparison is needed. Host MIDI-to-parameter delivery still needs
checking in the [named-DAW protocol](DAW_VALIDATION.md).

The adapter computes and checks `(32 * gain) * volts`, then processes the panel
effects and any mono fold-down before applying Expression. At settled neutral
panel settings and exact unity Expression, it returns that monitored value directly. Engine failures and
nonfinite monitored values fault even when Expression is zero. Expression is
applied before the final float32/float64 output conversion, so attenuation may
bring a finite double into the float32 output range. Conversion checks continue
even when an inactive output is omitted.

### Automation and bounded input

Gain, Expression, Bass, Treble, Intensity and Speed use independent linear
interpolation; sustain and Vibrato are stepped.
Gain/Expression points are canonical binary32 values, rounded to nearest with
ties to even. Each control's offset `-1` anchor is the audio owner's current
canonical live value after any recall selected for that block. A mailbox snapshot
may contain a newer pending recall and is not the interpolation anchor. The last
point at a duplicate offset wins, and the final value is held after the final
point. Output interpolation uses binary64 values; saved configuration uses their
canonical binary32 values. Equivalent curves split across blocks need the
corresponding boundary anchors. At an equal sample offset, sustain takes effect
first, CC123 releases follow in ascending channel order, then notes retain their
input-list order. A new strike at that offset survives the channel release.

The adapter accepts at most **256 input events**, including pressure, **24 unique
parameter queues**, **8192 raw points for each persistent control**,
and **256 raw CC123 points in total across all channels**, including duplicates. Translation
uses fixed storage for at most **585 physical note actions per block**; each
sample has a separate **256-action limit including sustain**. Pressure consumes
the input-event budget and participates in offset ordering but emits no physical
action or ownership change. It requires bus 0, an ordered valid sample offset,
finite PPQ, SDK channel 0–15 (MIDI channel 1–16), pitch 0–127 and finite binary32
pressure `0..1`; all signed note IDs are accepted. Other unsupported input event
kinds still reject the block.

Invalid or over-budget input rejects the block atomically, preserving
configuration, mechanical state, ownership and any pending valid recall. This
includes duplicate parameter queues and malformed points discovered late in a
queue. Accepted arithmetic faults retain the reset-required policy described
under [Saving and resetting state](#saving-and-resetting-state).

### Output layouts and buffer handling

Select mono or stereo through the host's output configuration while processing
is suspended. The accepted arrangement updates the bus name and channel count
together: `Mono Out` has one channel and `Stereo Out` has two. Unsupported layout
requests preserve the last accepted arrangement. With equal panel output
channels, mono returns the left value directly,
preserving the legacy scalar path. When the panel creates different stereo
channels, mono uses `0.5 * left + 0.5 * right`. The opposed modulation therefore
folds to a steady attenuation at a fixed depth; it does not create cyclic mono
tremolo. There is no added pan-law gain. Host routing or panning may affect a
later capture, so compare at
the plug-in boundary when evaluating the layouts. Layout selection belongs to
the host and is not saved in the instrument's state chunk; latency and tail
reporting remain unchanged.

For host implementers, positive-frame calls obey these buffer rules:

| Output declaration | Required behavior |
| --- | --- |
| Active output supplied | One bus with the negotiated one/two-channel width, its channel-pointer array, and every declared sample pointer non-null |
| Inactive output supplied | One bus with the negotiated width and its channel-pointer array; individual sample pointers may be null |
| Inactive output omitted (`numOutputs = 0`) | Accept otherwise valid processing; continue mechanics, events, automation and fault checks without touching an output pointer or silence flag |
| Active output omitted | Reject the block |

For rejection cleanup, a supported declaration is float32/float64, one bus,
one or two declared channels and 1–8192 frames. Each supplied non-null sample
buffer is cleared in full, with silence flags `1` for mono or `3` for stereo.
The declared cleanup width is recognized independently of the negotiated layout,
so a rejected mono/stereo mismatch can still be cleared safely. Cleanup includes
malformed events/automation and a configured-block-limit breach within the global
8192-frame limit. Unsupported frame, bus or channel counts, or sample formats,
leave samples and silence flags untouched.

Zero-frame flushes may omit the output bus or supply a zero-channel or
negotiated-width bus. They never inspect sample pointers. An empty bus gets
silence flags `0`, mono gets `1`, and stereo gets `3`; omission touches no flags.
Flushes accept only offset-zero parameter points and an empty event list. A
selected recall is installed first, followed by final persistent control
values and staged CC123 releases. Ordinary releases advance no mechanics, frame
cursors or filter history; an accepted recall intentionally resets them. The
[Expression and mono contract](specs/HOST_EXPRESSION_MONO_20261124.md) gives the
full transaction and pointer requirements.

Pitch bend, note expression, physical half-pedal, CC120 mapping and identified
historical electronic circuits are outside this version. The panel effects below
are synthetic digital processing. Host naming of
octaves differs, so use the MIDI pitch numbers when checking the range.

### Generic parameter text

Text conversion is portable and locale-independent. Input must be ASCII, at most
**127 characters**, with a NUL terminator within 128 UTF-16 code units, including
any whitespace and unit suffix in that limit.

| Control | Accepted text |
| --- | --- |
| Monitoring Gain | Decimal multiplier `0..32`, optional leading sign, fraction and `e`/`E` exponent; surrounding ASCII whitespace and an optional lowercase `x` suffix are allowed, for example `16x` or `+1.6e1 x` |
| Sustain | Exactly `Off` or `On`, case-sensitive, without extra whitespace; the normalized threshold remains `0.5` |
| Expression | Decimal multiplier `0..1`, using the same signed decimal/exponent grammar, surrounding ASCII whitespace and optional lowercase `x`; examples: `0`, `0.5x`, `+5e-1 x`, `1` |
| Bass / Treble | Decimal `−12..12`, optional exact `dB` suffix; surrounding ASCII whitespace is allowed |
| Intensity | Decimal `0..1`, optional exact lowercase `x` suffix |
| Speed | Decimal `0.5..12`, optional exact `Hz` suffix |
| Vibrato | Exactly `Off` or `On`, with the same threshold as Sustain |
| All Notes Off commands | Decimal normalized value `0..1`, using the gain decimal grammar and surrounding ASCII whitespace, with no unit suffix; numeric display retains enough digits for a binary64 round trip |

Numeric input must be consumed completely. NaN, infinity, hexadecimal numbers,
commas, trailing garbage, overflow, nonzero decimals that underflow to binary64
zero, and out-of-range values are rejected without clamping. Range checks use
the rounded finite binary64 value before gain normalization or Expression
canonicalization; for example, `32.000000000000000001` can round to 32 and be
accepted for Monitoring Gain. Expression must be within `0..1` at that binary64
stage, even if a value just outside the range would later round to binary32 unity.
For nonnegative controls, negative nonzero values, including subnormals, are rejected; signed zero is
canonicalized. Gain and Expression display canonical binary32 control values
with enough digits to round-trip; `x` is kept in their unit metadata. Sustain
retains its `Off`/`On` display.
Invalid IDs, null pointers, uninitialized controllers and invalid normalized
display values also fail conversion without changing caller output or saved
configuration. Converting command text does not execute a channel release.

## Saving and resetting state

The `RHST` configuration chunk uses synthetic model ID `1` for the direct
variant, `2` for filtered and `3` for quadrature. Those IDs remain unchanged;
the format version describes the stored controls, not a different sound path.
Gain/sustain retain parameter IDs `0/1`, and Expression uses `2`.

| Chunk or saved setting | Current behavior |
| --- | --- |
| Own-model legacy version 1, exactly 16 bytes | Load Monitoring Gain and sustain; Expression defaults to `1x` and all panel controls reset to defaults |
| Own-model version 2, exactly 20 bytes | Load Gain, sustain and Expression; all panel controls reset to defaults |
| Save with exact panel defaults and canonical Expression `1x` | Write version 1 with the exact legacy 16-byte encoding |
| Save with exact panel defaults and non-unity Expression | Write version 2, exactly 20 bytes |
| Save with any nondefault panel setting | Write version 3, exactly 40 bytes, preserving all eight persistent controls |
| Valid own-model v2/v3 with default panel and Expression `1x` | Accept; a later save canonicalizes it to version 1 |
| Wrong model, version, length or invalid control encoding | Reject without changing configuration or consuming an already pending valid recall |

Version 2 keeps the original field positions and appends little-endian binary32
Expression at bytes 16–19. Version 3 retains those fields and appends binary32
Bass, Treble, Intensity, Speed and Vibrato at bytes 20–39. Vibrato is encoded as
an exact boolean float. A bypassed effect with a nondefault Speed or Intensity
still needs version 3: saving never silently discards inactive settings. The
[legacy codec](../include/rhodes/instrument_state.hpp) and
[extended codec](../include/rhodes/plugin_state.hpp) define the exact formats.
These lengths describe the plug-in chunk, not a host preset or project container.

**Rollback limits:** version-1-only builds cannot load version 2 or 3, and
version-2-only builds cannot load version 3. To export a v1/v2-compatible chunk,
restore Bass/Treble to `0 dB`, Intensity to `0x`, Speed to `6.25 Hz`, and Vibrato to
`Off`; set Expression to `1x` as well for version 1. Save a separate preset before
returning to the older build. Those older formats cannot retain nondefault panel
settings. Host projects can also retain automation, parameter inventory and
output routing, so a compatible chunk alone does not guarantee project rollback.
Keep the original project/preset and the build that created it when evaluating
a newer prototype.

Every variant rejects both other model IDs: all six ordered cross-model recalls
are rejected without conversion. State stores neither ringing notes, UI-held keys, queued audition messages,
transient CC123 command values nor the negotiated mono/stereo layout. A valid recall is
queued through a lock-free mailbox and installed at a valid audio boundary:
mechanics, note holders, cursor, FIR history when present, panel effect history
and interpolation history reset before the recalled configuration is applied. Malformed fields,
incorrect lengths and incompatible models/versions are rejected. Saving takes
one coherent snapshot of all eight persistent controls without resetting the sound.

Control-value conversion and state encoding preserve their bit-level contract
under supported host rounding and FTZ/DAZ modes while preserving those controls.
Text conversion does not promise unchanged floating-point status flags. This
does not promise identical audio underflow behavior across those modes; see the
[control audit](agents/development-20261123/W3-A053.md)
and [actual host checks](agents/development-20261123/W3-A055.md).

Activation and processing lifecycle transitions also reset mechanics and
ownership while preserving configuration. Ordinary note release preserves
mechanical state. Arithmetic/output-conversion faults silence the failed sample
and remainder, preserve an already rendered prefix, and require an explicit
hard reset before processing continues. A lifecycle transition or a valid
configuration recall installed at an accepted audio boundary supplies that reset.

## Observed verification and remaining gates

### Current photo-editor checkpoint — 24 November 2026

**HOST-004 remains under development and acceptance review.** The unchanged
source photo and verified lossless PNG are committed; the
[asset verifier](../tools/verify_editor_assets.py) checks encoded hashes, dimensions
and the retained profile, with an optional decoded-pixel comparison. All three
native GUI modules have built locally on Linux. The
[SDK-only loaded editor fixture](../tests/cpp/vst3_photo_editor_test.cpp) passed
3,855 unattached-interface checks across nine controllers and 24 created views,
including 54 malformed content scales. Those checks do not open a native window.

The direct module has also attached in the official SDK `editorhost` on Linux
under Xvfb and rendered a real `1024 × 768` native window. The captured idle
window was visually inspected and shows the complete reference composition.
An initial attachment attempt failed in the SDK test host because the isolated
X server lacked its expected `_XEMBED_INFO` atom; preparing that host prerequisite
allowed the window to open without a plug-in code change.

Gesture behavior, quantitative image comparison, resize/cleanup behavior of an
attached view, editor-enabled Windows/macOS runs and named-DAW operation remain
separate qualification gates. Earlier platform records below apply only to the
revisions and feature scope they identify. Final editor acceptance needs retained
source-bound screenshots, packaged-resource checks and executed platform results;
the unattached API result alone cannot establish those properties.

### Current Expression and output-layout verification — 24 November 2026

**HOST-003 remains under development and acceptance review.** Its
[frozen contract](specs/HOST_EXPRESSION_MONO_20261124.md) requires independent
audio/control references, state migration and concurrency checks, mono/stereo
buffer checks, legacy unity replay and native module results for the executed
source. Implementation began in
[41c9d85](https://github.com/baabi-boo/Fender-rhoades/commit/41c9d85);
the [campaign record](agents/CONTINUATION_D_20261124.md) and
[live project state](PROJECT_STATE.md) track subsequent work and acceptance.

The retained [initial Linux integration](validation/host-expression-initial-linux-20261124.json)
and [additional host-fixture record](validation/host-expression-additional-host-fixtures-20261124.json)
describe distinct executions, each with its own source and test scope. Focused
passes added after a full-suite run are not one larger full-suite result. Final
counts, skips and platform acceptance must remain tied to the integrator's
source-bound acceptance record.

| Current fixture scope | Source |
| --- | --- |
| Version-1 compatibility, version-2 domains, malformed packets, models and mailbox words | [State codec fixture](../tests/cpp/expression_state_test.cpp) |
| Adaptive stream transfers, lengths and failure handling | [Stream fixture](../tests/cpp/vst3_expression_stream_test.cpp) |
| Default/unity output, independent automation, mute/resume and partitioned curves | [Audio fixture](../tests/cpp/vst3_expression_audio_test.cpp) |
| Metadata, strict text, recall/lifecycle and floating-point controls | [Control fixture](../tests/cpp/vst3_expression_control_test.cpp) |
| Malformed queues, preflight atomicity, flushes and inactive transactions | [Transaction fixture](../tests/cpp/vst3_expression_transaction_test.cpp) |
| Queue/point/event bounds and instrumented allocation checks | [Bounds fixture](../tests/cpp/vst3_expression_bounds_test.cpp) |
| Coherent publication, live interpolation anchors and callback interleavings | [Concurrency fixture](../tests/cpp/vst3_expression_concurrency_test.cpp) |
| Mono/stereo metadata, scalar output, pointers, omission, cleanup and lifecycle | [Mono fixture](../tests/cpp/vst3_mono_contract_test.cpp) |
| Actual loaded modules, controls/state, audio/layout and coexistence | [Loaded-module fixture](../tests/cpp/vst3_loaded_expression_test.cpp) |

The table identifies implemented checks, not an all-platform result. Earlier
HOST-001/HOST-002 platform passes do not validate this extension. Current-source
Linux, Windows and macOS evidence, including SDK/module validators and legacy
audio comparison, must be reviewed for the new gate. A named-DAW result also
requires the separate [manual protocol](DAW_VALIDATION.md); none follows from
these harnesses. Device deadlines, release qualification and measured fidelity
retain their own acceptance requirements.

### Current HOST-002 verification — 24 November 2026

**HOST-002 is complete in the canonical task graph.** The
[final criterion review](validation/host-midi-final-criterion-review-20261124.json)
incorporates the recovered macOS results and passes the frozen gate. Earlier
[CC123](agents/reports/continuation-c/w5-082.md),
[pressure](agents/reports/continuation-c/w5-083.md) and
[control-text](agents/reports/continuation-c/w5-084.md) reviews remain scoped to
their recorded executions. The
[original criterion review](validation/host-midi-acceptance-20261124.md) records
C04 as incomplete at its historical cutoff; the later
[recovered-results record](validation/host-midi-macos-recovered-20261124.json)
and final review supply the subsequent evidence. These results predate
HOST-003/HOST-004 and do not qualify the current editor extension. The
[canonical project state](PROJECT_STATE.md) records the accepted transition.

The [native cutoff record](validation/host-midi-native-final-20261124.json),
[selected logs](validation/host-midi-native-final-selected-20261124.log) and
[review](agents/reports/continuation-c/w5-081.md) retain these actual executions.
VST3 jobs used [ce59b0c](https://github.com/baabi-boo/Fender-rhoades/commit/ce59b0c254ab99b399f03e63094b10ce10933e71);
Foundation jobs used [d4c8a6b](https://github.com/baabi-boo/Fender-rhoades/commit/d4c8a6b2668e478f48facd2eeb146aff0f551506).

| Check / retained evidence | Executed source | Result |
| --- | --- | --- |
| [Local Linux integration](validation/host-midi-linux-final-20261124.json) | Incremental dirty `ce59b0c` with fixture/generator repairs | 327/327 CTests passed; no skips |
| [Local Python](validation/host-midi-python-final-20261124.json) | Repaired working tree | 888 reported: 887 passed, 1 skip |
| Native [Linux](https://github.com/baabi-boo/Fender-rhoades/actions/runs/45373649775/job/138374005076) / [Windows x64](https://github.com/baabi-boo/Fender-rhoades/actions/runs/45373649775/job/138374004751) VST3 | `ce59b0c` | 157/157 CTests passed per platform; no skips |
| [Native Linux core](https://github.com/baabi-boo/Fender-rhoades/actions/runs/45376430320/job/138383891694) | `d4c8a6b` | 170/170 CTests passed; no skips |
| [Native Windows core](https://github.com/baabi-boo/Fender-rhoades/actions/runs/45376430320/job/138383892161) | `d4c8a6b` | 167/167 CTests passed; no skips |
| [Linux address/undefined-behavior sanitizers](https://github.com/baabi-boo/Fender-rhoades/actions/runs/45376430320/job/138383892155) | `d4c8a6b` | 170/170 CTests passed; no skips |
| [Native Linux Python](https://github.com/baabi-boo/Fender-rhoades/actions/runs/45376430320/job/138383892070) | `d4c8a6b` | 888 reported: 838 passed, 50 skips |
| [Native Windows Python](https://github.com/baabi-boo/Fender-rhoades/actions/runs/45376430320/job/138383892223) | `d4c8a6b` | 888 reported: 830 passed, 58 skips |

Both native VST3 jobs passed all six SDK/module-info validators and the loaded
module/MIDI checks. The three additional Linux core tests require `/dev/full`;
Windows does not register them. Python totals include skips, with their reasons
retained in the linked records.

The [local source audit](agents/reports/continuation-c/w4-073.md) matches the
retained 99-file C++ freeze to `d4c8a6b`; the execution remains an incremental
dirty-tree run, not a clean `d4c8a6b` build. The published
[4cf0de7](https://github.com/baabi-boo/Fender-rhoades/commit/4cf0de7089e5f622916cc5f6a25bb4034715d781)
update adds documentation/evidence while preserving runtime/test sources.
The native record confirms 91/91 applicable VST3 source/build files match
`ce59b0c` and 195/195 Foundation code files match `d4c8a6b`; this does not
promote later documentation or project records to remotely tested state.

At the **10:57:30.673 UTC cutoff**, the original macOS VST3 job had been
cancelled without execution. The requested retry had appeared as attempt 2,
[job 138393683256](https://github.com/baabi-boo/Fender-rhoades/actions/runs/45373649775/job/138393683256),
still queued with no runner or steps. The `d4c8a6b` Foundation workflow ended
cancelled: macOS core never ran and macOS Python stopped without a completed
suite summary. Its successful Linux/Windows jobs above retain their own results.
The replacement [Foundation run at 4cf0de7](https://github.com/baabi-boo/Fender-rhoades/actions/runs/45379468043)
had seven queued jobs. The later
[closing status readback](validation/host-midi-native-closure-20261124.json) at
**11:20:37 UTC** records the macOS VST3 retry as cancelled without execution,
five successful Linux/Windows Foundation jobs, and both macOS Foundation jobs
still queued without runners or steps. It adds status observations, not new test
counts. The [dated continuation handoff](validation/host-midi-handoff-20261124.md)
records the evidence missing at that cutoff; the recovered macOS supplement
linked above is a later observation. The cutoff reports remain unchanged and
do not establish an all-green current revision. Native DAW delivery, device deadlines,
minimum-OS execution, signing/release and measured fidelity remain separate gates.

The dated historical checkpoints below predate HOST-002 and retain their original
source-bound scope; they do not validate its CC123, pressure or text changes.

### Historical native platform checkpoint — 24 November 2026

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
passes. At this checkpoint, Linux additionally covered independently loaded
module coexistence; the corresponding Windows/macOS real-host scenario remains
unverified. The local Linux SDK-integrated run passed 288 of 288 CTests without skips.

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
CTest registrations were Linux-only at that checkpoint; the current three-platform
registration does not extend that retained observation. Windows/macOS coexistence
in a real host remains separate coverage. The [dedicated quadrature host fixture](agents/transient-campaign/w2_a32.md)
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
