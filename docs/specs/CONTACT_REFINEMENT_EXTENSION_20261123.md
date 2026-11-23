# Bounded contact refinement extension

Integrator decision before extension implementation, 23 November 2026. This
supplements the [frozen original acceptance](CONTACT_ACCEPTANCE_20261123.md);
it changes no physical observable budget, 0.8 decrease threshold, roundoff rule,
fixture constant or original refinement level. The original 208-run failure
record remains immutable.

## Reason and selection prerequisite

[Workers 001](../agents/continuation-20261123/w1-001.md) and
[006](../agents/continuation-20261123/w1-006.md) observed that a further 1.25 us
maximum step resolves the final decrease failures in three examined fixtures.
This is evidence to test a uniform refinement extension, not permission to
select only those fixtures or claim full acceptance. The original fixture
contract permits further refinement. Event-bracket midpoint resolution and
continuous extrema remain separate concerns.

Before extending, select evidence by the complete recorded settings tuple.
Conflicting duplicates must fail independently of input order; byte-equivalent
numerical repeats may deduplicate with provenance retained. The actual frozen
local tolerance scales are **1, 0.1, 0.01**. Arbitrary distinct scales do not
substitute for them.

## Explicit bounded policy

Both runner and auditor accept `--extra-step-levels` with integer choices 0, 1,
or 2, default 0. The extension applies uniformly to the 26 required fixtures.
Partial fixture generation may help diagnosis but cannot pass the whole gate.

| Extra levels | Step axis, microseconds | Full deduplicated matrix |
|---:|---|---:|
| 0 | 20, 10, 5, 2.5 | 208 |
| 1 | 20, 10, 5, 2.5, 1.25 | 338 |
| 2 | 20, 10, 5, 2.5, 1.25, 0.625 | 364 |

Each extended matrix retains every original configuration. Add the new maximum
steps with the original baseline local/event controls. At the selected finest
step, also add the frozen three event levels and three local scales, deduplicating
their shared baseline. A depth-2 matrix need not repeat depth-1's extra event/local
axes at 1.25 us; prior depth-1 observations are retained as separate artifacts.

Hold event/local controls fixed along each step comparison. Require each final
comparison to meet the unchanged observable budgets and apply the original
decreasing-error calculation to the final two differences. Preserve all earlier
differences, rather than truncating the series to its favorable tail. Refine
event/local axes at the explicitly selected finest step. Coordinate-rescaling
comparisons use matched complete settings.

## Reporting and completion

For an extended audit, emit both the original frozen-grid result and the extended
result, with explicit policy, matrix identities, selected settings and missing
slots. Do not relabel the original failed observations as passed. Reject invalid
policy values and conflicting evidence; missing required runs are incomplete.
Do not infer an extension from input ordering or select a depth after observing
which one passes. The operator declares it before generation/auditing.

The extension result can support convergence only for the declared matrix.
Independent-method waveform/energy/event agreement, peak localization and all
other frozen clauses remain necessary for PHY-002. In particular, no optional
step extension resolves the presently missing continuous-extrema acceptance.

Retain regressions for both input orders, exact/conflicting duplicates, substituted
local scales, partial fixture sets, missing finest axes, and default-policy
backward compatibility. Confirm the unchanged 208-run original comparison before
using new evidence for a completion decision.
