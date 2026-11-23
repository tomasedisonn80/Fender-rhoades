# Four-fixture analytic contact peak diagnostic

Wave 4 implementation contract, 23 November 2026. Scope is exactly
`conservative_impact`, `damped_impact`, `forced_clamp`, and `preload_equilibrium`
with their existing independent synthetic parameters. This adopts a bounded
slice of the [extrema proposal](CONTACT_EXTREMA_AUDIT_DESIGN_20261123.md), with
the rounded-preload correction below. It does not amend
[acceptance](CONTACT_ACCEPTANCE_20261123.md), the existing auditor, or PHY-002's
**incomplete** continuous-extrema gate. General ODE searches and calibration are
outside this contract.

## Parallel module ownership and interfaces

| Module | Public interface and responsibility |
|---|---|
| `tools/contact_peak_schema.py` | `parse_candidate(raw: bytes) -> ParsedCandidate`; stdlib-only strict parsing; expose `CandidateFormatError`. |
| `tools/contact_peak_oracles.py` | `reference_peaks(name: str, *, damped_time: bool = False) -> dict`; independent references only. |
| `tools/validate_contact_peaks.py` | `compare_candidate(parsed, reference) -> dict`, CLI, bounded ingestion and exclusive evidence retention. |

`ParsedCandidate` is frozen, with `fixture`, `settings` (ordered name/value pairs
in existing `RUN_SETTING_FIELDS` order), `run` (normalized native mapping),
`peaks` (mapping or `None`), `availability` (`available`, `not_available`, or
`unsupported_fixture`), and `raw_sha256`. Malformed data raises the bounded
format error; producer failure is retained as a failure, never compared as success.

The reference mapping has `fixture`, `duration_s`, `definition`,
`definition_sha256`, `reference_settings`, and `peaks`. Hash the independently
assembled definition's canonical JSON: sorted keys, compact separators, no NaN.
Each of the four named peak records contains `kind` (`isolated`, `plateau`,
`disabled`, `enclosure`), `unit` (`m`, `N`), `value_interval`, `value_status`,
`maximizer_intervals_s`, `time_status`, `coverage`, and `details`. Point targets
use `[M,M]`; only verified enclosures claim bounded truth. Unresolved values use
`null`, unresolved times use `[]`, and the corresponding status explains why.
Keep analytic domain arguments, numerical refinement and certification distinct.

The comparison mapping contains `fixture`, `candidate_sha256`, `availability`,
`validity`, `peaks`, and `phy002_acceptance: "incomplete"`. Each peak preserves
the candidate record and reference, plus separate `value_status`, `time_status`,
`observation_consistency_status`, `coverage`, budgets, errors and reasons. No
single passing value may erase another failed or unresolved field.

## Strict native input

[A53](../agents/transient-campaign/w3_a53.md) freezes the exact emitter fieldsets,
eight settings, 26 sample keys, diagnostic metadata, units and enums. Require
schema 1, model `synthetic-contact-p1`, one run, finite non-boolean numbers,
positive settings, ordered complete samples and valid nested diagnostics.
Reject duplicates, unknown/partial fields, nonfinite conversions and recursion
errors. Limits: 32 MiB/file, 128 MiB/collection, 64 files, depth 64, integer
magnitude at most `2**64-1`, 1–30,000 samples and at most 100,000 events.

For supported fixtures, require empty action snapshots, fixed duration within
`1e-12 s`, and matching initial mechanical state: exact expected zeros and at
most 64 ULPs for other components. All hammers are enabled; only the first three
dampers are disabled. Allowed peak origins are `sample`, `stationary_candidate`
and `disabled`. Stationary bracket width is at most
`event_tolerance_s*(1+1e-9)`; other brackets are degenerate. Validate
`0 <= lo <= time <= hi <= duration`. Disabled records require all numeric zeros.

Omitted `located_peak_candidates` means `not_available`; supplied null or
malformed content fails. Other fixture names remain `unsupported_fixture`, never
pass. The envelope omits physical configuration: names and initial-state checks
do not attest arbitrary supplied bytes. Retain source/definition identities and
any declared producer binary hash separately; never infer oracle parameters from
candidate observations.

## Independent references and bounded numerics

Use only independently assembled fixture definitions from
`validate_contact_reference.py`, never C++ trajectories, events, replay or peaks.
[A54](../agents/transient-campaign/w3_a54.md) supplies the normative derivations:
conservative closed forms; the damped scalar invariant and distinct force root;
forced-clamp compression maximum at zero and enabled zero-force plateau; and
rounded preload's exact-rational energy enclosure. Preserve whole zero plateaus
and disabled semantics separately. Preload's positive damper peaks have
`kind=enclosure`, `time_status=unresolved_near_equilibrium`; they are not exact
positive plateaus. Recompute and verify A54's rational inequalities, outward
rounding and branch margins from the independent binary64 definition.

Damped scalar bisection uses widths `1e-10, 1e-12, 1e-14 m/s`, at most 80
iterations per level, retained signs/brackets and explicit stagnation failure.
Optional SciPy time quadrature uses A54's regularized 128-term series, successive
`(epsabs,epsrel)` pairs `(1e-11,1e-9)`, `(1e-13,1e-11)`, `(1e-15,1e-13)`,
`limit=128`, 10,000 evaluations and an actively enforced 10-second wall cap.
Retain settings, counts, convergence messages, error estimates and refinements;
nonfinite results, exhausted limits or nonempty convergence messages yield
`inconclusive_reference`. Successive differences must fit 0.1 of the applicable
value or supplemental time budget. Quadrature error estimates are not
certificates. Without SciPy, value references and CLI/schema/help remain usable;
requested damped timing reports `not_available`.

## Comparisons, CLI and retention

Frozen value budgets remain compression `B(M)=1e-8 m+1e-4*abs(M)` and force
`B(M)=1e-4 N+2e-4*abs(M)`. For verified nonnegative `[L,U]`, sufficient pass
requires `max(abs(candidate-L),abs(candidate-U)) <= B(L)`; definite failure
requires `distance(candidate,[L,U]) > B(U)`; otherwise report
`inconclusive_reference`. Check claimed maxima against exported observations
using the same observable budget. Do not require the independent time inside a
candidate replay bracket. Isolated-time comparison uses supplemental
`1e-7 s+1e-4*abs(t_ref)`; plateau comparison uses interval membership/distance.
This timing diagnostic is not a frozen acceptance amendment.

CLI: repeatable `--candidate PATH`, required `--output NEW_DIR`, optional
`--with-damped-time` and `--producer-binary PATH`. Never execute supplied binaries.
Create the output directory exclusively; retain raw bytes/hashes, plan/settings,
definition/source identities, versions, every case/result and partial failures.
Do not overwrite, omit failures or silently retry. Exits: 0 completed requested
comparisons pass; 1 definite candidate/diagnostic failure; 2 missing, unsupported
or inconclusive requested evidence; 3 operational failure. Definite failures
take precedence over incomplete comparisons. Unrequested damped time does not
prevent a value-only exit 0. Default completion covers values, validity and
available analytic timing; the flag additionally requests damped timing.
Preload's unresolved positive timing remains separately incomplete. All reports
still say PHY-002 is incomplete.

Focused verification covers independent maxima, force/compression timing
distinction, zero/disabled/enclosure semantics, malformed/missing/unsupported
input, enforced budgets, optional-backend absence and exclusive retention. Do
not change the old auditor, production solver, frozen budgets or project state.
