# Adopted analytic contact-peak diagnostic

Integrator adoption, 23 November 2026, before this diagnostic's implementation
and candidate comparisons. This is a bounded first part of the
[independent extrema proposal](CONTACT_EXTREMA_AUDIT_DESIGN_20261123.md).
The existing contact auditor, frozen refinement gates, and optional C++ peak
collector remain unchanged. This diagnostic cannot complete PHY-002.

## Initial fixture domain

Implement a separate `tools/validate_contact_peaks.py` for the following exact
synthetic fixtures from `validate_contact_reference.py`:

| Fixture | Independent maximum argument |
|---|---|
| `conservative_impact` | Reduced-mass sinusoidal compression during the single conservative contact; zero force after separation |
| `damped_impact` | Scalar relative-motion invariant and its unique compression-side force-stationarity root, followed by decreasing rebound force |
| `forced_clamp` | Decreasing positive compression from the initial state with identically clipped force |
| `preload_equilibrium` | Assembled static compliance and constant damper compression/force over the whole interval |
| `grazing_miss` | The prescribed free oscillator remains strictly separated |
| `grazing_tangent` | The free oscillator reaches zero gap at its analytic tangent and never has positive compression |

Check each fixture's assumptions explicitly before applying its formula. Do not
extend an oracle by matching only a familiar fixture name. Disabled contacts
have exactly zero diagnostic values. For the preload fixture, check that the
stationary hammer remains separated from the assembled equilibrium.

The derivations in the proposal supply the conservative and damped scalar
oracles. Compute the damped compression maximum from the invariant at relative
speed zero. Find the force peak from the strictly monotone scalar equation with
an explicit bounded bracket; retain that bracket and residual. Verify the
claimed maximum over the full declared interval, including its initial and
terminal states. This phase may report analytic maximizer sets or supplemental
times where available, but does not introduce a mandatory peak-time gate.

Use high-precision standard-library Decimal arithmetic for the non-polynomial
scalar invariant/root calculation, with separately declared 40- and 70-digit
evaluations. Both peak-value results must differ by at most one tenth of the
corresponding frozen observable budget before the comparison is resolved.
Cap scalar bisection at 256 iterations and report unresolvable progress or a
failed bracket explicitly. No ODE trajectory search is required in this phase.

## Comparisons and reporting

Candidate compression uses the unchanged limit
`1e-8 m + 1e-4 * abs(reference_peak)`. Candidate force uses the unchanged limit
`1e-4 N + 2e-4 * abs(reference_peak)`. Retain raw values and absolute errors;
do not fit a scale, shift, or denominator. Validate the exact optional schema,
SI units, finite values, brackets, origins, action-side semantics, and declared
settings before comparison. Reject duplicate JSON keys and boolean numeric
fields. A candidate peak below an exported endpoint/action observation beyond
the same budget is a failure.

Distinguish malformed supplied data, absent optional diagnostics, unsupported
fixtures, unresolved references, and numerically failed comparisons. Preserve
every supplied run, including repeated setting tuples; never choose a favorable
duplicate. Missing or unsupported records cannot produce a passing comparison.
An overall report remains `incomplete` even when all supported comparisons pass,
because this six-fixture phase does not cover the complete acceptance domain.
Known numerical failures remain explicit within that incomplete report.

Use an exclusive output path and retain input/source hashes, the exact plan,
formula assumptions, precision levels, root brackets and residuals, per-run
decisions, maxima and all limitations. The command must work without NumPy or
SciPy for this analytic domain. Keep ordinary CLI help dependency-free.

## Verification and later work

Use independently derived conservative, damped, clamp, equilibrium and grazing
values; include a mutation that corrupts a candidate peak and must fail. Exercise
missing diagnostics, wrong units/origins, nonfinite/boolean values, duplicate
JSON keys, unsupported fixtures and scalar-budget exhaustion. The tests must
distinguish a value comparison from a coverage claim.

After implementation review, generate all 26 fixtures with the existing opt-in
collector on the explicitly declared depth-1 matrix. Run this diagnostic once
on that fixed input. Retain covered and uncovered outcomes without replacing
the earlier 338-run audit. General independent dense-trajectory searches and
their coverage review remain separate future work under the proposal.
