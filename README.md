# Rhodes Mark I — Physical Modeling Research

[![Foundation CI](https://github.com/baabi-boo/Fender-rhoades/actions/workflows/ci.yml/badge.svg)](https://github.com/baabi-boo/Fender-rhoades/actions/workflows/ci.yml)

A sample-free, component-informed model of a **Fender Rhodes Mark I**, built from
mechanical motion, electromagnetic pickup conversion, and the connected electrical
network. The first target is one **73-key Stage specimen, once identified**, at a defined
loaded output. The repository name is retained for existing links.

**Current stage: experimental VST3 instrument and physical-model research.**
The C++20 synthetic core now has bounded sample-offset events, MIDI note ownership,
versioned state recall, and three optional VST3 variants with a native photo
editor with recorded Linux verification. All three retain their class identities, saved-state
model IDs and default neutral-panel/unity-expression rendering path.
**Rhodes Research Quadrature** batches six prepared observations of
the synthetic source inside each host-frame interval and reports 48 frames
of latency under its separate model-3 identity. All three also expose ordinary
host parameter controls. A separate offline finite-mass hammer/damper reference
includes energy and event diagnostics; it does not run inside the plug-ins.
Parameters remain synthetic; no physical specimen has been measured, no fidelity
claim has been established, and no plug-in release has been published.
The [live project state](docs/PROJECT_STATE.md) is generated from the
[canonical task graph](project/state.json).

The [full quadrature study](docs/validation/quadrature-audio-full-20261123.json)
passed both independent comparisons for **2,097 cases / 2,534,400 output frames**
at nine exact sample rates. Maximum error against the refined continuous
reference was **8.32 × 10⁻¹² V**, below the unchanged **10⁻⁴ V** peak limit.
The later [batched implementation replay](docs/validation/batched-quadrature-20261124.md)
preserved all **2,097 Q6 outputs** and all **24 direct/filtered WAV fixtures**
exactly in the retained Linux/GCC run. The original
[legacy compatibility result](docs/validation/quadrature-legacy-compatibility-20261123.json)
remains available. This replay checks output identity; it does not repeat the
independent numerical integration. All timing captures and their outliers are
retained separately from those numerical results.

The historical [native platform record](docs/validation/native-platforms-20261124.md)
documents successful Linux, Windows and macOS builds and host tests. All three
variants passed the SDK and module metadata validators on each platform at the
recorded revision; exact test counts and platform-specific skips are listed.
These retained results predate HOST-002 and HOST-003 and do not validate their
changes or establish native DAW operation or audio-device deadlines.

The [HOST-003 expression and output-layout continuation](docs/specs/HOST_EXPRESSION_MONO_20261124.md)
adds **Expression**, a global digital multiplier from `0..1x`, default `1x`,
independent of Monitoring Gain. Automate it in the host or send MIDI CC11 on any
of the sixteen input channels; each channel addresses the same control for the
whole instrument. The plug-in does not pair CC11 with CC43. Expression at zero
silences the output while notes, mechanics and release tails continue. All three
variants gained Expression and mono/stereo output in that continuation. With the
new front panel at its defaults, mono remains the same scalar signal as either
stereo channel, Expression `1x` retains the legacy 16-byte state, and other
Expression values use the 20-byte version-2 state. The photo-editor extension
below adds its own controls and version-3 state without changing those defaults.
The [VST3 guide](docs/VST3.md) explains controls, output layouts and rollback
limits. This engineering extension remains under
[source-bound acceptance review](docs/VST3.md#current-expression-and-output-layout-verification--24-november-2026);
its test fixtures do not establish measured fidelity or named-DAW results.

The [HOST-002 MIDI continuation](docs/specs/HOST_MIDI_20261124.md) adds
channel-specific CC123 release commands, validated polyphonic pressure as a
no-op, and strict generic-control text conversion. Shared sustain and ringing
tails are preserved; pressure does not alter synthesis. Retained checks include
[327/327 local Linux CTests](docs/validation/host-midi-linux-final-20261124.json)
and [157/157 native VST3 CTests on each of Linux and Windows](docs/validation/host-midi-native-final-20261124.json).
The [CC123](docs/agents/reports/continuation-c/w5-082.md),
[pressure](docs/agents/reports/continuation-c/w5-083.md) and
[control-text](docs/agents/reports/continuation-c/w5-084.md) criterion reviews
pass within their frozen synthetic scope. **HOST-002 is complete in the canonical
task graph:** the [supplemental final criterion review](docs/validation/host-midi-final-criterion-review-20261124.json)
incorporates the recovered macOS evidence. The earlier
[incomplete review](docs/validation/host-midi-acceptance-20261124.md) and
[dated handoff](docs/validation/host-midi-handoff-20261124.md) remain historical
records. These HOST-002 results predate the Expression/mono and photo-editor
extensions and do not establish named-DAW behavior for the current source.

The [HOST-004 photo-editor continuation](docs/specs/PHOTO_EDITOR_20261124.md)
uses the owner's [original photograph](assets/reference/rhodes-suitcase-reference.webp)
as the full 4:3 editor surface, including the cabinet, legs, background and pedal.
Its lossless PNG conversion preserves decoded image pixels and the color profile.
The implementation aligns interactive controls and all 73 keys with the picture;
Volume and the pedal use the existing gain and sustain parameters. Bass, Treble,
Intensity, Speed and Vibrato add five persistent controls, for **24 host parameters**
while retaining the first 19 parameter positions. The tone shelves and opposed
stereo amplitude modulation are explicitly synthetic digital effects. They do
not identify the photographed instrument's electronics. Nondefault panel settings
use a **40-byte version-3 state**; default panel settings retain legacy v1/v2
exports. The final Linux replay passes **136 interaction checks**, including
exact reference pixels at the maximum-size idle view and resizing through all
three supported sizes. The three variants pass **39 native lifecycle cases**,
including removal inside host callbacks, repeated reopening and sibling windows.
Fresh current-module captures preserve **608,256 paired legacy stereo frames**
exactly. **HOST-004 is complete for this recorded Linux scope**, with a
[criterion-by-criterion review](docs/validation/photo-editor-acceptance-20261124.md)
and registered numerical evidence. Native Windows/macOS editor behavior, non-unit display scaling and
named-DAW operation remain unverified; see the
[current editor checkpoint](docs/VST3.md#current-photo-editor-checkpoint--24-november-2026).

The separate [four-fixture contact diagnostic](docs/CONTACT_PEAK_DIAGNOSTIC.md)
compares synthetic compression and force maxima with independent references,
including optional damped peak timing. **PHY-002 and RT-001 remain incomplete**:
general contact extrema and remaining direct/filtered convergence questions
still need work. Instrument measurements and calibrated fidelity remain separate
validation gates.

## Start here

| I want to… | Read or run |
|---|---|
| Understand the physical model | [Architecture](docs/ARCHITECTURE.md) |
| Inspect the current numerical equations | [Numerical contract](docs/NUMERICS.md) |
| See completed work and blockers | [Generated project state](docs/PROJECT_STATE.md) |
| Continue development with an agent | [Precise continuation prompt](docs/agents/CONTINUE.md) |
| Follow the engineering sequence | [Roadmap and acceptance gates](docs/ROADMAP.md) |
| Implement the next physical subsystem | [Detailed implementation contracts](docs/specs/README.md) |
| Collect useful instrument data | [Measurement plan](docs/MEASUREMENT_PLAN.md) |
| Evaluate accuracy and numerical behavior | [Validation specification](docs/VALIDATION.md) |
| Build and contribute | [Development](docs/DEVELOPMENT.md) · [Contributing](CONTRIBUTING.md) |
| Measure the synthetic kernel | [Benchmark methodology](docs/BENCHMARKING.md) |
| Inspect the supplied research | [Research and source provenance](docs/research/README.md) |
| Audit the setup agents | [Actual-agent campaign](docs/agents/CAMPAIGN.md) |
| Audit the original host-adapter development | [Five waves of development](docs/agents/DEVELOPMENT_CAMPAIGN.md) |
| Audit the filtered-audio continuation | [Five-wave continuation](docs/agents/CONTINUATION_CAMPAIGN.md) |
| Audit quadrature and portability development | [Quadrature campaign and report hashes](docs/agents/QUADRATURE_CAMPAIGN.md) |
| Audit current main-branch development | [24 November campaign and report ledgers](docs/agents/DEVELOPMENT_WAVES_20261124.md) |
| Audit MIDI interoperability development | [Separate continuation-C ledger](project/continuation-c-20261124.json) |
| Audit Expression and mono development | [Five-wave continuation-D campaign](docs/agents/CONTINUATION_D_20261124.md) |
| Audit the reference-photo editor development | [Completed five-wave campaign and ledger](docs/agents/PHOTO_EDITOR_CAMPAIGN_20261124.md) |
| Inspect the filtered audio design | [Timing, response and compatibility contract](docs/specs/FILTERED_AUDIO_20261123.md) |
| Inspect the quadrature audio design | [Frozen Q6 timing and verification contract](docs/specs/QUADRATURE_AUDIO_20261123.md) |
| Inspect the original VST3 prototype contract | [Original host behavior and later overrides](docs/specs/HOST_PROTOTYPE_20261123.md) |
| Inspect current MIDI and control behavior | [CC123, pressure and strict text contract](docs/specs/HOST_MIDI_20261124.md) |
| Inspect Expression, state compatibility and mono output | [Frozen HOST-003 contract](docs/specs/HOST_EXPRESSION_MONO_20261124.md) |
| Inspect the exact photo target and panel extension | [Native editor contract](docs/specs/PHOTO_EDITOR_20261124.md) |
| Build and try the VST3 instrument | [VST3 setup and controls](docs/VST3.md) |
| Evaluate a build in a named DAW | [Proposed manual evaluation protocol](docs/DAW_VALIDATION.md) |
| Run the separate hammer/damper experiments | [Offline contact reference](docs/CONTACT_REFERENCE.md) |
| Compare the four analytic contact fixtures | [Contact peak diagnostic](docs/CONTACT_PEAK_DIAGNOSTIC.md) |

## Build and run

Requirements: C++20 compiler, CMake 3.24+, and Python 3.11+ for project tooling.
The test build needs `std::barrier` and `std::jthread` library support; see
[toolchain requirements](docs/DEVELOPMENT.md) for the Apple build boundary.
The initial core has **no third-party runtime dependencies** and downloads no
packages during its build.

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release --parallel 2
ctest --test-dir build -C Release --output-on-failure --no-tests=error
```

Render a synthetic research note on Linux or macOS:

```sh
./build/rhodes_render --output build/research-note.wav --seconds 3 \
  --sample-rate 48000 --key 40 --velocity 0.7
```

Choose an audio path with the same renderer:

| Option | Audio path | Reported latency |
|---|---|---|
| `--audio-path direct` (default) | Original synthetic source at the requested rate | 0 frames |
| `--audio-path filtered` | Existing 4x/2x/1x internal-rate source and fixed FIR | 48 frames |
| `--audio-path quadrature` | Six fractional observations per host-frame interval and a fixed convolution kernel | 48 frames |

For example, add `--audio-path quadrature` to the render command above. Every
path retains the requested file duration; the two delayed paths include their
causal delay without trimming or extending the file. Quadrature's first sample
after reset is zero because its convolution uses completed intervals. Its success
message reports six quadrature nodes per host cell, the host sample rate and
latency; direct and filtered messages retain their internal-rate metadata.

With a Visual Studio multi-configuration build, use
`build/Release/rhodes_render.exe`. `--key` is a zero-based physical-key index
`0..72`; index 40 maps to MIDI 68 in the synthetic E1–E7 mapping. `--velocity` is
a normalized impulse intensity, **not measured hammer speed**. The renderer's
float WAV is a diagnostic artifact with an explicit monitoring gain; it is not
calibrated instrument output. Output is staged and published only after a
successful render. Existing regular files require explicit `--force`; final
symlinks are refused. New-file publication requires local hard-link support.

Run the repository integrity checks:

```sh
python3 tools/project.py validate
python3 tools/project.py sync --check
python3 tools/validate_data.py
python3 tools/check_docs.py
python3 tools/campaign.py validate
python3 tools/campaign.py sync --check
python3 tools/development_campaign.py validate
python3 tools/development_campaign.py sync --check
python3 tools/validate_continuation_campaign.py
python3 tools/quadrature_campaign.py validate
python3 tools/quadrature_campaign.py sync --check
python3 tools/validate_development_waves.py
python3 tools/verify_vst3_matrix_archive.py \
  docs/validation/vst3-matrix-20261124.json \
  docs/validation/vst3-matrix-batched-20261124.json \
  docs/validation/vst3-matrix-batch-final-20261124.json
python3 tools/generate_audio_filter.py --check
python3 tools/generate_quadrature_filter.py --check
python3 -m unittest discover -s tests/python -v
```

On Windows, substitute `python` for `python3` where appropriate. The `sh`
examples use POSIX backslash continuations; in PowerShell, put each wrapped
command on one line. Ninja users may use the `dev`, `release`, and
supported-platform `sanitize` CMake presets;
the sanitizer preset excludes Windows. See
[development instructions](docs/DEVELOPMENT.md) for platform and CI details.

## What exists, and what still needs research

| Subsystem | Implemented foundation | Required next work |
|---|---|---|
| Mechanical motion | Exact discrete linear damped modes; persistent per-key state | Identified tine/tonebar/support properties and coupled structure |
| Excitation and release | Impulse/switched-loss audio core; separate finite-mass p=1 contact reference and four-fixture peak comparison | Full independent refinement/extrema acceptance and calibrated action mechanics |
| Pickup | Nonlinear polynomial flux-linkage derivative after modal summation | Measured geometry-dependent map, reciprocal forces and validity bounds |
| Harp circuit | Synthetic resistive series/parallel network retaining all 73 branches | Verified specimen topology, complex coil impedance, parasitics and load |
| Stage controls | Architecture and component requirements | Revision-specific connected passive circuit |
| Instrument identity | Unidentified profile with units and provenance fields | Actual specimen, construction revisions and measurements |
| Runtime | Bounded events; direct, sampled-filtered and batched Q6 paths; full synthetic quadrature/reference matrix and exact retained replay | Remaining direct/filtered aliasing and convergence gates, future-model error budgets and representative host/device timing |
| Host product | Three VST3 variants, isolated model states 1/2/3, 24 controls including global CC11 Expression and synthetic panel effects, adaptive v1/v2/v3 configuration, mono/stereo output, a native photo editor implementation, bounded MIDI/automation and reported latency | HOST-003 and HOST-004 source-bound acceptance, attached editor/visual qualification, native DAW/device validation, deployment baselines and release packaging |

Peterson/Janus Suitcase electronics, amplifiers, loudspeakers and room acoustics
belong to separately identified future profiles. They do not silently become part
of the first Stage target.

## Physical rules the project preserves

1. A retrigger strikes an already moving system; it never resets its phase.
2. Release and pedal alter mechanical damping/contact while state persists.
3. Local modal motion is combined **before** nonlinear magnetic conversion.
4. Flux linkage already contains winding turns. Its derivative produces voltage.
5. Stationary pickups remain electrical loads in the connected harp network.
6. Shared nonlinear circuits process the combined instrument signal.
7. A detailed offline reference and an optimized runtime have separate error budgets.
8. Documentary, synthetic, numerical, measured, perceptual and performance evidence
   support different claims; one cannot substitute for all the others.

## A workflow that stays current

`project/state.json` holds task dependencies, acceptance criteria, blockers,
ownership and completion evidence. `project/evidence.json` records immutable
artifact hashes and evidence kinds. `tools/project.py` validates this state,
selects ready work, coordinates claims, records handoffs and regenerates the
status document. CI rejects inconsistent state and stale generated documents.

```sh
python3 tools/project.py next
python3 tools/project.py sync
```

Agents start with [AGENTS.md](AGENTS.md) and the
[continuation prompt](docs/agents/CONTINUE.md). They claim a ready task, implement
within a declared scope, run meaningful checks, record evidence, and leave an
actionable handoff. One integrator controls shared state and Git publication when
workers run concurrently.

This is automatic **state validation and document regeneration**. It does not
launch AI workers, fabricate progress, execute recorded evidence commands, or
push changes without an authorized agent. The
[100-agent setup ledger](docs/agents/CAMPAIGN.md) records actual assignments and
their reports separately from the earlier research referenced by the attachment.

## First scientific milestone

Predict one measured note across held-out strike strengths, releases, repeated
strikes, pickup positions and electrical loads. First establish an actual
specimen and calibrated data, then explain its behavior with the right physical
interactions. A plausible electric-piano sound alone does not pass this gate.

There are useful software tasks before hardware becomes available: finite-mass
contact, reference-solver convergence, data tooling and documented benchmarks.
The task graph makes these available once their software prerequisites pass,
without marking the measurement blockers as solved.

## Rights

This is an independent research project, with no claimed Fender or Rhodes
affiliation. A project-wide software license has not yet been selected by the
owner. Research links do not grant rights to copy source, figures or recordings.
See [rights and attribution](RIGHTS.md) and [security](SECURITY.md).
