# Rhodes Mark I — Physical Modeling Research

[![Foundation CI](https://github.com/baabi-boo/Fender-rhoades/actions/workflows/ci.yml/badge.svg)](https://github.com/baabi-boo/Fender-rhoades/actions/workflows/ci.yml)

A sample-free, component-informed model of a **Fender Rhodes Mark I**, built from
mechanical motion, electromagnetic pickup conversion, and the connected electrical
network. The first target is one identified **73-key Stage specimen** at a defined
loaded output. The repository name is retained for existing links.

**Current stage: research foundation.** There is a buildable C++20 kernel, an
offline WAV renderer, analytical tests, and an evidence-based workflow for human
and AI contributors. Parameters are synthetic; no physical specimen has been
measured, no fidelity claim has been established, and no audio plug-in has been
released. The [live project state](docs/PROJECT_STATE.md) is generated from the
[canonical task graph](project/state.json).

## Start here

| I want to… | Read or run |
|---|---|
| Understand the physical model | [Architecture](docs/ARCHITECTURE.md) |
| Inspect the current numerical equations | [Numerical contract](docs/NUMERICS.md) |
| See completed work and blockers | [Generated project state](docs/PROJECT_STATE.md) |
| Continue development with an agent | [Precise continuation prompt](docs/agents/CONTINUE.md) |
| Follow the engineering sequence | [Roadmap and acceptance gates](docs/ROADMAP.md) |
| Collect useful instrument data | [Measurement plan](docs/MEASUREMENT_PLAN.md) |
| Evaluate accuracy and numerical behavior | [Validation specification](docs/VALIDATION.md) |
| Build and contribute | [Development](docs/DEVELOPMENT.md) · [Contributing](CONTRIBUTING.md) |
| Measure the synthetic kernel | [Benchmark methodology](docs/BENCHMARKING.md) |
| Inspect the supplied research | [Research and source provenance](docs/research/README.md) |
| Audit the setup agents | [Actual-agent campaign](docs/agents/CAMPAIGN.md) |

## Build and run

Requirements: C++20 compiler, CMake 3.24+, and Python 3.11+ for project tooling.
The initial core has **no third-party runtime dependencies** and downloads no
packages during its build.

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release --parallel 2
ctest --test-dir build -C Release --output-on-failure
```

Render a synthetic research note on Linux or macOS:

```sh
./build/rhodes_render --output build/research-note.wav --seconds 3 \
  --sample-rate 48000 --key 40 --velocity 0.7
```

With a Visual Studio multi-configuration build, use
`build/Release/rhodes_render.exe`. `--key` is a zero-based physical-key index
`0..72`; index 40 maps to MIDI 68 in the synthetic E1–E7 mapping. `--velocity` is
a normalized impulse intensity, **not measured hammer speed**. The renderer's
float WAV is a diagnostic artifact with an explicit monitoring gain; it is not
calibrated instrument output. Existing files require explicit `--force`.

Run the repository integrity checks:

```sh
python3 tools/project.py validate
python3 tools/project.py sync --check
python3 tools/validate_data.py
python3 tools/check_docs.py
python3 tools/campaign.py validate
python3 tools/campaign.py sync --check
python3 -m unittest discover -s tests/python -v
```

On Windows, substitute `python` for `python3` where appropriate. Ninja users may
use the `dev`, `release`, and `sanitize` CMake presets. See
[development instructions](docs/DEVELOPMENT.md) for platform and CI details.

## What exists, and what still needs research

| Subsystem | Implemented foundation | Required next work |
|---|---|---|
| Mechanical motion | Exact discrete linear damped modes; persistent per-key state | Identified tine/tonebar/support properties and coupled structure |
| Excitation and release | Explicit impulse and switched-loss surrogates | Finite-mass hammer, unilateral contact, damper and pedal mechanics |
| Pickup | Nonlinear polynomial flux-linkage derivative after modal summation | Measured geometry-dependent map, reciprocal forces and validity bounds |
| Harp circuit | Synthetic resistive series/parallel network retaining all 73 branches | Verified specimen topology, complex coil impedance, parasitics and load |
| Stage controls | Architecture and component requirements | Revision-specific connected passive circuit |
| Instrument identity | Unidentified profile with units and provenance fields | Actual specimen, construction revisions and measurements |
| Runtime | Fixed-storage research core and deterministic offline rendering | Convergence, antialiasing, bounded host event processing and measured deadlines |
| Host product | Build/test infrastructure | Plugin adapters, UI, packaging and release validation |

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
The task graph makes these available without marking the measurement blockers
as solved.

## Rights

This is an independent research project, with no claimed Fender or Rhodes
affiliation. A project-wide software license has not yet been selected by the
owner. Research links do not grant rights to copy source, figures or recordings.
See [rights and attribution](RIGHTS.md) and [security](SECURITY.md).
