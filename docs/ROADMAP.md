# Development roadmap and exit criteria

## Objective and ordering

Build a sample-free physical Mark I model by establishing one measured note before
claiming a validated keyboard. The first target is an identified 73-key Stage
specimen at its loaded harp output, followed by its actual passive Stage output
circuit. Powered Suitcase families and acoustic output are later, separately
identified profiles. This ordering follows section 12 of the
[user-supplied research](research/USER_RESEARCH.txt).

`project/state.json` owns task status, dependencies, claims, blockers, and evidence.
[PROJECT_STATE.md](PROJECT_STATE.md) is its generated human-readable view. This
roadmap owns scientific intent and exit criteria; it deliberately does not copy
live completion statuses. A task can only be marked complete for the scope and
evidence it actually delivered.

The foundation has no measured specimen or calibrated fidelity result. The
synthetic renderer and test primitives are useful groundwork, not completion of
the one-note physical-model milestone.

## Canonical task map

| Task | Scope | Prerequisites | Exit artifact and gate |
|---|---|---|---|
| `FND-001` | Buildable repository, agent workflow, data contracts, documented architecture | None | Reproducible build/tests, state validation, generated-doc drift check, traceable setup evidence |
| `SPEC-001` | Identify one Stage specimen and configuration | Physical instrument access and authorized records | Resolved specimen passport, revision, regulation, key mapping, wiring/output boundary |
| `DATA-001` | Acquire calibrated pilot data and a real acquisition manifest | `FND-001`, `SPEC-001` | Actual immutable measurements, calibration/uncertainty, repeatability, condition split, rights and hashes |
| `PHY-001` | Analytic mechanical and pickup/network primitives | `FND-001` | Independent analytic tests, persistent state behavior, supported ranges, explicit synthetic parameters |
| `PHY-002` | Finite-mass hammer and damper reference with persistent note state | `PHY-001` | Contact/recontact/release tests, action state, energy accounting, convergence, declared synthetic/calibrated scope |
| `MAG-001` | Geometry-dependent pickup and reciprocal magnetic reference | `SPEC-001`, `DATA-001` | Identified linkage/force relations, map domain, polarity/turns contract, held-out subsystem checks |
| `ELEC-001` | Identified pickup network and defined load | `SPEC-001`, `DATA-001` | Verified circuit graph, coil/load parameters, silent-branch behavior, independent impedance/transfer checks |
| `VAL-001` | One-note integrated prediction | `PHY-002`, `MAG-001`, `ELEC-001` | Frozen model predicts held-out strikes, retriggers, release, pickup positions, and loads with justified thresholds |
| `KEY-001` | Construction zones and full keyboard calibration | `VAL-001` | Per-key/zone provenance, register transition checks, held-out keys or explicitly limited coverage |
| `RT-001` | Representative runtime benchmark and numerical error budget | `PHY-001` | Reproducible workload/hardware record, timing distribution/maxima, deadline accounting, reference-error report |
| `STG-001` | Connected passive Stage output controls | `VAL-001` | Verified volume/bass graph, measured tapers/endpoints, loaded transfer and held-out control/load validation |
| `CIR-001` | Optional revision-specific Suitcase circuits | `STG-001` | Verified graph and identified device/supply dynamics for each explicitly named profile |
| `ACO-001` | Optional acoustic output boundary | `CIR-001` | Defined driver/cabinet/listening geometry, terminal and acoustic validation, separate scope decision |

`SPEC-001` and consequently actual acquisition work are blocked while no physical
specimen/data is available. Continue useful unblocked implementation and analytic
validation rather than inventing a serial number, circuit revision, recording,
or component value. Canonical dependencies may be tightened as findings warrant;
record the reason instead of silently redefining an acceptance gate.

## Phase 0 — Reproducible engineering foundation

Create a small buildable core, deterministic offline renderer, meaningful tests,
portable build configuration, code contribution guidance, and a continuation
contract for new agents. Preserve the supplied research as source material and
label its unverified leads. Introduce versioned schemas/templates for specimen,
component, and acquisition metadata without populating them with fabricated
measurements.

The self-updating system means canonical state can regenerate status and guide
the next task, while checks detect drift, invalid dependencies, and unsupported
completion records. It does not mean unattended agents automatically design,
validate, or publish the product. Every worker still inspects the implementation,
performs its assigned work, records evidence, and hands integration to an owner.

**Exit:** build and tests succeed in the reported environment; tools reject
invalid state; generated files are reproducible; initial tasks/blockers are
honest; no source asset is silently granted reuse rights. A platform CI matrix
becomes verified coverage only when that platform's job actually passes.

## Phase 1 — Identity and pilot acquisition

Complete `SPEC-001` and `DATA-001` using
[MEASUREMENT_PLAN.md](MEASUREMENT_PLAN.md). Establish native hammer action, local
motion, pickup voltage, wiring, known loads, timing, calibration, and repeatability
for a representative key. Freeze complete held-out conditions before fitting.
Select a manageable pilot that exposes identification problems early.

The [specimen/acquisition contract](specs/DATA_INGESTION.md) specifies the coordinated
metadata upgrade needed for configuration history, calibration validity, payload
verification and derived-data ancestry. `specimen_identity` and `measurement` are
separate evidence gates. Neither is satisfied by a renamed synthetic fixture.

**Exit:** another contributor can understand the specimen and reproduce the
measurement configuration from the passport and manifests. All used records have
immutable hashes and rights. Unknown quantities and observational effects are
reported. A synthetic schema fixture cannot complete actual acquisition.

## Phase 2 — Predictive single-note reference

`PHY-001` establishes exact/analytic limits and state conventions. `PHY-002`
replaces impulse and loss-switch surrogates with finite-mass hammer, continuous
damper/contact states, and reference integration. It can make progress with
synthetic parameters while hardware is unavailable; such progress is algorithmic
evidence, not calibrated fidelity.

The next mechanics implementation follows [the contact reference contract](specs/CONTACT_REFERENCE.md), including reciprocal signs, branch-specific dissipation, coordinate-rescaling checks and continuous pedal requirements.

`MAG-001` identifies the flux-linkage map and compatible reaction forces.
`ELEC-001` identifies the actual loaded pickup graph. The [transduction and network contract](specs/TRANSDUCTION_NETWORK.md) specifies polarity, compatible energy, map domains and asymmetric circuit fixtures. These tasks can proceed
alongside mechanical identification; loading is part of the first note, not an
audio effect added after it. Define a consistent equilibrium and avoid adding
magnetic influence already embedded in measured mechanical parameters twice.

`VAL-001` combines the subsystems with a frozen configuration and predicts whole
unseen conditions. It must separate model error, numerical error, and acquisition
uncertainty. See [VALIDATION.md](VALIDATION.md) for the full eight-part gate.

**Exit:** acceptable condition-level predictions under justified thresholds for
strike strength, release/pedal, retrigger, pickup geometry, and electrical load.
Passing a clean sinusoidal decay test or producing a pleasant tone cannot pass
this gate. Scope restrictions must remain visible if some conditions fail.

## Phase 3 — Keyboard and passive Stage completeness

Expand only with a documented construction-zone strategy. Record representative
keys around hammer/tine/mounting transitions and extremes, then select additional
measurement density using sensitivity and residuals. Preserve persistent state
for all physical keys and circuit membership for all pickups. Any mechanical
sleeping or coupling reduction needs evidence.

`KEY-001` includes actual per-key variations and repeatability, not just 73
frequency-scaled copies of a central note. `STG-001` separately validates the selected Stage controls,
cable, and load at the defined output jack. If the loaded-harp and Stage-jack
boundaries differ, preserve both as explicitly named configurations.

**Exit:** register transitions and per-key behavior are characterized; held-out
coverage supports the intended range; keyboard claims state which keys,
conditions, and output boundary were tested.

## Phase 4 — Runtime and host integration

Start `RT-001` early with a representative synthetic kernel so computational risk
is visible. Report the limited workload. Repeat the benchmark when coupling,
contact, antialiasing, full keyboard data, and shared nonlinear circuits change
cost or error. Early kernel numbers are not a final-polyphony guarantee.

Specify callback constraints, supported sample rates, buffer/event semantics,
asset preparation, state serialization, and latency before adding host adapters.
The [runtime and asset contract](specs/RUNTIME_AND_ASSETS.md) records the proposed
interfaces, bounded event semantics and rejection/continuation fixtures.
Choose plug-in SDKs or UI dependencies only after reviewing their requirements
and licenses. The initial dependency-free CLI does not select a final commercial
framework or grant any SDK rights.

Host adapters and distributable plug-ins need explicit future tasks in canonical
state once the scope is chosen. Do not label a renderer build as a plug-in
release. Windows, Linux, and macOS claims require their own observed build/host
results.

**Exit:** documented reference error and aliasing bounds over the supported
operating domain; callback timing and deadline behavior under recorded stress;
host lifecycle/state/latency tests for each claimed release format.

## Phase 5 — Identified circuit families and optional acoustics

`CIR-001` adds only explicitly identified profiles. Verify schematic revision
against available board/connectivity evidence, document corrections, retain all
reference designators, and select justified device models. Model shared stages
as shared: chords must interact through the connected circuitry. Handle optical,
thermal, supply, and transformer histories where they affect measurements.

`ACO-001` requires a new boundary decision covering amplifier, loudspeaker,
cabinet, and listening/microphone geometry. Validate electrical loading and
acoustic radiation separately before combined evaluation. The initial Stage
scope does not need a room model to establish its loaded-output accuracy.

**Exit:** each profile has its own evidence and limitations. No invented hybrid is
sold as a historical circuit, and no general authenticity claim is inferred from
one board or one microphone position.

## Parallel work and change control

Useful parallel lanes are analytic mechanics, data tooling, circuit inventory,
reference-solver design, and performance harnesses. A lane may explore a later
phase without claiming its prerequisites are complete. Give each worker concrete
paths, test obligations, and a report identifier. The integrator resolves shared
contracts and updates canonical state once evidence has been reviewed.

When a discovery changes the plan, record a decision, update dependencies and
acceptance criteria, preserve previous evidence with its original scope, and
regenerate the project view. Do not treat more files, more agents, or a higher
number of tests as a substitute for passing the next physical gate.

## Immediate continuation priorities

Use the state tool's `next` command to determine the current actionable task.
Typical useful work after foundation integration is to harden analytic
primitives, implement finite-mass contact with a bounded reference harness, or
benchmark the explicitly synthetic kernel. The main external dependency remains
an identified specimen and calibrated pilot measurements. Record that dependency
precisely and continue work that does not depend on fabricated physical facts.
