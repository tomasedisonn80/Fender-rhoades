# W1-001 — independent supported-impact extrema design audit

Date: 24 November 2026. Worker: `w1_001_contact_extrema`.
Assignment: read-only implementation/design audit; this report is the only
authored file. No solver, acceptance, canonical-state, or historical-evidence
changes are made. No numerical contact experiment is claimed.

## Finding and smallest useful implementation

Implement a separate, explicitly two-fixture extrema reference and driver for
`supported_impact` and `supported_impact_scaled`. Keep the existing four-fixture
diagnostic and the general auditor's incomplete-extrema result unchanged.

Both requested fixtures have `kh=20000`, `ch=0`, `kd=cd=0`, no relaunch,
constant zero damper face, and a 0.020 s duration. Their hammer compression is
`D=max(delta,0)` and force is exactly `F=20000*D`; the damper is disabled.
Therefore hammer compression and force share maximizer times, while their value
budgets must still be checked separately. The general sixth-degree damped-force
locator and sinusoidal-face treatment are unnecessary for this first slice.
The modal subsystem remains coupled through the hammer and has nonzero modal
damping; this is not the scalar conservative free-impact fixture.

Recommended new implementation boundaries:

1. `tools/contact_extrema_reference.py`: immutable fixture/level settings;
   bounded stepwise Radau; accepted mesh and dense-segment retention; independent
   branch and stationary searches; refinement comparison; unresolved ledger.
2. `tools/validate_contact_extrema.py`: bounded input, exclusive artifact output,
   the two-fixture candidate matrix, reference cache, per-candidate comparisons,
   and provenance. Reuse the existing independent `Fixture`, `initial_vector`,
   `rhs`, and `observe` definitions, not C++ trajectories or root locations.
3. Focused locator, budget, schema, and comparison tests. Freeze the experiment
   plan before executing production candidate comparisons.

Compute six independent trajectories: two fixture definitions times R0/R1/R2.
Compare all 26 candidates with those references. Candidate step settings do not
select reference settings, roots, branches, or integration meshes. Integrate
the scaled fixture independently instead of manufacturing its reference by
scaling an already-computed unscaled solution.

## Equations and branches

State order, already used by `initial_vector` and `rhs`, is:

```text
[q0,q1,v0,v1,xh,vh,modal_loss,hammer_loss,damper_loss,
 actuator_work,launch_work,hammer_impulse,damper_impulse]
```

For mode j, `Kj=mj*(2*pi*fj)^2`, `Cj=2*mj*betaj`, and
`qddj=(-Kj*qj-Cj*vj+bhj*Fh-bdj*Fd)/mj`. Hammer definitions are
`delta=xh-bh.q-clearance`, `w=vh-bh.v`, and
`a=-Fh/mh-bh.qdd`. The general active-branch derivative is
`Fdot=w*(k+c*w)+c*delta*a`; here it reduces to `20000*w`.

For each smooth positive-compression segment, physical stationary points solve
`w=0`. Include both physical endpoints, every independently located `delta=0`
boundary, and every positive-branch stationary point. `z=k+c*w` is identically
20000 for the enabled hammer, so no hammer clamp boundary exists in this slice.
There are no external action states; reject an input claiming one. Cover the
entire 20 ms interval, including every recontact, rather than stopping after
the first impact. Before first onset the modes remain at rest and
`xh=-0.0002+t`; therefore first onset at `0.0002 s` is an independent analytic
check available without running either solver.

Disabled damper compression and force are exact zero on `[0,0.020]`, even when
the signed geometric damper gap is positive. Represent them as disabled
maximizer intervals, not an enabled plateau or positive gap maximum.

The signed coordinate transformation is `S=diag(2,-3)`, with `q'=S*q`,
`v'=S*v`, `m'=m/s^2`, `K'=K/s^2`, `C'=C/s^2`, and projections `b'=b/s`.
This preserves the physical gap, relative speed, force, and peak times.
Continue existing structural comparisons; report any additional peak scaling
comparison separately. Do not silently rescale the declared reference atol
vector for this fixture.

### Dense versus physical derivatives

Radau's public dense output is cubic on an accepted step [P1]. In normalized
local time `u=(t-ti)/(ti1-ti)`, reconstruct the 13 component cubics from four
public dense evaluations; check them at independent interior points and retain
the reconstruction residual. Keep the actual step endpoints. Do not depend on
private SciPy coefficient attributes.

For these two fixtures, `delta(u)` and state-derived `w(u)` are cubic;
`d(delta(u))/dt` is quadratic. Enumerate the real branch roots of `delta`,
physical roots of `w`, and dense extrema from `d(delta)/dt`. Retain the union,
tagging `physical_rhs_derivative` and `dense_derivative` distinctly. Their
roots need not coincide between collocation nodes. Since `c=0`, the force
search can share these root locations while preserving both observable values.
Partition every accepted interval at its branch roots before assigning smooth
branch eligibility. Keep left/right dense-segment endpoint evaluations when
roundoff produces a small discontinuity; record the discrepancy.

The specified quarter-point sign scan is a useful independent path, but cannot
be the only path: SciPy explicitly warns that sign-change event detection can
miss multiple crossings in a step [P2]. Polynomial candidates expose nearby
roots or even roots missed by that scan. `PPoly.solve(discontinuity=False,
extrapolate=False)` is a public option; its zero-section NaN marker must become
a structured plateau descriptor, never numeric evidence [P3].

Strictly bracketed roots get retained bisection brackets of width at most
`1e-12 s`, at most 80 iterations, with failure if the midpoint is not
representable. Endpoint zeros remain explicit. A floating polynomial root
without a justified isolating bracket is an additional candidate, not a
certified root. Retain residual/conditioning failures, tangencies, near-flat
regions, and unresolved clusters. Do not merge close peaks only because their
times round to the same output precision. Exact plateaus require an identity;
small derivatives alone imply `numerically_flat_unresolved`.

## Exact declared settings and gates

The R-level settings are the existing proposal, not candidate controls:

| Level | Maximum step (s) | rtol | Base atol multiplier |
|---|---:|---:|---:|
| R0 | 5e-6 | 1e-10 | 10 |
| R1 | 2.5e-6 | 1e-12 | 1 |
| R2 | 1.25e-6 | 1e-13 | 0.1 |

Base atol, in the 13-coordinate order above:

```text
[1e-16,1e-16,1e-13,1e-13,1e-16,1e-13,
 1e-16,1e-16,1e-16,1e-16,1e-16,1e-16,1e-16]
```

Per fixture/level: at most 200,000 accepted steps, 5,000,000 RHS evaluations,
1,000,000 search evaluations, 100,000 retained root/endpoint candidates, and
300 s wall time. Count actual RHS calls through a wrapper from construction
onward, including finite-difference Jacobian work; also retain SciPy's own
`nfev`, `njev`, and `nlu`. Enforce step/search/candidate limits before exceeding
them. Use a parent process deadline in addition to cooperative checks so a
stalled solver call or import can be terminated. Preserve partial mesh and
failure details; post-hoc counts do not enforce budgets. Use one registry of
locations with observable references so identical endpoints are not needlessly
duplicated four times against the retention cap.

| Check | Frozen/proposed condition |
|---|---|
| Compression value budget | `BD(M)=1e-8 m + 1e-4*abs(M)` |
| Force value budget | `BF(M)=1e-4 N + 2e-4*abs(M)` |
| Independent value refinement | Both `abs(M1-M0)` and `abs(M2-M1)` at most `0.1*B(M2)` |
| Refinement classification | Stable endpoint/branch classification, no lost resolved peak cluster |
| Supplemental isolated peak timing | `1e-7 s + 1e-4*abs(tR2)` |
| Independent root bracket | Width at most `1e-12 s`; at most 80 iterations |
| Existing candidate convergence | Preserve both successive comparisons and the existing 0.8 decrease rule with its declared roundoff handling |

A resolved value uses `resolved_under_declared_refinement`; coverage remains
`uncertified_independent_search`. Preserve value, time, and coverage statuses
separately. A proven incorrect value remains a failure even when coverage is
incomplete. Ambiguous time pairing is inconclusive, not a forced association
with the candidate's earliest tie. Disabled/plateau/tied maxima use membership
or distance to all maximizing intervals rather than a single selected time.
Candidate event-bracket width is a schema check; an independent physical root
need not lie in that numerical candidate bracket.

Also re-evaluate the reference at the candidate-reported peak time, retain
stationarity/branch diagnostics there, and verify that the claimed maximum is
not below any exported sample or separate sampled-maximum diagnostic beyond
the appropriate value budget. These checks do not replace the global search.

## Candidate matrix and data shape

Read-only execution of `refinement_matrix(1, names)` and `plan(...)` confirmed
13 distinct setting tuples per fixture, 26 total. Matrix identity is
`contact-refinement-20261123-extra-1`; settings SHA-256 is
`802f018edbad9396df0af0099cd1d0bf6b0d0a9621b0a5ae95688a283d31e17b`.

Per fixture the tuples are the union of:

- h = 20/10/5/2.5/1.25 microseconds, eta = 2.5 nanoseconds, local scale = 1;
- h = 2.5 and 1.25 microseconds, eta = 10/5 nanoseconds, local scale = 1;
- h = 2.5 and 1.25 microseconds, eta = 2.5 nanoseconds, local scale = 0.1/0.01.

Output step is always `20e-6 s`. Scale-one local controls are rtol `1e-8`,
position atol `1e-12`, velocity atol `1e-9`, energy atol `1e-13`, and impulse
atol `1e-13`; local scale multiplies all five. Use actual Python-produced
setting tuples for slot identity, preserving their floating-point values.

Input remains native schema version 1, model `synthetic-contact-p1`, exactly one
run per file, the full eight-field setting tuple, samples/events/action lists,
and all four optional peak records with exact unit keys and native metadata.
The existing parser labels these two names `unsupported_fixture`; its unsupported
path skips initial/final-state and disabled-contact checks and permits action
origins. Merely changing that availability flag is unsafe. Introduce an explicit
two-fixture validation policy, or factor common parsing behind a policy whose
default preserves the existing four-fixture behavior. Validate initial state,
complete duration, empty actions, disabled zero records, exact setting slots,
duplicates/missing slots, and source envelope before reference comparison.

Retain a versioned plan and manifest with candidate raw byte hashes, fixture
definition and evaluated K/C hashes, producer build/binary hashes, auditor source
hashes, Python/NumPy/SciPy versions, exact settings, exit status, and elapsed and
budget counts. Large arrays should be separate hashed artifacts:

| Artifact | Suggested shape/content |
|---|---|
| Accepted mesh | `t: [N]`, `y: [N,13]`, start/end states and final time |
| Dense reconstruction | coefficients `[N-1,13,4]` in a declared normalized basis; residuals and segment IDs |
| Branch ledger | endpoint/bracket times, side, state, contact/branch classification |
| Root candidates | segment ID, method, observable, time/bracket, residual, eligibility, origin |
| Unresolved ledger | interval, reason, classification, conditioning/residual evidence |
| Per-level peaks | all four values, all relevant maximizing intervals, type and status |
| Comparisons | R0/R1 and R1/R2 differences; all 26 candidate outcomes, including every failure |

Do not describe min/max values across three numerical runs as a rigorous
enclosure of the ODE. A polynomial reconstruction enclosure alone also does
not bound trajectory error. This implementation supplies independent numerical
evidence; completion still requires a separate coverage review and the remaining
PHY-002 criteria.

## Implementation traps and focused validation

- `radau_oracle(tight=True/False)` only exposes two settings, hides accepted
  dense-segment metadata, and uses monolithic `solve_ivp`; do not redefine its
  cached behavior to stand for R2 or reuse it as a live-budget implementation.
- Existing `exact_supported` has useful affine branch matrices, but its returned
  loss coordinates are not populated for the damped supported fixture. A new
  state-only exponential cross-check is feasible; reusing the full existing
  oracle as a damped ledger reference would be wrong. Its finite branch scan is
  not a coverage certificate either.
- Do not pass the candidate's gap/force/event arrays into independent branch
  generation. Use the independent fixture and state only.
- Missing optional diagnostics, malformed supplied diagnostics, producer
  failure, unresolved reference, and genuine numerical failure need distinct
  retained outcomes.
- Test cubic reconstruction and normalized-time differentiation with known
  polynomials; two close roots with same-sign scan endpoints; a double root;
  an exact zero interval; near-flat nonzero data; roots at shared endpoints;
  and the mismatch between dense-position derivative and interpolated velocity.
- Test real cap enforcement with bounded fake workers/solvers, including a
  stalled worker and partial-artifact retention. Test invalid fixture/action and
  disabled-contact semantics. Test refinement just below/above unchanged
  thresholds and lost-root-cluster handling.
- Numerical checks should include analytic first onset, `Fmax=kh*Dmax`, disabled
  exact zeros, six complete 20 ms trajectories, both refinements, and the signed
  coordinate pair. Those are proposed checks, not results of this audit.

## Actual inspection and commands

Read starting documents `AGENTS.md`, `README.md`, `docs/PROJECT_STATE.md`,
`docs/ROADMAP.md`, and `project/state.json`. The root acquired the task claims
during the audit; this worker made no claim or state update.

Inspected:

- `docs/validation/research-gates-20261124.md`
- `docs/specs/CONTACT_EXTREMA_AUDIT_DESIGN_20261123.md`
- `docs/specs/CONTACT_ACCEPTANCE_20261123.md`
- `tools/validate_contact_reference.py` (fixtures, force/RHS/observations,
  analytic/Radau oracles, input checks, unconditional extrema incompleteness,
  settings, refinement, and signed coordinate comparisons)
- `tools/contact_peak_schema.py`, `tools/validate_contact_peaks.py`, and
  `tools/contact_peak_oracles.py` (selected parser/comparator/reference and
  bounded subprocess sections)
- `tools/run_contact_sweep.py` (plan and peak preflight)
- `src/contact_reference.cpp`, `include/rhodes/contact_reference.hpp`, and
  `apps/contact_reference.cpp` (contact/peak implementation, API, native output,
  fixture/scaling definitions)
- `tests/python/test_contact_peak_oracles.py` and
  `tests/python/test_contact_peak_comparison.py` (selected existing contracts)

Observed source HEAD: `3a4ef9c3794749cdc6bdba7e55ea9922fe840ac7`.
`tools/validate_contact_reference.py` SHA-256:
`c910da5da6b6ad957b709a414cef39a08ed6e8b75c6e36c9d73ef535a064dcdc`.
`tools/contact_peak_schema.py` SHA-256:
`af5cbf87d155ecb40c176b78a7527b2a7bd686c7fd515be621af93d910db86c4`.
These identify inspected bytes, not an executed reference experiment.

Actual checks:

- `python3 tools/project.py validate` — exit 0; 14 tasks, 26 evidence records,
  fidelity `unvalidated` at startup.
- `python3 tools/project.py sync --check` — exit 0; generated state current.
- `python3 tools/project.py next --json` — exit 0; PHY-002 and RT-001 available
  at that startup observation, before the root's new claims.
- Read-only Python fixture/plan inspection — exit 0; confirmed the constants,
  26-slot plan, and settings hash reported above.
- Package metadata inspection — NumPy 2.3.5, SciPy 1.17.0 installed.
- `python3 tools/check_docs.py` — exit 0; documentation integrity checks passed.

No contact sweep, Radau trajectory, build, CTest, or new numerical test was run.
The actionable next step is adoption of the narrow plan, followed by the
independent bounded reference and locator tests before generating comparisons.

## Public primary sources

Accessed 24 November 2026; these verify library behavior only. Equations,
settings, and implementation recommendations above are derived from the
repository contracts and this audit.

- [P1: SciPy 1.17.0 Radau](https://docs.scipy.org/doc/scipy-1.17.0/reference/generated/scipy.integrate.Radau.html): cubic collocation dense output; public `step`, `dense_output`, mesh state and evaluation metadata.
- [P2: SciPy 1.17.0 solve_ivp](https://docs.scipy.org/doc/scipy-1.17.0/reference/generated/scipy.integrate.solve_ivp.html): stepwise sign-change event detection can miss multiple crossings.
- [P3: SciPy 1.17.0 PPoly.solve](https://docs.scipy.org/doc/scipy-1.17.0/reference/generated/scipy.interpolate.PPoly.solve.html): discontinuity/extrapolation options and the identically-zero-section marker.
