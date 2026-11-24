# Wave 1 worker 004 — contact refinement diagnosis

Scope: read-only solver/acceptance audit; only this report was written. The
integrator owns PHY-002, canonical state, source changes and Git. Required
repository instructions, README, project state and roadmap were read.

## Finding and acceptance boundary

The ten failures in the latest
[branch-split result](../../validation/contact-split-depth1-20261123.json)
are failures of the frozen final-step **0.8 decrease rule**, not observed
exceedances of the independent sampled accuracy budgets. That distinction does
not turn the original grid into a pass. The retained result has 338 runs over
26 fixtures, zero sampled numerical failures, zero missing refinement slots,
and zero declared depth-1 refinement failures. Every run still lacks the
independent general continuous-extrema gate. Its worst reported sampled
comparison is 0.006460247266066269 of budget, for `evolved_relaunch_scaled`
modal velocity 1.

Current `src/contact_reference.cpp`, its public header and
`tools/validate_contact_reference.py` exactly match the SHA-256 bindings in that
retained result. This audit parsed selected JSON fields; no full raw candidate
sweep or independent reference study was rerun.

### All ten original-grid failures

The fractions below are the last two successive differences divided by each
observable's frozen budget. Values below one satisfy that accuracy comparison;
the separate convergence test requires the last value to be at most 0.8 times
the previous value when resolvable.

| Fixture | Observable | Previous fraction | Final fraction |
| --- | --- | ---: | ---: |
| damped_impact | q_m[0] | 2.998960e-7 | 7.686488e-7 |
| damped_impact | hammer_position_m | 9.484473e-8 | 9.712501e-8 |
| damped_impact | energy_j | 1.521576e-7 | 1.465026e-7 |
| damped_impact | damper_gap_m | 2.998960e-7 | 7.686488e-7 |
| event_after | event_time_s | 8.035487e-7 | 7.019191e-7 |
| grazing_tangent | event_time_s | 0.010216620 | 0.010216609 |
| relaunch_phase_minus | hammer_loss_j | 1.052484e-6 | 2.128476e-6 |
| supported_conservative | event_time_s | 1.908420e-6 | 1.981422e-6 |
| supported_impact | energy_j | 1.806131e-7 | 1.546609e-7 |
| supported_impact_scaled | energy_j | 1.901497e-7 | 1.547081e-7 |

**Do not describe all ten as machine roundoff.** The auditor already exempts
differences below a physical-scale 128-ulp resolution estimate. A bounded
replay of `damped_impact` with the existing executable reproduced its retained
fractions. At the final comparison's worst normalized samples, q differs by
8.30098556353024e-15 m (4,900,037 local ulps), hammer position by
1.9310047267781516e-15 m (142,483 ulps), and energy by
2.959351513842634e-14 J (68,238 ulps). These are extremely small relative to
the engineering budgets, but simple single-operation rounding does not explain
them. Adaptive-step changes, branch localization, accumulated numerical error,
and the location of the worst normalized sample remain relevant. This bounded
review does not uniquely attribute the other nine failures.

The tangent has a clearer diagnosis: its force-free motion uses the analytic
anchor, but `scan_segment` still calls time-tolerance-limited bisection for the
turning point. With event tolerance 2.5 ns, the existing executable produces:

| Maximum step | Reported tangent time, seconds |
| ---: | ---: |
| 20 microseconds | 0.001007301220703125 |
| 10 microseconds | 0.001007301220703125 |
| 5 microseconds | 0.0010072991699218749 |
| 2.5 microseconds | 0.0010073012207031237 |
| 1.25 microseconds | 0.0010073012207031237 |

All reported brackets contain the independently prescribed 0.0010073-second
turning point and meet the unchanged 2.5 ns width. The approximately 2.05 ns
alternation is a bracket-midpoint effect. Refining maximum step while holding
root tolerance fixed does not require that midpoint error to decrease
monotonically. The additional step yields an identical midpoint, explaining
this fixture's retained depth-1 pass without a change in mechanics.

## Coordinate scaling, event splitting and peak rollback

- **Coordinate scaling is consistent.** `error_norm` uses
  `sqrt(mass/reference_mass)` on mechanical coordinates. Under the declared
  signed transformation q'=s*q, m'=m/s^2 and projection'=projection/s, the
  absolute weighted error and physical gap defect remain invariant. The three
  retained scaling comparisons have maximum fractions 7.091418e-7,
  4.702993e-8 and 4.288195e-7 for supported impact, moving damper and evolved
  relaunch respectively. The existing native scaling selector passed.
- **Event splitting preserves the unprojected state and budgets.** `advance`
  selects the earliest eligible branch root's right bracket endpoint, replays
  once, rescans, and restarts. Abandoned work still consumes call-wide caps;
  its power-residual diagnostic is excluded from the accepted interval.
  Existing simultaneous and close-root tests passed. No event-splitting
  correctness defect was reproduced.
- **Peak rollback is transactional.** Public mutations operate on a complete
  `Reference` copy and move it back only on success. Partial collector samples,
  intervals, events, compensation, controller state and diagnostics therefore
  remain private on failure. Existing peak and combined split/peak rollback
  tests passed, including deterministic continuation after the failed call.
- **General peak coverage remains incomplete by design.** `peak_segment`
  requires sampled derivative sign changes and skips a force bracket when
  either endpoint is ineligible. A positive force maximum near a branch
  boundary or a pair of derivative roots inside one quarter segment can be
  missed. Its exported `uncertified_sign_change_search` label is accurate;
  these candidates cannot close independent-extrema acceptance.

One source-level numerical consistency risk deserves a targeted regression,
not an asserted defect: event `scan` constructs quarter/midpoint values with
different RK4 compositions while `refine` replays from a fixed interval anchor.
Thus an initial endpoint sign is not necessarily the sign of the trajectory
subsequently bisected. The peak collector already avoids this mismatch.
Before changing event scanning, construct a near-root fixture that proves a
bracket inconsistency; a wholesale replacement would change work counts and
could consume existing budgets earlier. No such counterexample was established
here.

## Minimal production improvement worth implementing separately

For **already certified single-mode, force-free tangencies only**, derive the
next gap maximum from `analytic_anchor_`, the oscillating mode's phase and
frequency, and the projection sign. Evaluate the candidate with the existing
analytic replay and retain an outward-rounded, validated bracket. This makes
the event location depend on the certified motion rather than the arbitrary
RK interval partition. Retain the existing bisection path for all uncertified
contact motion, the original work caps, time/gap tolerances and 0.8 rule.

Add a regression covering all five declared steps, signed projections,
nonzero time origins and multiple periods. Require a consistent analytic
turning-point enclosure, correct event count/order, zero force/impulse, and
unchanged rollback on budget exhaustion. This is an accuracy/reproducibility
improvement; the current bracket is already within its stated tolerance.
It cannot erase the other nine historical failures or replace the planned
independent supported-impact extrema experiment.

## Commands and actual outcomes

- `python3 tools/project.py validate`: passed, 14 tasks and 26 evidence records.
- `python3 tools/project.py sync --check`: passed.
- `python3 tools/project.py next --json`: no available unclaimed task; bounded
  review remained under the integrator's existing assignment.
- `build/vst3/rhodes_contact_tests scaling`: passed.
- `build/vst3/rhodes_contact_event_split_tests`: passed.
- `build/vst3/rhodes_contact_split_peak_tests`: all three selectors passed,
  96 / 386 / 243 checks respectively.
- `build/vst3/rhodes_contact_peak_tests peak_rollback`: passed, 112 checks.
- Existing CLI, parsed in memory: `--fixture grazing_tangent --max-step STEP
  --event-tolerance 2.5e-9`, STEP in 20/10/5/2.5/1.25 microseconds; all exited 0.
- Existing CLI, parsed in memory: same options with `--fixture damped_impact`,
  STEP in 10/5/2.5 microseconds; all exited 0. Differences were calculated with
  the unchanged auditor field mapping and tolerance function.

No rebuild was performed. Executed CLI SHA-256:
`4ed2552430f5de5846adcb1a7b99521b118a12b714ad61b146ede7bab24f8f2d`.
Reviewed solver SHA-256:
`0f9808442b5ddf6ddc6091c9b6294117b7aea85cd54707b9c3743121ac706100`.
These targeted existing-binary checks are not a new full source-bound numerical
acceptance study. No budget, solver, evaluator, historical artifact or task
status was changed.
