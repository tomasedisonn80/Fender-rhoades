# PHY-002: finite-mass contact reference contract

**Status: specification for the next implementation.** The current engine still
uses synthetic impulses and switched viscous loss. This document consolidates
setup reviews [A011](../agents/reviews/A011.md),
[A012](../agents/reviews/A012.md), and [A013](../agents/reviews/A013.md).
It does not establish a measured hammer or damper law.

## First bounded deliverable

Implement an offline, synthetic one-mode resonator plus a finite-mass hammer.
Expose displacement, velocity, contact force, contact mode, stored energy,
dissipated energy, input work and residual at each accepted time step. Retain
the existing analytic free-motion solution as an independent limit. Compare
candidate contact integrators before choosing a bounded runtime approximation.
Add physical damper contact after the hammer fixture establishes the energy and
event conventions. Do not change instrument fidelity status for this task.

## Coordinates and reciprocal work

Choose one positive contact normal. For hammer position `x_h`, rest clearance
`g0`, modal coordinates `q`, and hammer participation vector `b_h`, define

$$
\delta=x_h-b_h^Tq-g_0,\qquad
\dot\delta=v_h-b_h^T\dot q.
$$

Compression is positive. The hammer receives `-F`; the modal generalized force
is `b_h F`. Use the same `b_h` in contact observation and force application.
This preserves work reciprocity even though generalized modal forces are not
individual free-body forces. Mount/support reactions remain part of the energy
and momentum interpretation.

Reconstruct physical motion as `u(r)=sum(phi_j(r)*q_j)`. With dimensionless shape
functions, `q` is in metres and modal mass is in kilograms. Record mode sign,
normalization point/direction, hammer and damper projections, pickup observation,
support observation and measured boundary conditions. Under `q'_j=s_j q_j`,
transform shape/participation by `1/s_j` and modal mass by `1/s_j^2`; physical
motion, energy, force and pickup output must remain invariant.

Each identified modal asset also references a mechanical-boundary record: tuning
spring location, screw settings, grommet compression/preload, support constraints,
equilibrium and pickup/load condition. Mark which support stiffness, loss and
resonances are already embedded in the modes before adding explicit support
states. Validate spatial force-to-motion transfer at tine, tonebar and support
observations; electrical spectra alone cannot identify structural poles.

## Candidate constitutive law

The research suggests a unilateral candidate, to be identified later:

$$
F=0\quad(\delta\le0),\qquad
F=\max(0,k\delta^p+c\delta^p\dot\delta)\quad(\delta>0).
$$

Declare `m_h>0`, `k>0`, `c>=0`, `p>0`, their finite operating ranges and units:
`m_h` kg, `k` N/m^p, `c` N·s/m^(p+1). Specify whether a zero clamped force at
positive compression changes contact state, how grazing is handled, and how a
new action launch interacts with an ongoing contact. Do not reset resonator
displacement or velocity on any of those events.

For compressed elastic storage `U=k*delta^(p+1)/(p+1)`, the unclipped branch
dissipates `c*delta^p*delta_dot^2`. The clipped branch has a different balance:
`d(E_mechanical+U)/dt = k*delta^p*delta_dot <= 0` in the otherwise conservative
fixture. The integrator must account for this loss separately. A solver that
uses the unclipped dissipation expression after clamping can report a false
energy error or conceal actual numerical injection.

## Launch and repeated strikes

Keep action launch velocity separate from relative impact velocity. A measured
MIDI mapping will later connect key motion to native hammer motion. In the
synthetic reference, explicitly record the energy added or removed by launch,
relaunch and any constrained action motion. Opposite-phase strikes must interact
with the current tine state. Reset is an explicit diagnostic operation only.

## Damper and continuous pedal

The physical extension needs a continuous pedal/linkage coordinate with per-key
clearances and key-lift geometry. Define the signed gap, normal force, contact
storage and dissipation just as explicitly as the hammer. Finite-compliance
engagement preserves mechanical displacement/velocity; separation has zero
attractive force; partial pedal travel can create separation and recontact.

For a fixed linkage and no excitation, total modeled stored energy must not
grow beyond numerical tolerance. For a moving prescribed linkage, include the
external actuator work through its signed effort/flow port. Tine energy alone
is not a passivity test. A continuously variable decay coefficient does not
identify physical half-pedal behavior.

## Required tests and recorded limits

| Fixture | Independent observation |
|---|---|
| Separated surfaces | Exactly zero contact force and free-motion agreement |
| Conservative two-free-mass contact | Momentum and total-energy balance |
| Supported resonator plus hammer | Contact work reciprocity and support-force accounting |
| Damped impact including clamp | Correct branch-specific nonnegative loss |
| Grazing and separation/recontact | Event ordering, no attractive contact or state reset |
| Opposite-phase retriggers | Preserved state, distinct physically explained responses |
| Rescaled two-mode coordinates `(2,-3)` | Identical physical trajectories, energy and voltage |
| Damper engagement/partial travel | Continuous mechanical state and actuator-work balance |
| Step refinement `h`, `h/2`, `h/4` | Converged impulse, rebound, event time and waveform |

Before coding the acceptance tests, commit their synthetic fixture constants,
supported domain, absolute/relative tolerances and the reason for those limits.
Use a stated energy scale so near-zero residual normalization is meaningful.
Record cumulative residual, force residual, impact timing, integrated impulse
and rebound error; bounded output alone is insufficient. A failed fixture must
not be made green by silently widening a tolerance.

The detailed [reference-solver contract](REFERENCE_SOLVERS.md) specifies equilibrium, independent timestep/solver/event refinement, grazing fixtures and bounded failure recovery.

## What must be measured later

Acquire synchronized native hammer motion, tine/damper motion, pedal/linkage
travel, contact timing and loaded voltage. Reserve whole speeds, initial phases,
travel directions and pedal trajectories for validation; measure uncertainty
and repeatability. Physical acquisition remains in SPEC-001/DATA-001. This
reference can satisfy PHY-002's numerical scope using clearly labeled synthetic
parameters while those measured tasks remain blocked.
