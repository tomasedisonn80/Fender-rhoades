# Photo editor continuation — 24 November 2026

## Scope

Continue HOST-004 on main from `e2aa41cfe728a6510166fe2446eecaa4c4ab4e13`.
The original 1536 × 1152 photo remains the idle editor surface. Improve native
interaction, visible-key feedback, host compatibility evidence and regression
coverage without changing instrument identities or the neutral audio path.

The preceding owner assignment is resumed through its existing HOST-004 lease.
The source checkout and build/dependency copies belong to this chat. No other
checkout or build tree is modified. Git publication uses normal fast-forward
pushes, with concurrent main changes inspected and reconciled.

## Requested campaign

The user requested 100 actual workers in five waves. The environment permits
three workers alongside the integrator. Each wave therefore queues twenty
separate assignments. A planned assignment does not count as an executed worker.
Reports live under `docs/agents/reports/photo-continuation-20261124/`; a final
source-bound ledger will enumerate the workers actually launched and completed.

| Wave | Purpose | Planned workers |
| --- | --- | --- |
| 1 | Recover prior evidence; audit interaction, geometry and host contracts | 20 |
| 2 | Implement scoped corrections and independent regression fixtures | 20 |
| 3 | Exercise native modules, controls, state, packaging and compatibility | 20 |
| 4 | Resolve observed failures and strengthen native interaction evidence | 20 |
| 5 | Independently review the final source, results and handoff | 20 |

## Initial confirmed issues

1. Switch mouse presses take the double-click reset branch before toggling.
2. The pinned VSTGUI X11 keyboard translation omits Mod4/Super.
3. White-key feedback paints polygons extending underneath black keys.
4. A tooltip can time out and fail to return while moving inside the same target.

State-saving investigation retains the standard VST3 parameter queue path.
The [official communication contract](https://steinbergmedia.github.io/vst3_dev_portal/pages/FAQ/Communication.html)
requires a host flush when processing stops while the plug-in remains active.
A stale component state without that delivery is a host limitation, not evidence
that a second unsynchronized parameter path is safe. Positive and negative
loaded-module observations will distinguish delivery from serialization.

## Boundaries

The previous photo and evidence remain immutable. Linux, Windows and macOS
results must identify the source actually executed. Real window tests and
headless interface checks remain distinct. No measured Rhodes fidelity,
real-device deadline performance or release qualification is implied.
