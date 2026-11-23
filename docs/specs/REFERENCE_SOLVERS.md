# Reference solvers: initialization, convergence and failure

**Status: requirements for future coupled solvers.** The current linear modal
update is analytic; the coupled hammer, magnetic and circuit solvers do not yet
exist. This specification consolidates [A026](../agents/reviews/A026.md),
[A027](../agents/reviews/A027.md), [A028](../agents/reviews/A028.md) and
[A029](../agents/reviews/A029.md). Proposed fixtures are not reported results.

## Equilibrium before a first strike

Solve the assembled mechanical and electrical operating point with the selected
gravity, support preload, damper condition, magnetic attraction, supply/load and
control settings. Record generalized forces, contact complementarity, KCL/KVL,
charge and flux consistency. Coordinate zero must refer to this assembled point,
not an arbitrary unassembled geometry with a silent output.

An initialization record includes model/configuration hashes, reference frames,
boundary conditions, all initial mechanical and electrical states, solver/precision,
scaled residuals, tolerances and convergence outcome. Verify positive and negative
small perturbations around a stable synthetic equilibrium with no unexplained
energy injection or startup pulse. Hard reset, host restart and live parameter
migration have separate semantics. A failed initialization is a failed setup,
not a state that may be silently accepted by the audio callback.

## Three separate refinements

A small nonlinear residual means that the discrete equations were solved. It
does not establish that the timestep accurately approximates the continuous
system. A passive, bounded solution may still drift in pitch, shorten decay,
change rebound velocity or miss a short contact.

| Refinement | Hold fixed | Record |
|---|---|---|
| `h,h/2,h/4,h/8` | Tight solver/event tolerances | Physical-time trajectory, event time, impulse, rebound and energy differences |
| `tau,tau/10,tau/100` | Finest timestep and event tolerance | Scaled residual, iteration count, failures and observable changes |
| `eta,eta/2,eta/4` | Finest timestep and solve tolerance | Event counts/order, bracket accuracy, onset and separation times |
| Independent method | Same model and physical observation times | Independent implementation discrepancy and analytic free-motion limit |

Predeclare per-observable absolute/relative limits in the appropriate SI units,
positive reference scales, and a separate energy scale. Retain absolute errors
near zero. Do not shift the waveforms to hide onset or phase error.

A proposed acceptance procedure requires two successive timestep refinements
below budget with decreasing differences, plus two solver/event tightenings that
change observables by less than one tenth of their budget. Independent-method,
event-sequence and energy comparisons must also pass. Report nonsmooth order loss.
Plateaus, ambiguous grazing and exhausted budgets are inconclusive. The procedure
must be frozen with fixture-specific values before claiming convergence.

## Contact events and singular boundaries

Track geometric separation (`delta=0`) independently from a force-clamp boundary
at positive compression (`k+c*delta_dot=0`). Zero force does not imply a gap.
Never evaluate a fractional compression power on a negative gap.

For `0<p<1`, the contact derivative can diverge at onset. Either explicitly bound
the supported exponent range or validate an energy-compatible regularization and
its limiting error. A Newton Jacobian cannot simply be assumed finite there.

The required grazing fixture places a synthetic stationary hammer tangent to an
analytic free-motion turning point. Signed clearance perturbations produce a
miss or a short contact. Endpoint sign-change detection can miss a tangent root
or two roots within one step. Check classification, event ordering, chatter,
nonnegative force, state continuity and refined impulse/rebound/energy error.

Freeze tie-breaking for relaunch immediately before, at and after separation or
clamping, including opposite tine velocities. Track actual action input work.
Set minimum time progress, subdivision and iteration caps. On exhaustion, restore
the complete last accepted state and energy ledger before any bounded retry, or
return explicit failure. Never accept an unconverged force as a successful step.

## Nonlinear bandwidth and resampling

Exact linear propagation and a mechanical Nyquist bound do not remove nonlinear
aliasing. At the current synthetic lowest accepted rate, the top retained mode
is below the mechanical bound while cubic pickup products extend above it.
The present core has no antialias filter and makes no alias-free claim.

Choose a consistently coupled internal rate; do not oversample only one block
while leaving an important feedback port at an incompatible rate. Record filter
passband/stopband, delay and numerical error. Use successive reference-rate
doublings and independent filter refinement on hard attacks, high notes, chords,
retriggers and clipping where applicable. The offline reference may need rates
above the current runtime API's 192 kHz ceiling; do not treat that ceiling as proof
that the reference is converged. ADAA is an option only for a suitable mapping,
not a general replacement for dynamic coupled integration.

Report attack and decay errors separately, including RMS and spectral-line
residuals, common timing and rate/filter configuration. A universal oversampling
factor is not an acceptance criterion. Freeze an operating-domain error budget
and report unsupported conditions.

## Evidence package

Use one energy ledger across physical subsystems. Between events, compute
instantaneous power residual `dE/dt + P_loss - P_external`, interval residual
`r_n = E_(n+1)-E_n + D_n-W_n`, and cumulative `R_N=sum(r_n)`. Report both
`max(abs(R_n))` and `sum(abs(r_n))` so cancellation cannot hide excursions.
Insert separate event rows for impulse work `J*v_before+J^2/(2*m)`, relaunch,
pedal/linkage work and changes to stored-energy parameters. A purely resistive
change does not itself add a storage term. Numerical damping remains residual,
not a physical-loss credit. Recheck coordinate invariance after any reduction.
These requirements consolidate [A030](../agents/reviews/A030.md).

Retain the exact fixture/configuration hashes, solver implementation, precision,
build flags, commands, raw sweep results, independent reference, event traces,
worst residuals, cap counts and limitations. Numerical evidence supports the
declared synthetic/reference model. Matching a physical specimen still requires
the separate acquisition and held-out prediction gates.
