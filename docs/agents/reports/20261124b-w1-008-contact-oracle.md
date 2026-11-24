# W1-008 — bounded independent supported-contact extrema reference

Date: 24 November 2026. Worker: `w1_008_contact_oracle`.
Assignment: implement only `tools/contact_extrema_reference.py` and this report.
The root owns Git, claims, evidence registration, canonical state, and production
experiment execution. No other worker was spawned. PHY-002 remains incomplete.

## Delivered scope and frozen source

Added a separate offline reference generator for the exact independent
`supported_impact` and `supported_impact_scaled` definitions. It reuses the
existing Python `Fixture`, `fixtures`, `initial_vector`, `rhs`, and `observe`;
it never calls the C++ solver, reads candidate trajectories, or seeds an
independent root search from candidate event or peak times. The scaled fixture
is independently integrated when requested. Its atol vector is not rescaled.

Final implementation SHA-256:
`35b1f822cef0da3036faaf8dffcfd37e9091ca3df5098650d3d306fac1e7908b`.
The root and driver worker were notified that these bytes were frozen before
production reference execution. No old four-fixture tool, acceptance document,
historical report, task status, or fidelity claim was changed.

This implements the narrow scope proposed by
[W1-001](20261124b-w1-001-contact-extrema.md) using the settings and limitations
in [the independent-extrema design](../../specs/CONTACT_EXTREMA_AUDIT_DESIGN_20261123.md).
The design remains a diagnostic proposal, not an adopted acceptance amendment.

## Public API and worker interface

```python
generate_reference(
    fixture, level="R2", *, limits=None, raw_path=None,
    checkpoint_path=None,
) -> dict

evaluate_reference(reference_dict, time_s, *, side="right") -> dict
compare_refinement([R0_reference, R1_reference, R2_reference]) -> dict
self_check() -> dict
```

The generator accepts either a supported name or that exact frozen `Fixture`.
Modified fixtures, unsupported names, unknown levels, invalid limits, and raised
limits are rejected. Each generation returns `completed` or
`inconclusive_reference`; a single completed generation has value status
`requires_refinement`. Coverage is always `uncertified_independent_search`.
An inconclusive result retains the partial trajectory and the best observed
values with inconclusive value status; those are not successful full-domain
maxima.

The top-level result includes `settings`, `limits`, `counts`, backend versions,
the solver's own `nfev`/`njev`/`nlu`, complete-duration/final-time fields, four
peak records, all root candidates, physical maximum clusters, branch sequence,
unresolved ledger, failure details, and fixture/source/artifact hashes. Peak
records use `value_m` or `value_n`, `classification`, `kind`,
`maximizer_intervals_s`, maximizing candidate references, and separate value,
time, and coverage statuses.

`evaluate_reference` validates the retained file and per-array hashes, selects
the exact left or right dense segment at a mesh endpoint, and independently
returns state, contact values, branch, physical relative velocity, dense
compression derivative, and physical force derivative. It does not use
`t +/- epsilon` or extrapolate. It deliberately reloads/verifies the artifact
per call; this is an offline diagnostic interface.

`compare_refinement` requires one exact setting tuple at each declared level,
one fixture identity and one source identity. It reports both successive value
differences against `0.1*B(M_R2)`, branch-sequence stability, physical maximum
cluster retention/pairing, classifications, time differences, and derivative
discrepancy statistics separately. Unresolved search entries block a resolved
hammer-value claim. Disabled damper values retain their analytic identity.
The result never calls the min/max of three numerical runs a rigorous enclosure.

Example parent-managed worker invocation:

```sh
python3 tools/contact_extrema_reference.py \
  --fixture supported_impact --level R0 \
  --output /tmp/contact-R0.json --raw /tmp/contact-R0.npz
```

Both final destinations are exclusive; existing artifacts are not replaced.
Exit 0 means the single reference completed, not that candidate acceptance or
coverage passed. Exit 2 means an inconclusive reference or invalid invocation.
`--limits-json` can tighten live limits. `--self-check` runs the analytic helper
checks without producing or comparing a contact trajectory.

## Numerical settings and bounded search

| Level | Maximum step (s) | rtol | Base atol multiplier |
|---|---:|---:|---:|
| R0 | 5e-6 | 1e-10 | 10 |
| R1 | 2.5e-6 | 1e-12 | 1 |
| R2 | 1.25e-6 | 1e-13 | 0.1 |

The 13-component base vector is
`[1e-16,1e-16,1e-13,1e-13,1e-16,1e-13,1e-16,1e-16,1e-16,1e-16,1e-16,1e-16,1e-16]`.
State order is the existing two modal positions, two modal velocities, hammer
position/velocity, five loss/work coordinates, and two impulses.

The independent public stepwise Radau interface covers all 0.020 s. Before each
accepted-step attempt, RHS call, search evaluation, and newly retained location,
the corresponding limit is checked. Actual RHS calls include finite-difference
Jacobian work starting from solver construction, unlike relying on `nfev` alone.
The fixed defaults are 200,000 accepted steps, 5,000,000 RHS evaluations,
1,000,000 search evaluations, 100,000 retained locations, and 300 wall seconds.
The module docstring defines the search-evaluation accounting. Finalization
after a cooperative stop is permitted so already bounded partial arrays can be
written.

Each accepted segment is reconstructed from four calls to its public dense
evaluator. Three independent interior evaluations check the reconstruction.
The reconstruction uses the actual representable normalized abscissae; it does
not assume that floating `t0 + h/3` maps back to exactly `u=1/3`. Coefficients
are ascending powers of normalized time. No private SciPy dense-output fields
are inspected.

For each segment, the generator independently enumerates cubic `delta` roots,
partitions its branches, searches cubic state-derived `w` roots on positive
branches, and separately searches the quadratic dense-position derivative.
Every search also uses a quarter-point sign scan. Bounded polynomial polishing
and residual/conditioning diagnostics complement retained bisection brackets;
they do not certify the ODE. Brackets are at most `1e-12 s`, with at most 80
iterations. An unrepresentable midpoint, exhausted search budget, or nonfinite
evaluation fails explicitly. Nearby roots are not merged by rounded output
times. Double roots, unbracketed roots, near-real clusters, small nonzero
functions, and polynomial-only plateaus stay visible in the unresolved ledger.

One candidate registry retains mesh endpoints, differing dense endpoint sides,
branch roots, and both kinds of stationary roots. Its arrays avoid duplicating
one physical location for four separate observables. Since `ch=0` and
`kh=20000`, force and compression share candidate locations while preserving
their distinct budgets. Disabled damper compression and force are identically
zero with the full `[0,0.020]` maximizer interval, regardless of geometric gap.

The frozen value budgets remain `1e-8 + 1e-4*abs(M)` for compression and
`1e-4 + 2e-4*abs(M)` for force. The supplemental time scale remains
`1e-7 + 1e-4*abs(t_R2)`. Derivative discrepancy statistics explicitly describe
the final implementation's one midpoint probe per dense segment; the two full
root searches remain separate.

## Raw data and hard-timeout retention

With `raw_path`, mesh times/states, segment times, dense coefficient arrays,
reconstruction residuals/limits, candidate times/states/segment IDs, and candidate
origin bits are written to a compressed NPZ. Its byte hash and each array's
shape/dtype/raw-byte hash are retained in JSON. Without `raw_path`, these arrays
are returned as JSON lists under `raw_arrays` for direct API callers.

The CLI additionally retains `RAW_STEM.partial.json` before numerical imports.
It contains the declared fixture, level, settings, limits, and incomplete status.
`RAW_STEM.partial.npz` starts at the initial state and is refreshed every 2,048
accepted steps or ten seconds. Refreshes atomically replace only checkpoints
owned by that invocation. The NPZ embeds `checkpoint_metadata_utf8` containing
the partial arrays' manifest, counts, roots, branches, unresolved entries, and
separate last mesh/dense times. The sibling JSON also records its NPZ byte hash.
Partial checkpoints always say `inconclusive_reference`.

Cooperative checks cannot interrupt a stalled native operation or import. The
driver must impose a parent deadline, retain partial files/stdout/stderr, and
classify a terminated worker as inconclusive. There is no implicit external
process launched by the API itself. Worker 011 was given the exact checkpoint
filenames and asked to consume explicit final result filenames rather than
glob all JSON files.

## Actual checks and observations

Startup checks were run before implementation:

- `python3 tools/project.py validate` — exit 0; 14 tasks, 26 evidence records,
  fidelity `unvalidated`.
- `python3 tools/project.py sync --check` — exit 0; generated state current.
- `python3 tools/project.py next --json` — exit 0; both active tasks already
  claimed by the root, so this worker made no competing claim.

Implementation/helper checks actually run:

- `python3 -m py_compile tools/contact_extrema_reference.py` — passed.
- `python3 tools/contact_extrema_reference.py --self-check` — passed, including
  the final frozen source. Maximum known-cubic coefficient error was
  `4.440892098500626e-16`.
- `python3 -O tools/contact_extrema_reference.py --self-check` — passed on the
  source immediately preceding the final one-line conservative timing guard.
  The checks use explicit exceptions, not removable `assert` statements.
- Independent known polynomial roots at 0.41 and 0.41002 were separately
  bracketed although the quarter scan saw no sign changes. Double-root,
  exact-zero, near-flat-nonzero, endpoint-root, noncubic rejection, normalized
  differentiation, and physical/dense derivative distinction checks passed.
- An inline API harness executed real Radau with each of accepted-step, RHS,
  search, and candidate limits set to one. Every run stopped at exactly that
  counter, returned `BudgetExceeded` and `inconclusive_reference`, and retained
  its initial/partial mesh. A `1e-9 s` wall limit also stopped explicitly.
- A three-step CLI run retained final and partial arrays of shape `[4,13]` and
  an explicit accepted-step exhaustion result. Its actual RHS count was 36;
  the harness also checked it exceeded SciPy's `nfev` because Jacobian calls
  are counted by the wrapper.
- An inline retained-evaluator harness reproduced the development R0 peak,
  checked force/compression and disabled identities, and rejected a changed
  NPZ hash. Unsupported fixtures, unknown levels, raised caps, nonmapping
  limits, and overwrite of a pre-existing NPZ were rejected.
- An inline refinement-unit harness used copies of one retained development
  record with synthetic per-level values; it did **not** run R1/R2. Values at
  `0.099*B` passed the difference gate, `0.101*B` failed it, and removing a
  physical maximum cluster produced the explicit lost/ambiguous-cluster issue.
  Value failure and still-agreeing supplemental times remained separate.
- Independent bisection calls with zero iterations and adjacent unrepresentable
  large-time midpoints reported `root iteration budget exhausted` and
  `root midpoint is not representable`.
- A real R0 worker was allowed to write its 2,048-step checkpoint, then stopped
  with `SIGSTOP`. The parent timed out after 0.15 s, killed it with `SIGKILL`,
  and verified that no final JSON/NPZ was claimed. The retained checkpoint had
  mesh shape `[2049,13]`, final time `0.004941384417464811 s`, matching per-array
  and file hashes, and inconclusive status. Worker return code was `-9`.
- `git diff --check -- tools/contact_extrema_reference.py` — exit 0.
- `python3 tools/check_docs.py` — exit 0 after the report was written;
  documentation integrity checks passed. The final two-path whitespace check
  also exited 0.

### Development R0 smoke run and retained failure

One full unscaled R0 smoke run was executed before final checkpoint/probing and
refinement-reporting additions. Its artifacts are temporary development outputs
at `/tmp/w1-008-contact-R0b.json` and `/tmp/w1-008-contact-R0b.npz`; they are not
the frozen production experiment. Its source hash was
`e583e4b6f2fa0a3b4d0dc2aa6f8fa81d1c950d7b007de53c4028c6e19ef63b01`.

| Observation | Actual development result |
|---|---:|
| Final time | 0.020 s |
| Accepted steps | 6,850 |
| Actual RHS evaluations | 49,425 |
| Search evaluations | 214,544 |
| Unique retained locations | 9,788 |
| Elapsed time | 8.673 s |
| Root candidates / physical maxima | 12 / 3 |
| Unresolved entries | 0 |
| Maximum compression | 0.000356919949843275 m |
| Maximum force | 7.138398996865501 N |
| Observed maximizing time | 0.004602537500252369 s |
| Analytic first-onset error | 2.9802302661287305e-13 s |

The maximum occurs in the third positive-compression interval. This actual
observation confirms the practical need to cover recontact instead of stopping
after the first impact. It is one numerical observation, not a signed-coordinate
or three-level refinement result.

The earlier development run was retained as
`/tmp/w1-008-contact-R0.json` and `.npz`. It stopped explicitly after 996 accepted
steps at `0.0025191618781554115 s`: assuming ideal 1/3 and 2/3 abscissae caused
the cubic reconstruction residual check to fail. The fix used actual
representable normalized sample coordinates. Poorly scaled companion-root
residuals also motivated bounded Newton polishing followed by independent
sign-bracket retention. The first failure was not relabeled as success.

For identification of these temporary observations, the successful R0 JSON hash
is `b420dd3e6efde5966554cc3875cb39aa0c8da1066bf219a988c1368c70fbc661`,
and its NPZ hash is
`f06d2b6fd00f5d6dd50f6c81f0ff1a2df84596936f8c15c619d9233323a66c2e`.
The earlier failed JSON hash is
`5d84947b3eb19418c19628419c77f3981cfff0d71ab85bc47d0a79e4946ca720`.
Observed runtime versions were Python 3.12.14, NumPy 2.3.5, and SciPy 1.17.0.

## Limitations and integration handoff

Only the new module and this report were authored. The final source has focused
analytic/cap/checkpoint checks; the one complete R0 trajectory above belongs to
the explicitly identified earlier development source. At the root's request,
this worker did not launch R1/R2, the scaled fixture, or the six-reference
production experiment. It also did not run CMake/CTest or candidate comparisons.
The synthetic refinement threshold test is not evidence of trajectory
refinement. Platform claims are limited to this Linux environment.

Next, use the frozen module through Worker 011's bounded parent driver, retain
all six R0/R1/R2 results and all 26 independently produced candidate records,
and assess both true reference refinements, the signed coordinate pair, exact
duration, classifications, unresolved ledger, and candidate value/time checks.
Keep every cap/solver/search failure visible. A separate review is still needed
for continuous-domain coverage and every remaining PHY-002 acceptance item.
No numerical agreement here establishes measured Rhodes fidelity or an audio
runtime result.
