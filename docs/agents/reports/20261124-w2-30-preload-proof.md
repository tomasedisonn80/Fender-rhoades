# Wave 2 assignment 30 — rounded-preload proof review

Date: 2026-11-24. Parent: `/root/w2_contact_implementation`.

## Outcome and boundary

No concrete source defect was found in the rounded-preload enclosure for the
frozen `fixtures()["preload_equilibrium"]` definition. This is a source-level
mathematical review with one limited arithmetic probe, whose failure is recorded
below. It is not a newly executed verification of all twelve rational checks,
a solver test, a measured instrument result, or general extrema certification.
**PHY-002 remains incomplete.**

Read `AGENTS.md`, `README.md`, `docs/PROJECT_STATE.md`, `docs/ROADMAP.md`, and the
`PHY-002` entry in `project/state.json` (revision 27, task `in_progress`). Reviewed
the analytic slice contract, normative A54, the independent fixture and physical
law, `_preload`, `_outward`, and the existing preload test. This was an assigned
bounded review; no second task was selected or claimed. Only this report was
written. No Git, canonical state, solver, or old-auditor edits were made.

## Logical coverage reviewed

1. **Exact input interpretation and stiffness.** `_preload` converts the actual
   binary64 initial coordinates and evaluated modal stiffnesses to `Fraction`.
   It therefore retains the rounding in `fixtures()` rather than replacing the
   initial state with a rational equilibrium. With positive diagonal `K` and
   `kd > 0`, `A = K + kd*b*b^T` is positive definite. The implementation also
   checks positive `a00` and determinant before using the exact two-by-two
   inverse. The supported fixture has exactly two modal coordinates.

2. **Equilibrium and initial energy.** For `S = b^T*K^-1*b`, the definitions
   `delta* = -ell/(1+kd*S)` and `q* = -K^-1*b*kd*delta*` satisfy both
   `b^T*q* - ell = delta*` and `K*q* + b*kd*delta* = 0`. Consequently the
   potential-energy difference is `e^T*A*e/2`, with `e = q-q*`. Initial modal
   and hammer velocities are checked to be zero, so the implementation's
   initial perturbation energy omits no kinetic term for this fixture.

3. **Dissipation and reciprocal signs.** The independent physical definition
   uses `delta = b^T*q - ell`, `w = b^T*v`, damper force
   `F = delta*(kd+cd*w)` on the positive unclipped branch, and modal force
   `-b*F`. Subtracting equilibrium gives
   `M*v' = -A*e - C*v - b*cd*delta*w`. Thus
   `E' = -v^T*C*v - cd*delta*w^2 <= 0` while the branch holds. The code checks
   nonnegative background damping and `cd`; its static-face checks exclude
   actuator work. This is specifically the unclipped branch identity, not the
   different clipped-branch loss law.

4. **Observable envelopes.** Energy Cauchy–Schwarz gives the displacement
   bounds from `b^T*A^-1*b` and `h^T*A^-1*h`, and the relative-speed bound from
   `b^T*M^-1*b`. The code uses those respective forms with the correct vectors
   and masses. If their caps are `D`, `H`, and `W`, then
   `abs(F-kd*delta*) <= kd*D + cd*(delta*+D)*W`; this follows directly by
   expanding `F-kd*delta* = kd*b^T*e + cd*delta*w`. The implementation checks
   this bound against its force cap.

5. **Branch continuation and constant hammer.** The strict inequalities
   `delta*-D > 0`, `kd-cd*W > 0`, and
   `x0-h^T*q*-clearance+H < 0` prevent, respectively, damper separation,
   force clipping, and hammer contact. While the hammer is separated, its
   force and acceleration vanish; zero initial hammer velocity and no relaunch
   make `x = x0` constant. The negative-gap bound then preserves that premise.
   Starting on these branches, nonincreasing energy supplies the same strict
   margins up to any proposed first exit, contradicting continuity at that
   exit. Bounded modal energy also prevents finite-time escape on this smooth
   branch. The argument therefore covers the full declared interval rather
   than merely the initial state.

6. **Maximum enclosure and binary64 endpoints.** `_outward` converts a rational
   endpoint to a float, compares the float's exact rational value with the
   target, moves one adjacent float outward when necessary, and checks the
   direction again. Pointwise bounds over the nonempty closed interval also
   bound its continuous maximum. No separate extremum search is required for
   this enclosure claim. This review checked that logic; it did not separately
   rerun the endpoint calculations.

7. **Preserved semantics.** The positive damper records use `kind=enclosure`,
   `value_status=verified_enclosure`, empty maximizing intervals, and
   `time_status=unresolved_near_equilibrium`. They are not labeled positive
   plateaus. The separated enabled hammer records are zero plateaus over the
   whole `[0, duration]` interval, rather than disabled contacts or arbitrarily
   selected zero times. No maximizing time was computed in this review.

## Existing test scope and the one arithmetic probe

Read, but did not run or duplicate,
`test_preload_energy_and_outward_bounds_use_exact_rounded_definition` in
`tests/python/test_contact_peak_oracles.py`. It independently expresses the
energy as diagonal modal stiffness plus a rank-one term and uses
Sherman–Morrison for the inverse quadratic forms. It checks positive rounded
perturbation energy, the compression/hammer/speed/force inequalities, branch
margins, outward endpoints, and plateau/enclosure/time semantics. Its hammer
gap expression omits clearance, which is exactly zero in this frozen fixture;
the implementation itself includes clearance. This is not a defect for the
reviewed definition.

The only arithmetic command was an inline
`PYTHONDONTWRITEBYTECODE=1 python3 -B -` script using `fractions.Fraction` and
`fixtures()`. It did not call the oracle or a numerical solver. It formed

```text
delta0 = b^T*q0 - ell
gap_h0 = x0 - h^T*q0 - clearance
r_i = -k_i*q0_i - b_i*kd*delta0
delta_second0 = sum(b_i*r_i/m_i)
force_first0 = cd*delta0*delta_second0
```

The script reached and passed its assertions for stationary initial velocities,
no relaunch, a constant stationary face, `delta0 > 0`, `gap_h0 < 0`, `kd > 0`,
and both exact initial modal residual components being nonzero. It then exited
**1** with `AssertionError` on the reviewer's extra assertion
`delta_second0 > 0 and force_first0 > 0`. That sign was an unnecessary reviewer
assumption: the enclosure and unresolved-time semantics require neither
positive initial curvature nor positive initial force slope. The failure is
not evidence of a source defect. The planned numeric output lay after that
assertion and was never printed; this report supplies no unobserved derivative
values or stronger nonzero-derivative conclusion. No second arithmetic probe,
unit-test suite, or trajectory sweep ran.

## Source binding

SHA-256 identities of the reviewed source and normative inputs:

| Path | SHA-256 |
| --- | --- |
| `tools/contact_peak_oracles.py` | `eb9d848a41ff3d101a5b0d86bd49e1d039000103dd5b923295bfaef332bb5c68` |
| `tools/validate_contact_reference.py` | `c910da5da6b6ad957b709a414cef39a08ed6e8b75c6e36c9d73ef535a064dcdc` |
| `tests/python/test_contact_peak_oracles.py` | `9de8d7907b7a68fccf77788d9f04c93d4059d343bb4e6b1bacdc1b26bc2be1e6` |
| `docs/specs/CONTACT_ANALYTIC_PEAK_SLICE_20261123.md` | `56e5caefaebc03596b2ed566d50578b2be2e4dc54e37bf7fdce31dab31243f03` |
| `docs/agents/transient-campaign/w3_a54.md` | `ba47ba3dc750e54c3404e48b99b282d7ffb94f05e9152fb2a62ee083ff610baa` |
| `docs/specs/CONTACT_REFERENCE.md` | `f2e576ed41afd88f53588816190c420eb011254c19daa5928c146df4a5257c4c` |

The next action is for the integrator to retain its separately executed author
test evidence alongside this logical review. This report does not replace that
evidence or close PHY-002's continuous-extrema gate.
