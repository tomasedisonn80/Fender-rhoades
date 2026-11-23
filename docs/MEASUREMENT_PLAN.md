# Specimen, acquisition, and identification plan

## Current blocker and first useful acquisition

No physical specimen, calibrated recording set, measured pickup map, or verified
Stage netlist has been supplied. Unknown fields must remain unknown. The initial
project can test algorithms with synthetic fixtures, prepare tools, and validate
file contracts, but cannot pass a hardware-fidelity gate.

The first acquisition target is one documented 73-key Mark I Stage specimen and
one representative middle-register key. Record its actual construction and
condition before assigning a model year or extrapolating to the full keyboard.
The project should obtain one traceable pilot dataset before purchasing a large
measurement campaign or treating a fixture design as final.

This plan operationalizes sections 1, 3–6, and 10–12 of the
[user-supplied research](research/USER_RESEARCH.txt). It specifies future work;
it does not assert that these measurements have occurred.

The [acquisition acceptance contract](specs/DATA_INGESTION.md) defines the versioned
passport, calibration, partition-ancestry and payload requirements. Current
version-1 metadata checks implement only part of that contract; schema acceptance
does not complete this measurement plan.

## 1. Specimen passport

Create a stable specimen ID and keep personally identifying owner information
outside public records unless publication is authorized. The passport should
contain:

- Manufacturer/model markings, key count, serial/date markings where available,
  photographs, and a documented confidence level for interpretation.
- Harp, action, hammer-tip, damper, tine/tonebar, mounting, and pickup observations.
- Replaced, repaired, modified, corroded, or visibly aged parts; uncertainty where
  history is unavailable.
- Actual key indexing and physical-to-MIDI mapping, pitch/tuning reference,
  construction-zone transitions, and regulation settings.
- Pickup groups, winding polarities, continuity checks, control wiring, output
  reference plane, and cable/receiving-load specification.
- Ambient temperature and humidity, warm-up/settling procedure, inspection date,
  instrument configuration revision, and responsible operator.
- Photographs, schematics, recordings, and datasets with source rights and sharing
  permission. A public web page is not permission to redistribute its figures.

Freeze a configuration revision before an acquisition session. Moving a pickup,
replacing a tip, changing regulation, or repairing wiring creates a new revision
or explicit controlled-condition record. Photograph and measure enough reference
geometry to repeat the setup; retain scales, coordinate axes, and orientation.

## 2. Acquisition manifest

Each session and each record require an immutable identifier. Manifest schemas and synthetic validation fixtures belong to `FND-001`.
`DATA-001` requires an actual calibrated acquisition manifest and its data after
`SPEC-001`; this table is the semantic contract.

| Group | Required record |
|---|---|
| Identity | Specimen revision, session ID, record ID, operator, UTC timestamp |
| Physical condition | Key ID, tuning, regulation, hammer/damper/pedal state, pickup gap and orientation |
| Electrical condition | Connected nodes, controls, cable, known receiving load, probe loading |
| Acquisition | Device/model/firmware, channel mapping, sample rate, bit depth, ranges, clock/synchronization method |
| Calibration | Channel gain/polarity, physical units, calibration record/date, uncertainty, anti-alias response |
| Excitation | Native action or external fixture, measured strike kinematics/force, timing, repeated-strike interval |
| Environment | Temperature/humidity and any known mechanical disturbance |
| Data integrity | File format, byte size, SHA-256, storage locator, no undocumented edits |
| Processing | Raw-to-derived steps, software versions, filter settings, resampling, alignment, exclusions |
| Partition | `fit`, `validation`, or `test` label and complete held-out condition group; `development` is reserved for synthetic fixtures |
| Rights | Owner/source, permission or license, redistribution constraints |

Raw data remain immutable. Derived files reference their inputs and transformation
parameters. Never overwrite a failed or clipped take with a cleaner one under
the same ID. Mark quality flags and retain the exclusion rationale. Missing
calibration or unknown probe loading makes the affected claim unavailable; it
must not be silently imputed from a typical device specification.

## 3. Coordinate, timing, and level conventions

Define a right-handed mechanical coordinate system with labelled contact,
pickup, and support locations. Record displacement in metres, velocity in metres
per second, force in newtons, mass in kilograms, time in seconds, current in
amperes, and voltage in volts. State whether phase and sign refer to the pickup
winding, harp output, jack, or recorded channel.

Measure timing offset and drift between motion and electrical acquisition. If the
systems are not phase coherent, quantify that limitation instead of fitting the
unknown delay into magnetic dynamics. Document trigger delay separately from
musical onset. Maintain absolute calibrated voltage during fitting and evaluation;
level normalization is a separate listening-test treatment.

Choose bandwidth and sample rate from the fastest retained mode, contact event,
and expected nonlinear products. Record analogue anti-alias filtering and channel
phase response. Check sensor and probe mass/loading, fixture stiffness, noise
floor, clipping, electromagnetic contamination, and cross-talk. Characterize the
instrument with the measurement apparatus attached so that observation effects
can be bounded.

## 4. Measurement sequence

### A. Repeatability and regulation baseline

Repeat controlled native-action strikes under unchanged conditions. Measure
within-condition spread in hammer motion, fundamental frequency, decay, output
level, and onset timing. Repeat the session after re-establishing the setup to
estimate between-session variability. These distributions inform thresholds;
a single nominal recording cannot establish uncertainty.

### B. Mechanics and contact

Acquire hammer kinematics and synchronized tine/tonebar motion across strike
strengths. Observe separation and possible recontact. Include free decay,
native-action impact, and controlled excitation where feasible. Document damper
engagement, release, pedal positions, and repeated strikes during ongoing motion.
An external fixture may help identify a subsystem but is not automatically
representative of native action.

Separate bare/altered mechanical observations from fully assembled/loaded ones.
If pickup removal is impractical, record the retained magnetic influence in the
identification model. Do not identify modal losses with the magnet present and
then add the same magnetic damping as an independent effect.

### C. Magnetic geometry and electrical conversion

Record synchronized local motion and pickup voltage at controlled geometry and
loads. After every pickup movement, remeasure motion: the magnet can change the
mechanical response as well as sensitivity. Fit smooth linkage maps to physical
observations and separately constrain static magnetic forces where the reference
requires them. Compare candidate dimensionalities before adopting a tip-only map.

Reserve unseen positions and strike conditions for validation. A narrow map must
retain its validity range; extrapolation must raise a visible diagnostic or use an
explicitly evaluated fallback.

### D. Coil, wiring, and Stage controls

Measure continuity and winding polarity before selecting the circuit graph.
Acquire isolated and in-network complex impedance over a documented frequency
range, with calibrated stimulus and known loads. Account for measurement leads
and probes. Record resistance at known temperature and identify relevant
inductance and capacitance rather than treating DC resistance as a full impedance.

Measure control sweeps and tapers with the cable and destination load specified.
Use independent transfer, transient, and multitone measurements to test the
connected network. All pickups remain circuit members during evaluation, even
when their mechanical sources are stationary.

### E. Registers and later subsystems

After `VAL-001`, select representative keys around every observed construction
transition and at register extremes. Expand measurement density where sensitivity
or residuals require it. Do not assume one smooth interpolation reproduces every
hammer-tip or tine regime.

Powered preamp, modulation, supply, amplifier, driver, cabinet, and acoustic
measurements belong to later revision-specific tasks. They require the same
identity, calibration, uncertainty, and held-out discipline as the Stage target.
No evidence from one circuit family automatically validates another.

## 5. Condition matrix and data separation

The pilot campaign should cover the following factors. Numerical levels and
repetition counts are chosen after the repeatability and sensitivity pilot and
recorded before fitting; the table does not prescribe invented measurements.

| Factor | Development conditions | Independent validation condition |
|---|---|---|
| Strike | Several measured impact speeds and repeats | Entire unseen strike-strength groups |
| Retrigger | Multiple intervals and phases of existing motion | Held-out intervals/sequences |
| Release/pedal | Key releases, pedal-held release, controlled pedal changes | Complete unseen release/pedal trajectories |
| Pickup geometry | Several measured positions in safe operating range | Entire unseen position groups |
| Load | Known representative resistive/reactive conditions | Unseen loads within declared product scope |
| Session | Repeated baseline records | Later independently re-established session |
| Register | Pilot key, then construction-zone representatives | Held-out keys or clearly limited key-specific claims |

Split by **whole physical condition, acquisition session, or phrase**, not by
adjacent time samples or randomly chopped windows of one recording. Use training
data (`fit`) for parameter estimation and a separate `validation` partition for
predeclared model-selection decisions. Keep the final `test` partition locked
until the candidate and acceptance plan are frozen. Here “held-out validation” is
the scientific activity; each report must name the exact manifest split it used.
If final test data are used to tune the model, relabel them and acquire a fresh
holdout. Synthetic `development` fixtures never become measured acquisitions.

## 6. Identification strategy

Fit observable subsystems first: motion/contact, then magnetic conversion with
observed motion, then circuit parameters from electrical tests. Use the combined
output to test interaction rather than letting one unconstrained optimizer trade
hammer force against pickup gain and loading without explanation.

Track sensitivity, parameter correlations, confidence intervals or defensible
ranges, and alternative parameter sets that fit similarly. Prefer a simpler
identified model to unidentifiable complexity. A parameter reported by a
schematic is documentary; a parameter extracted by optimization is fitted.
Neither becomes a measurement merely because it improves audio similarity.

Every fit record stores objective terms and units, bounds, initialization,
optimization method/version, convergence status, excluded records, source hashes,
and residual summaries by condition. Failed fits remain visible. Any stochastic
method records a seed and repeatability procedure.

## 7. Releasable evidence bundle

A milestone evidence bundle contains the specimen/configuration passport,
acquisition manifest, rights information, immutable raw-file hashes, processing
and fitting scripts, model-asset hashes, a frozen condition split, uncertainty
analysis, numerical convergence report, and condition-level validation results.
Large data may live outside Git when durable authorized storage is available;
Git must retain the manifest and checksums needed to identify them unambiguously.
Do not commit private owner information, credentials, or redistributable-rights
assumptions with the scientific data.

`SPEC-001` exits when identity and configuration discrepancies are resolved or
explicitly scoped. `DATA-001` exits when an actual calibrated pilot acquisition and its manifest
are present, including immutable file hashes, a declared condition split,
repeatability/uncertainty, and rights. Schema validation against synthetic fixtures
is foundation work and cannot complete `DATA-001`. Neither task alone proves an
accurate instrument model. Hardware evidence is necessary for `MAG-001`,
`ELEC-001`, and the decisive `VAL-001` gate.
