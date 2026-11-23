# Validation and acceptance contract

The future coupled reference follows [the solver contract](specs/REFERENCE_SOLVERS.md), including explicit initialization, convergence and failure criteria.
The [acceptance-metric contract](specs/ACCEPTANCE_METRICS.md) details future
evaluators, uncertainty decisions and controlled listening prerequisites.

## Claims follow evidence

The current repository is a research foundation with synthetic development
parameters. Passing its software tests demonstrates those tested invariants. It
does not establish Rhodes realism, a measured frequency response, or reproduction
of a particular Mark I. A playable render is not a substitute for a physical
measurement, and a plausible timbre does not identify its component values.

The validation program follows sections 8, 10, and 12 of the
[user-supplied research](research/USER_RESEARCH.txt). Threshold values must be
recorded before the relevant acceptance experiment and justified by acquisition
uncertainty, instrument repeatability, and intended operating conditions. Until
those are known, use explicit pending criteria rather than invented fidelity
percentages.

## Evidence classes

| Class | What it can support | What it cannot support alone |
|---|---|---|
| Documentary | Candidate topology, component inventory, construction hypothesis | The actual selected specimen's values or behavior |
| Synthetic/analytic | Algebra, dimensions, boundary cases, solver invariants, software regression | Hardware fidelity or measured component identification |
| Reference numerical | Error of a reduced/runtime model against a specified converged reference | Correctness of the reference's physical assumptions |
| Measured subsystem | Calibrated response of the identified component/assembly under recorded conditions | Unobserved conditions, other specimens, or all possible parameter explanations |
| Held-out integrated measurement | Predictive behavior within the tested target and uncertainty | Universal identity or untested revisions |
| Controlled listening | Detection/discrimination under the stated protocol and statistical power | Exact circuit equivalence or proof of inaudibility everywhere |
| Performance | Observed timing and resources for the specified build/hardware/workload | A universal CPU percentage or every host deadline |

An evidence record must identify its class, source commit, model and input hashes,
command/procedure, environment, actual outcome, limitations, and associated task.
Store failures and inconclusive results as such. A command string without a
result is not a passed test. The project state tool checks record structure and
completion rules; it does not independently certify a human's scientific claims.

## Test pyramid and concrete obligations

### Analytic and primitive behavior

For each supported modal oscillator, compare displacement and velocity with the
closed-form free response at multiple times and sample rates. Verify signs,
units, zero-input rest, passive decay, and a lossless limiting case if that case
is supported. State numerical error bounds and excluded parameter regimes.
Tests for invalid/nonfinite parameters must ensure a defined outcome before state
or output is corrupted.

For retrigger, compare a continued nonzero state with the same state plus a new
impulse or contact event. The old state must remain present. For note-off and
pedal, inspect state continuity and event ordering; the initial switched-damping
surrogate must remain labelled as such.

For the pickup, compare the analytic linkage gradient with a numerical derivative
within a justified finite-difference range. Confirm the turns convention and
polarity. Demonstrate cross terms when multiple modes are combined before the
nonlinear map. These checks establish the implementation of a synthetic law,
not the accuracy of that law for a real pickup.

For the network, test equal and unequal-source analytic cases where supported,
zero sources, finite loads, limiting loads, and the contribution of silent
branches. A resistive fixture cannot pass an inductive, capacitive, or connected
Stage-control claim.

### Contact, coupled physics, and solver behavior

The future finite-mass hammer test suite must cover approach, compression,
separation, recontact, already-moving tines, and action relaunch. Momentum and
energy changes must agree with applied forces and dissipation. Damper tests need
onset, sustained contact, release, pedal trajectories, and repeated engagement.
Numerical tolerance must not conceal an attractive contact force or energy gain.

For the coupled reference, record operating-point residuals and per-step solver
residuals. Account for stored energy, dissipated energy, and external work across
mechanical and electrical ports. Zero-current permanent-magnet forces and
current-dependent reaction require separate checks. Resolve whether measured
modal parameters already contain magnetic effects before integrating them.

At each representative condition, refine timestep, nonlinear-solver tolerances,
modal order, and field/circuit reductions independently enough to isolate error.
Report tuning, phase, decay, onset, level, harmonic/intermodulation, and energy
changes. A bounded output is necessary but not sufficient.

### Event and host semantics

Replay a timestamped event schedule with multiple buffer partitions, including
events at the first/last sample, coincident events, repeated strikes, and pedal
changes. Define the expected tie-breaking order. Compare output and state within
justified tolerances. Check sample-rate preparation, reset, invalid configuration,
asset replacement, and future serialized-state migration explicitly.

Do not assume a per-sample engine has a host event contract before an adapter
implements and tests it. CI should describe the interface actually covered.

### Aliasing and reduction

Compare against a sufficiently converged higher-rate/reference output, including
hard strikes, high notes, multiple modes, chords, and later nonlinear circuits.
Quantify audible-band residuals and relevant spectral lines after aligned
resampling. Record the filter, latency, and alignment method. Increasing rate
until one waveform looks smooth does not establish a bound. The current
polynomial demonstration has no completed antialiasing claim.

Any simplified geometry, omitted feedback, shared-structure reduction, dormant
voice rule, or lower-rate thermal state needs its own declared domain and error
budget. Keep reduction error separate from measurement/model mismatch.

## First decisive gate: VAL-001

A candidate may pass the single-note prediction gate only when all of the
following are recorded and reviewed:

1. An identified specimen/configuration, key, output boundary, and known load.
2. Calibrated, synchronized data with uncertainty and repeatability estimates.
3. Traceable mechanical, magnetic, and electrical parameters with their provenance
   and validity domains; unknowns and approximations remain visible.
4. A numerical reference that passes analytic limits, energy accounting, and
   convergence checks without unexplained residuals.
5. Frozen `fit`, `validation`, and final `test` partitions by complete physical
   conditions; synthetic `development` records remain separate.
6. Predictions for held-out strike strengths, releases, retriggers, pickup
   positions, and loads within the declared scope.
7. Condition-level physical and electrical metrics with thresholds agreed before
   examining final validation results, plus raw/derived evidence references.
8. An error and failure analysis describing where the model does not predict
   sufficiently well and which claims are therefore withheld.

A failed condition cannot be averaged away by a good score elsewhere. If the
scope is deliberately narrowed, update the target declaration and retained
limitations visibly. Register expansion may proceed as exploratory work, but
cannot be claimed as validated expansion before this gate.

## Metrics to report

| Domain | Primary observations | Reporting requirement |
|---|---|---|
| Mechanics | Modal frequency, damping/decay, displacement/velocity, contact timing | Physical units, uncertainty, per-condition residuals |
| Magnetic conversion | Voltage given observed motion, geometry sensitivity, force where modeled | Calibration, map domain, derivative/force consistency |
| Electrical | Absolute level, complex transfer, impedance, harmonics, intermodulation | Load and reference plane, instrument floor, spectra and phase |
| Integrated note | Attack/release trajectory, retrigger response, pedal behavior, decay | Separate onset alignment, absolute gain, and timbral comparisons |
| Numerical | Step/order convergence, residuals, energy drift, block invariance | Solver settings, baselines, excluded unstable/invalid cases |
| Runtime | Maximum observed callback duration, deadline misses, sustained memory/resources | Exact hardware/build/workload and sample/block configuration |

Useful diagnostic plots include aligned calibrated waveforms, residual spectra,
modal decay curves, complex impedance, contact states, and cumulative energy
balance. Every plot needs a unit-labelled axis and a traceable source record.
Avoid a single “realism score” that blends unrelated failure mechanisms.

## Listening evaluation

After physical and electrical gates, use randomized controlled listening with
held-out phrases, register/velocity coverage, known monitoring, and a declared
comparison design. Preserve three separate questions: absolute level fidelity,
level-matched timbre, and onset/timing fidelity. Record participants, trial
allocation, exclusions, statistical method, confidence intervals, and power or
sensitivity limitations. A failure to detect a difference supports only that
experiment's conditions; it does not prove universal indistinguishability.

The brief cites ITU-R BS.1116 as a methodological lead. Adopting or claiming
conformance to a standard requires consulting its applicable text and recording
which requirements were implemented. This foundation makes no such conformance
claim.

## Real-time and portability gate

Measure a representative computational kernel early, while reporting its reduced
scope. Later stress the full supported key range, dense chords, fast retriggers,
pedal transitions, control changes, and long-running decay at supported sample
rates and buffer sizes. Report maxima and deadline misses, not only average CPU.
Include compiler flags, OS, processor, power policy, audio host, thermal state,
and run duration. Allocation/lock checks and sanitizer runs are useful software
evidence but do not themselves establish audio deadline behavior.

Portability CI must build and test on each claimed platform. A Linux local pass
cannot be described as Windows/macOS validation. A configured matrix is planned
coverage until a run succeeds. A plug-in release additionally needs actual format,
installation, host lifecycle, state restoration, and latency tests; this
repository's initial offline executable is not that release.

## Evidence updates and regression policy

Every behavior-changing task updates its evidence references, limitations, and
next task through the [agent workflow](agents/WORKFLOW.md). Deterministic generated
status files should be refreshed from canonical state and checked for drift.
Baseline changes require an explanation of the physical or numerical reason;
do not overwrite a regression threshold merely to make CI pass. Retain enough
input data and commands for a new agent to reproduce a result independently.

If a test environment or measurement is unavailable, record the check as unrun or
blocked and name the missing resource. The repository should remain buildable
and useful while its scientific limitations remain explicit.
