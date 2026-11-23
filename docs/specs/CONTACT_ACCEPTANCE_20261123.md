# Frozen contact-reference acceptance — 23 November 2026

The integrator adopts the synthetic fixtures, analytic values, error budgets,
energy scales and refinement procedure in
[W1-A005](../agents/development-20261123/W1-A005.md), with the damper work and
equilibrium derivation in [W1-A007](../agents/development-20261123/W1-A007.md).
This document is committed before contact implementation or acceptance tests.
These are numerical engineering requirements, not measured Rhodes parameters.

## Selected implementation scope

Use a separate offline reference with two diagonal modes, a finite-mass hammer,
and one prescribed compliant damper face. Support zero modal frequency for the
free-mass oracle. The first constitutive scope is **exponent p = 1** only:
`F = max(0, delta * (k + c * delta_dot))` at positive compression. No negative
compression power, attractive force or hidden mechanical reset is permitted.
The adaptive explicit reference remains outside the real-time engine and VST3.

Use adaptive RK4 with independent step-doubling error estimation and explicit
bounded failure. Integrate physical loss, signed actuator work and impulse as
part of the trajectory, never infer physical dissipation from an energy residual.
An independent integration method and the analytic limits are required; RK4
comparing with another call to itself is only a refinement check.

The hammer may be relaunched by changing its velocity while retaining every
position and modal velocity. Record its exact kinetic work. Position jumps are
not an ordinary note event. Prescribed damper position and velocity must come
from the same continuous trajectory. A fixed preloaded damper is initialized
from the assembled equilibrium specified in A005; initial numerical coordinates
remain the unshifted coordinates used in that fixture, avoiding an omitted
preload term.

## Acceptance remains criterion based

All A005 fixture families are required for full PHY-002 acceptance: conservative
and damped impact, forced clamp, separated and supported modes, coordinate
rescaling, velocity-only relaunch, event order, fixed preload, moving damper,
signed actuator work and grazing. Its optional exponent-1.5 extension is outside
the selected domain and must not be advertised as tested.

Retain the four maximum internal steps `20, 10, 5, 2.5 microseconds`, event
bracket tolerances `10, 5, 2.5 nanoseconds`, and the separate tight grazing
budget. An explicit solver has no nonlinear solve tolerance: record that axis
as inapplicable and refine its local integration tolerance separately. Report
event times/brackets, instantaneous constitutive and power residuals, interval
and cumulative energy residuals, impulse, rebound and physical-time waveform
differences. Do not silently widen thresholds after observing failures.

Every public call has finite input/domain guards, a finite subdivision/evaluation
budget and a minimum progress rule. A failed call restores its entire prior
state, energy ledger and event diagnostics. Success means the requested interval
was completed; partial advancement cannot be returned as success. Oversized,
nonfinite or unresolvable inputs return a typed failure. Record the concrete
limits with the implementation and test their exhaustion.

Two-successive-step refinements, independent-method agreement and event
classification remain separate gates. A balanced energy ledger alone does not
establish accurate contact timing. Unresolved tangency, a missed short contact,
unsupported trajectories or failed refinement remain explicit incomplete
acceptance conditions. Neither a runnable reference nor a VST3 adapter changes
the project's `unvalidated` fidelity status.
