# Development campaign — 23 November 2026

The current user authorized continued development on `main`, incremental commits
and pushes, public technical sources, and **100 actual worker assignments in five
waves of twenty**. At most three workers run alongside the integrator. A wave
finishes before the next starts. The earlier setup campaign remains historical.

## Intended engineering result

1. Implement the `PHY-002` finite-mass hammer and compliant damper reference with
   persistent modal motion, explicit action/linkage work and an energy ledger.
   Freeze synthetic fixtures and tolerances before implementation. Preserve the
   existing analytic primitives as independent limits.
2. Advance `RT-001` with bounded sample-offset events, atomic input rejection,
   allocation checks, and full-call timing. Keep its numerical/antialiasing gate
   open if this campaign does not establish every required error budget.
3. Build an optional VST3 instrument prototype using the existing synthetic
   research engine. Keep the offline contact solver outside the audio callback.
   Validate the actual adapter and state format before calling it usable in a
   host; a prototype does not imply measured Rhodes accuracy.

## Five waves

| Wave | Assignments | Purpose |
|---|---|---|
| 1 | A001–A020 | Audit, public-source review, frozen model/test contracts |
| 2 | A021–A040 | Contact, event and host implementation with independent review |
| 3 | A041–A060 | Numerical validation, edge cases and defect correction |
| 4 | A061–A080 | Integration, host validation, performance and platform checks |
| 5 | A081–A100 | Acceptance audit, documentation and durable handoff |

Every actual assignment returns a report under `development-20261123/`. A
separate generated development ledger records the returned collaboration task
names and immutable report hashes. A report records work performed, including
negative findings; it is not proof of physical fidelity or of resolved defects.

## Boundaries

All physical constants used before acquisition are explicitly synthetic.
`SPEC-001` and `DATA-001` remain blocked by the absence of an identified specimen
and calibrated measurements. Public papers guide equations and numerical methods;
they do not supply missing measurements of our target. The project-wide software
license remains an owner decision. Any optional SDK must have its origin, exact
revision and applicable license documented.

The integrator owns canonical state, acceptance decisions and Git publication.
Workers edit only assigned paths. Rejected or incomplete acceptance conditions
remain visible in the final handoff, without weakening their thresholds.
