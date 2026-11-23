# MAG-001 / ELEC-001: transduction and network contracts

**Status: future reference specification.** Current code has a one-way synthetic
polynomial pickup and an equal-resistance candidate harp. This document captures
reviews [A016](../agents/reviews/A016.md), [A017](../agents/reviews/A017.md),
[A018](../agents/reviews/A018.md), and [A019](../agents/reviews/A019.md).
No field map or specimen circuit has been identified.

## Sign and reference-plane record

Every magnetic/circuit asset must name positive mechanical coordinates, coil
terminals A/B, winding orientation, positive current, magnet orientation, and
output reference plane. Distinguish open-circuit coil EMF, loaded harp voltage,
and Stage output-jack voltage. The current numerical convention
`e = -grad(lambda) dot velocity` does not by itself establish real lead polarity.
Record how this source orientation maps to a passive terminal equation.

Flux linkage has units V·s (weber-turns); the turns are already included. A
per-turn flux asset needs an explicit conversion once. Constant linkage does not
change motional EMF and cannot identify zero-current magnetic attraction.

An independent synthetic polarity fixture uses 200 turns and per-turn gradient
0.001 Wb/m, giving linkage gradient 0.2 V·s/m. At velocity 0.02 m/s, the chosen
negative-derivative source is -0.004 V. Reversing motion or the selected leads
reverses the sign; adding constant linkage has no effect. This is a proposed
numerical fixture, not a measurement.

## Reciprocal reference energy

For one reversible candidate, use passive signs `v=V_A-V_B`, current entering A,
and force along positive `q`. A compatible co-energy model is

\[
W'(q,i)=-U_0(q)+i\lambda_0(q)+\tfrac12 L(q)i^2.
\]

Then `lambda=partial W'/partial i`, `F=partial W'/partial q`, and
`v=R*i+d(lambda)/dt`. The magnetic stored energy is
`E_mag=i*lambda-W'=U0+L*i^2/2`. The negative motional source convention above
requires the corresponding opposite source orientation relative to this passive
open-circuit terminal voltage. Freeze that mapping rather than changing signs
independently inside mechanics and circuit code.

Voltage/motion identifies linkage derivatives. It does not independently
identify `U0` or static force. Measure static-force/equilibrium behavior and
record magnetic stiffness, damping and preload already embedded in modal data.
Do not add those contributions again. Nonreversible hysteretic behavior, if
required by evidence, needs additional states and an explicit loss model.

A synthetic mass–spring–damper with a coil terminated by `R_L` must satisfy
`v=-R_L*i`. With no external excitation, define

\[
\rho(t)=E(t)-E(0)+\int_0^t[(R+R_L)i^2+c\dot q^2]\,d\tau.
\]

Check the residual under step refinement and independently verify
`partial F/partial i = partial lambda/partial q`. Record a nonzero perturbation
energy scale and absolute/relative tolerance before running the fixture.
Mechanical potential excludes magnetic effects modeled explicitly. The current
one-way pickup is not expected to pass this coupled-system conservation test.

## Smooth map assets

Store asset version/hash, specimen/configuration, source-data hashes, coordinate
frames, axis units/normalization, orientation parameterization, validity cells,
excluded regions, current/temperature conditions, uncertainty and reduction error.
A bounding box alone does not establish support inside unmeasured holes.

Derive gradients from the same interpolant as linkage. Require continuous first
derivatives across cells, and the appropriate higher continuity for solver
Jacobians. Include coordinate-transform Jacobians. Do not independently fit a
gradient table and assume it remains compatible with the stored scalar field.

Validate assets at preparation. An out-of-domain evaluation returns a bounded
diagnostic status; any fallback must be named and separately evaluated. Runtime
clamping or extrapolation must not silently create a new purported physical law.
Use a manufactured smooth map with known derivatives to test seams, rotations,
unit conversion, reciprocity, domain boundaries and refinement before measured
maps are compiled. Review A018 supplies one candidate fixture.

## Connected pickup graph

An identified graph records every pickup ID, physical-key mapping, terminal nodes,
group membership, series orientation, source polarity and connected/fault status.
Include actual continuity/photo evidence, circuit revision, impedance observations,
frequency/temperature range, load and cable. Verify complete, nonduplicated
membership of all 73 pickups. Mechanical silence does not remove a branch.

For a linear parallel group,

\[
Z_g=(\sum_i1/Z_i)^{-1},\qquad E_g=Z_g\sum_i e_i/Z_i.
\]

The current equal-resistance reduction cannot distinguish permutations among
three-pickup groups. Matching its output therefore cannot verify physical group
membership. Terminal open circuit also does not guarantee zero circulating
current between parallel branches with unequal source voltages.

An asymmetric reference fixture uses sources `(3,-3,0)` V, branch resistances
`(100,200,300)` ohms and load `600/11` ohms. Correct loaded voltage is `9/22` V.
Dropping the stationary branch gives the wrong `9/20` V; reversing the second
source gives `27/22` V. Preserve this fixture when implementing unequal and
complex impedances. A documented DC resistance does not identify inductance,
winding capacitance, frequency-dependent losses or load response.

## Promotion gate

MAG-001 and ELEC-001 require both numerical consistency and identified specimen
data. Synthetic fixtures may develop the algorithms while SPEC-001/DATA-001
remain blocked, but they cannot complete those measurement-dependent tasks.
Freeze full connectivity and parameter provenance before integrated one-note
validation; retain conditions not used in fitting and state the output boundary.
