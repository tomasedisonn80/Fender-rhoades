# Independent contact extrema diagnostic — proposed design

Worker 051 proposal, 23 November 2026; **not an adopted acceptance amendment**.
This is a bounded next implementation for the synthetic `p=1` offline reference.
The [frozen contact acceptance](CONTACT_ACCEPTANCE_20261123.md), existing
refinement matrices and [optional candidate contract](CONTACT_PEAK_DIAGNOSTICS_20261123.md)
remain unchanged. A passing diagnostic must leave the existing auditor's
continuous-extrema condition **incomplete** until a separate coverage review.

## 1. Purpose and independent boundary

Implement a separate `tools/validate_contact_peaks.py` that reads opt-in candidate
JSON and writes a new, exclusive output directory. It must never call the C++
force routine, replay, event detector or peak locator to construct its reference.
Reuse only the independently assembled fixture definitions and equations in
`validate_contact_reference.py`; preserve their existing audit behavior.

Report three separate questions: is the candidate record valid; do independently
located peak values agree under the frozen observable budgets; and what portions
of the continuous domain have a justified coverage argument? Finite sampled
agreement answers neither the third question nor physical fidelity. Missing
optional data means `not_available`, not zero peaks. No overall acceptance pass
is produced by this first diagnostic.

## 2. Equations and all possible maximizer locations

For either contact define signed compression `delta`, relative speed `w`, relative
acceleration `a`, and `z=k+c*w`. For an enabled contact, compression is
`D=max(delta,0)`. Force is `F=delta*z` only when `delta>0` and `z>0`; otherwise
`F=0`. A disabled contact has zero diagnostic compression and force, matching
the candidate API. All quantities use SI units.

| Quantity | Hammer | Damper |
|---|---|---|
| `delta` | `x_h-b_h^T q-g_h` | `b_d^T q-ell(t)` |
| `w` | `v_h-b_h^T v` | `b_d^T v-ell_dot(t)` |
| `a` | `-F_h/m_h-b_h^T q_ddot` | `b_d^T q_ddot-ell_ddot(t)` |

Compute each modal acceleration independently as
`q_ddot_j=(-K_j*q_j-C_j*v_j+b_hj*F_h-b_dj*F_d)/m_j`.
For the sinusoidal face `ell=o+A*cos(omega*t)`,
`ell_ddot=-A*omega^2*cos(omega*t)`; a constant or linear face has zero acceleration.

Inside a smooth positive-compression branch, `D_dot=w`. Inside a positive-force
branch, the product rule gives `F_dot=w*(k+c*w)+c*delta*a`. Thus candidate
stationary points solve `w=0` or that force derivative equals zero, respectively.
The force maximum need not coincide with maximum compression when `c>0`.

The independent search must also include interval endpoints, both sides of every
external action, and boundaries `delta=0` and `z=0`. A velocity-only relaunch
preserves compression but can jump force immediately. Use the stored independent
left/right action states; `t±epsilon` can accidentally sample another branch.
At a natural branch boundary, positions and velocities are continuous but the
applicable derivative formula can change. Treat that boundary as an endpoint.

Equal maxima can occur at several times or over a whole plateau. Preserve all
maximizer intervals relevant to a time comparison; do not impose the candidate's
tie-breaking order on the reference. Exact zero separated/clipped force is a
semantic check, not a tolerance-rounded small positive value.

## 3. Reference hierarchy and declared numerical settings

Use the existing analytic free-mass, scalar oscillator and branch-matrix
exponential references where applicable. For nonlinear fixtures, add a separate
parameterized Radau reference that retains accepted mesh endpoints, dense
solutions, independently found branch boundaries, and action states. Do not
change the existing cached `oracle_for` results or silently redefine `tight`.

Freeze the following levels before executing candidate comparisons:

| Level | Radau maximum step | Relative tolerance | Absolute vector multiplier |
|---|---:|---:|---:|
| R0 | `5e-6 s` | `1e-10` | `10` |
| R1 | `2.5e-6 s` | `1e-12` | `1` |
| R2 | `1.25e-6 s` | `1e-13` | `0.1` |

The base vector, in the existing 13-coordinate order, is
`[1e-16,1e-16,1e-13,1e-13,1e-16,1e-13,1e-16,1e-16,1e-16,1e-16,1e-16,1e-16,1e-16]`.
Record actual solver/version, accepted mesh, evaluation count and final time.
Keep candidate timestep/event/local-tolerance refinements separate from these
independent-reference refinements.

For an initial implementation, partition each reference mesh interval at known
branch and action boundaries, sample each subinterval at its endpoints and
quarter points, and refine strictly bracketed stationary roots. Retain endpoint
zeros explicitly. Use bounded bisection to retain an actual final bracket:
width at most `1e-12 s`, at most 80 iterations, and abort an unrepresentable
midpoint. A root inside an unresolved branch region remains unresolved. A scalar
root-finder's tolerance alone is not a retained bracket or a coverage proof.

Cap each reference at 200,000 accepted steps, 5,000,000 RHS evaluations,
1,000,000 search evaluations, 100,000 retained root/endpoint candidates and 300 s
wall time. Use the public stepwise Radau interface [3] or a worker process so these
caps can actually stop work; post-hoc counts cannot enforce a budget. Exhaustion
or nonfinite values yield `inconclusive_reference` with the partial evidence.
These are proposed offline diagnostic limits, not changes to C++ budgets.

## 4. Roots that sign changes cannot cover

SciPy documents that event detection can miss multiple crossings inside a step;
Radau dense output is a cubic collocation interpolant [1]. Decreasing a scan
spacing is useful evidence, not proof that all extrema were found. Preserve
`coverage=uncertified_independent_search` for the initial implementation.

For constant/linear prescribed motion, each accepted Radau dense segment makes
`delta` and the state-derived `w` cubic polynomials; `delta*(k+c*w)` has degree at
most six. Enumerating real roots of the polynomial derivatives and branch
polynomials provides additional candidates, including even roots. Reconstruct
coefficients in normalized local time from the public dense evaluator, record
the reconstruction residual, and validate the adapter against known cubics.
Use `extrapolate=False` and `discontinuity=False` for `PPoly.solve`; it explicitly
represents identically zero sections using a special nonfinite marker [2]. Handle
that marker as a plateau descriptor, never as an ordinary numeric peak or as
arbitrary NaN input. Floating polynomial roots still need residual/conditioning
checks and do not certify the ODE trajectory. Sinusoidal faces require a separate
analytic/trigonometric treatment; do not pretend their dense contact function
is polynomial.

Crucially, differentiating the dense position polynomial need not reproduce the
separately interpolated velocity, and differentiating the velocity polynomial
need not reproduce the RHS acceleration between collocation points. Keep
`dense_derivative` and `physical_rhs_derivative` searches distinct, include their
candidates, and report their discrepancies under R0/R1/R2 refinement. Do not
claim a physical stationary point merely because a fitted polynomial has one.

An isolated even-multiplicity derivative root can be a stationary inflection;
nearby pairs of roots can hide a local maximum between same-sign endpoints.
Check both in synthetic locator tests. A numerically tiny derivative over several
samples is `numerically_flat_unresolved`; an exact plateau requires an analytic
identity or identically zero validated polynomial on that segment. Retain the
entire plateau interval and its endpoints. Unresolved tangencies and clusters
remain in the coverage ledger even when the observed peak value is stable.

## 5. Value comparisons and schema

Use existing frozen budgets without fitted scale, alignment or denominator:

| Observable | Candidate/reference diagnostic limit `B(M)` |
|---|---|
| Peak compression | `1e-8 m + 1e-4*abs(M)` |
| Peak force | `1e-4 N + 2e-4*abs(M)` |

Before comparing the candidate, require both `abs(M_R1-M_R0)` and
`abs(M_R2-M_R1)` to be at most `0.1*B(M_R2)` for each peak, with stable action-side
classification and no lost resolved peak cluster. Label this
`resolved_under_declared_refinement`, not certified. Otherwise report the
candidate comparison `inconclusive_reference`. Finite reference refinement does
not by itself establish completeness of the domain search.

For isolated, unambiguously paired stationary points, record a supplemental time
comparison using `1e-7 s + 1e-4*abs(t_R2)`, consistent with the existing event-time
scale. This is a newly proposed diagnostic for peak timing, not an assertion that
the frozen event criterion already specified peak-time acceptance. For plateaus
and tied maxima, compare membership/distance to the maximizing intervals and
mark a single-time score inapplicable. Preserve value, time and coverage results
separately. A known wrong value must remain `fail` even if coverage is incomplete.

Require the existing run envelope and exact setting tuple. When supplied,
`diagnostics.located_peak_candidates` must contain the exact current coverage and
evaluation-method strings and all four records: `hammer_compression`,
`hammer_force`, `damper_compression`, `damper_force`. Compression uses `value_m`,
force uses `value_n`; each also has `time_s`, `bracket_lo_s`, `bracket_hi_s`, and
`origin`. Reject duplicate JSON keys, booleans as numbers, nonfinite/negative
values, times outside the fixture, reversed brackets, unknown origins or unit
substitutions. Enabled origins are `sample`, `stationary_candidate`,
`action_before`, `action_after`; only a disabled contact may use `disabled` and
its zero record. Check action origins against actual declared action times.
For a stationary record enforce the candidate's declared event-bracket width;
do not demand that the independent root lie in that numerical replay bracket.
Compare that relation separately, allowing for trajectory error.

Also check that the claimed maximum does not fall below exported endpoint/action
observations beyond the same observable budget. Re-evaluate the independent
trajectory at the reported time and correct action side. Neither check replaces
the independent global search. Existing sampled maxima remain separately named.

## 6. Required independent tests and retained evidence

| Test | Independent expectation or failure it exposes |
|---|---|
| Conservative free impact | `mu=0.0016 kg`, `omega=sqrt(20000/mu)`; maximum compression `1/omega`, force `20000/omega`, time `pi/(2*omega)` |
| Damped free impact | Scalar invariant `Phi(w)=2*w-log1p(2*w)` gives compression; independently solve positive-branch force stationarity before `w=0` |
| Forced clamp | Compression maximum `1e-4 m` at time zero; force identically zero despite positive compression |
| Preload equilibrium | Constant analytically assembled damper compression/force; full-duration plateau |
| Sinusoidal damper | Analytic prescribed-face acceleration sign and both branch boundaries; no polynomial-face substitution |
| Relaunch before/at/after separation | Exact left/right states, preserved positions, force jump and action order |
| Two close roots, even root, plateau | Known polynomial examples defeat sign-only scan and distinguish multiplicity from a flat interval |
| Coordinate-rescaled pair | Same physical compression and force with transformed modal coordinates |
| Invalid/missing/overflow records | No comparison passes; missing option remains distinguishable from malformed supplied data |
| Budget and unresolved-reference cases | Explicit incomplete result and retained partial output, with no accepted diagnostic pass |

The scalar invariant and conservative targets already have independent
derivations in [W1-A005](../agents/development-20261123/W1-A005.md); do not copy
candidate C++ expected values into the new oracle. Keep root/time/plateau tests
independent of whichever locator implementation is selected.

For the damped free-mass fixture, let `alpha=c/k`,
`Phi(w)=alpha*w-log1p(alpha*w)`, and `w0=1 m/s`. Then
`delta(w)^2=(2*mu*k/c^2)*(Phi(w0)-Phi(w))` and
`a=-delta*(k+c*w)/mu`. On the active branch, force stationarity reduces to
`w=c*delta(w)^2/mu`, or
`Phi(w)+(alpha/2)*w-Phi(w0)=0`. Its left side is strictly increasing for
`0<w<w0`, giving one compression-side force peak; on rebound `w<0` the force
derivative is negative while the branch remains active. This supplies a scalar
oracle independent of the trajectory peak scanner. Obtain its time separately
from the monotone relative-motion quadrature, treating its integrable endpoint
singularity explicitly. Handle `c=0` with the conservative formula.

Retain plan and settings, candidate raw bytes/hashes, source/probe identities,
reference versions/settings, all four peak results at all three levels,
brackets/origins, unresolved intervals and reasons, execution budgets and exits.
Large reference arrays stay in raw artifacts with per-array hashes. A compact
report must include every fixture and every failure, not only the worst case.

## 7. Separate future coverage decision

Promotion requires a reviewed argument that each smooth segment either has all
relevant roots isolated or has a conservative value upper bound below the
current maximum plus the declared budget. Branch transitions, action sides,
tangencies and plateaus must be covered too. An interval enclosure of only the
interpolation polynomial is insufficient: a bound on the numerical trajectory's
error relative to the ODE is also needed for a claimed mathematical certificate.
Analytic fixture proofs or validated integration/enclosures are possible routes;
this document implements neither and adds no such dependency.

Independent peak-value evidence can then be reviewed for the finite fixture
domain without demanding a general theorem for every public input. The root
integrator must explicitly adopt any completion decision, preserve historical
incomplete reports, and retain all other PHY-002 criteria. No audio-path or
physical-fidelity claim follows from these offline synthetic checks.

## Public primary sources

1. [SciPy 1.17.0 `solve_ivp`](https://docs.scipy.org/doc/scipy-1.17.0/reference/generated/scipy.integrate.solve_ivp.html):
   Radau method/dense-output definition and event sign-change limitation.
2. [SciPy 1.17.0 `PPoly.solve`](https://docs.scipy.org/doc/scipy-1.17.0/reference/generated/scipy.interpolate.PPoly.solve.html):
   real polynomial roots, discontinuity/extrapolation controls, zero-section marker.
3. [SciPy 1.17.0 `Radau`](https://docs.scipy.org/doc/scipy-1.17.0/reference/generated/scipy.integrate.Radau.html):
   public `step`, `dense_output`, mesh state and evaluation-count interface.

Sources accessed 23 November 2026. These establish library behavior; equations,
proposed settings and the coverage distinction above are this design's derivation
and engineering choices, not claims made by SciPy.
