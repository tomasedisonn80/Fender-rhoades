# Measured prediction and acceptance metrics

**Status:** future evaluator contract, consolidating A061–A066. It specifies
requirements for `VAL-001` and later expansion; no physical dataset, measured
threshold, perceptual study or implemented evaluator is claimed. Use
[VALIDATION](../VALIDATION.md) for the overall gate and
[DATA_INGESTION](DATA_INGESTION.md) for admissible observations.

## 1. Freeze the evaluated question

Every report names specimen/configuration, output reference plane, key, load,
condition/session IDs, input/model/evaluator hashes, exact partitions and independent
take counts. Declare whether the estimand is a single-strike prediction, a condition
mean, or another explicitly defined quantity. Record exclusions, censored records,
coverage and failures. More windows from one take do not create independent data.

Freeze required cells, metrics, thresholds, alignment, exclusions, uncertainty
propagation, numerical budgets and multiplicity policy before final-test access.
Keep the model and evaluator immutable during the final comparison. A failed
required cell blocks its claim; narrowing the supported scope requires an explicit
new declaration and retained limitations.

## 2. Absolute voltage, polarity and time

Compare calibrated measured volts with the engine's corresponding modeled volts
at the same electrical boundary. A diagnostic render can be converted only if its
nonzero monitoring gain is recorded and divided out. The current renderer reports
gain to stdout and does not embed that provenance in its WAV header; preserve the
command/output separately. Zero-gain or unknown-gain files cannot recover absolute
model output. Synthetic voltage units are not physical calibration.

Use one physical timeline. Permit independently measured acquisition offset/drift
and declared filter-latency corrections with uncertainty, frozen before evaluation.
Preserve original traces and all transformations. Do not fit per-note shifts,
polarity inversions, phase rotations or time warps to held-out output. Report onset
error separately from known transport delay.

Report signed waveform residuals, voltage RMSE, peak/RMS differences, onset/release
timing and event-relative attack/sustain/retrigger/release windows. Specify window
boundaries, bandwidth and noise floors. Separate level-matched timbre copies may
use one documented positive gain per comparison over a frozen window, retaining
the complete timeline and transients. Envelope matching or trimming attacks would
remove behavior being evaluated and is not permitted for acceptance.

Proposed evaluator regressions inject known gain, polarity, delay and transient
errors into an otherwise identical synthetic signal. Each must remain visible in
its relevant primary metric even if a separate timbre comparison succeeds.

## 3. Modal frequency and decay

Specify mode matching, event-relative windows, taper/normalization, estimator
version, noise estimation and censoring before final use. Jointly estimate
overlapping modes; beating minima are not new damping. Mark unresolved modes
explicitly. Segment attacks, repeated strikes and damping changes instead of
forcing one stationary exponential through distinct regimes. Do not fit logarithms
below the estimated noise floor or extrapolate censored tails without declaration.

For positive frequencies under the same convention, report signed Hz error and
`1200*log2(f_prediction/f_reference)` cents. Distinguish observed damped frequency
from inferred undamped natural frequency and record conversion assumptions.
For a modal amplitude envelope `exp(-beta*t)`, report `beta` in s^-1 and
`T60=ln(1000)/beta`; the zero-loss limit is infinite. Squared-amplitude envelopes
decay at `2*beta`; instantaneous mechanical energy need not be a pure exponential.
Use 20-log amplitude and 10-log energy ratios consistently.

A proposed synthetic reference combines known 440/442 Hz damped modes with
different loss coefficients, then adds separately seeded noise and a chirp variant.
Freeze generator, hashes and numerical recovery tolerances. The test must expose
overlap, nonstationarity and late-tail censoring; these development frequencies
and losses do not describe a measured Rhodes.

## 4. Mechanical and electrical event landmarks

Record action trigger, hammer launch, geometric contact, force onset/clamp,
separation, damper engagement and voltage onset/peak separately. Freeze electrical
detector bandwidth, threshold, hysteresis and windows; voltage threshold crossing
is not a substitute for observed mechanical contact.

For retriggers, retain pre-event history and match modal displacement/velocity,
hammer state/contact branch, key/action state and pedal trajectory. Distinguish
launch speed from relative impact speed. A repeat strike need not produce a new
voltage onset; evaluate the event-relative trajectory and residual without phase
reset or attack normalization.

Record event brackets, missing/extra events, grazing ambiguity and observation
limits. Events beyond the capture interval are censored, not zero-error successes.
Proposed fixtures cover opposite-phase retriggers, launch around contact separation,
partial pedal travel/reversal, coincident release/pedal changes, grazing contacts
and truncated captures. Refine step, solver and event-localization tolerances
independently before using the reference to judge the runtime.

## 5. Identification and uncertainty

Freeze modal coordinate scale/sign, masses and contact/pickup participation.
Equivalent coordinate rescalings must preserve physical motion, energy and voltage;
they are not distinct identified solutions. Constrain mechanics, conversion and
electrical loading with independent subsystem observations so one unconstrained
fit cannot exchange hammer force for pickup gain or load without explanation.

Store parameter transforms, nonzero physical scales, residual units, objective
weights, covariance/whitening, derivative checks, singular values, rank threshold
and weak combinations. Use multistart alternatives and defensible profile or
condition/session resampling intervals. Optimizer convergence or local rank does
not establish unique physical identification. Ambiguous parameter values remain
unidentified even when predictive error meets its declared threshold.

Record within-session and independently re-established between-session variability.
Track temperature, warm-up, tuning/regulation, operator, repositioning, apparatus
loading and drift. Shared calibration bias is not averaged away by repeated takes.
Unknown correlation requires a defensible bound or an inconclusive result.

Separate acquisition uncertainty, fitted-parameter uncertainty, repeatability,
reference discretization and runtime reduction. Allocate them without double
counting; do not inflate uncertainty using the model discrepancy being tested.
Numerical-budget failures cannot be excused by measurement noise.

For a nonnegative discrepancy with estimate `e` and a declared-coverage interval
`[L,H]`, one proposed conservative rule is:

| Condition | Result |
|---|---|
| `H <= T` | Pass the predeclared threshold `T` |
| `L > T` | Fail |
| Otherwise | Inconclusive; interval overlaps the acceptance boundary |
| Missing required cell or invalid calibration | Incomplete gate |

The interval construction, conservative numerical bounds and threshold must be
justified and frozen for the actual experiment. The acceptance guard band is
`H-e`; it is not permission to enlarge `T` after seeing results. Report each
required cell and its reason, unresolved inputs and next action.

## 6. Controlled listening

Pair specimen excerpts and frozen model predictions for the same recorded input
events, configuration and load. Keep repeated physical takes distinct. Blind
participants and administering experimenters, hold decoding separately, and
randomize/counterbalance order and allocation. Record seeds, training, monitoring,
breaks, exclusions and stimulus hashes.

Absolute-level trials use common playback gain. Matched-timbre trials use a frozen
matching rule with adjustments retained. Timing trials preserve the discrepancy
under investigation. One endpoint cannot substitute for another.

Before confirmatory recruitment or final-outcome access, archive the population,
paired-stimulus manifest, endpoints/contrasts, stopping rule, exclusions,
missing-data handling, analysis model and pilot-informed power/sensitivity plan.
Account for participant, stimulus/phrase and session clustering; report independent
listener and stimulus counts alongside trial counts. Predeclare multiplicity
handling across conditions and endpoints.

Freeze equivalence margins and interval coverage with justification. Nonsignificance
alone does not establish equivalence; use the declared interval/margin or
discrimination-bound rule. Conclusions apply to the tested population, stimuli,
monitoring and conditions. This foundation makes no standards-conformance or
universal indistinguishability claim.
