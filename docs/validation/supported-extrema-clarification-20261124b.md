# Supported-extrema refinement clarification — 2026-11-24

This corrects the narrative statement about reference **timing** refinement in
[supported-extrema-20261124b.md](supported-extrema-20261124b.md). The original
machine-readable result and its source/producer bindings remain unchanged. The
[independent evidence audit](../agents/reports/20261124b-w2-023-extrema-evidence-audit.md)
recomputed the comparisons and checked every retained artifact and final array.

The sentence saying that reference value **and time** refinement must lie within
one tenth of their budgets overstates the frozen timing requirement. The
[original audit design](../specs/CONTACT_EXTREMA_AUDIT_DESIGN_20261123.md), the
[prior continuation](research-gates-20261124.md), and the executed implementation
use these separate rules:

| Reference check | Declared rule |
|---|---|
| Peak values, R0→R1 and R1→R2 | Each absolute difference is at most `0.1 * B(M_R2)` |
| Supplemental isolated-peak timing | Each successive interval-centre difference is at most `1e-7 + 1e-4*abs(t_fine)` seconds |
| Physical peak-cluster pairing | Matching counts, crossing classifications and an unambiguous partner within that supplemental time budget |
| Disabled damper timing | Membership in the whole-duration maximizing interval; no unique timestamp is inferred |

Here `B(M)` is `1e-8 + 1e-4*abs(M)` metres for compression or
`1e-4 + 2e-4*abs(M)` newtons for force. The plan's field
`both_reference_refinement_fractions: 0.1` describes the **two successive value
comparisons**. Its name is ambiguous when read separately from the original
contract; it does not introduce a one-tenth timing threshold.

The initial run's observed timing differences also satisfy the stricter,
unrequired one-tenth scale. The unscaled maximum's successive centre differences
are about 3.53e-13 and 1.18e-13 seconds. Thus this narrative correction does not
change any recorded pass/fail result. It does not tighten, relax or replace the
frozen numerical rules. Agreement under those rules remains distinct from a
proof of continuous extremum coverage.

## Source and retained-data scope

The initial comparison used the preserved native producer and source identified
in its report. The subsequently committed anchor-based grazing change has a
new `src/contact_reference.cpp` identity. The initial experiment alone must not
be presented as an execution of that changed source. A separate native replay
can establish compatibility of the two supported-impact candidate sets; that
replay must retain its own source, binary and comparison identities.

All final and partial/checkpoint NPZ payloads together occupy **86,175,359 bytes**;
the six final reference NPZ files account for **44,195,042 bytes**. The larger
number in the initial narrative includes the retained checkpoints. Raw data,
independent searches, reference refinement, candidate compatibility and complete
PHY-002 acceptance remain separate claims.
