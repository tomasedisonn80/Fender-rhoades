# Worker 005 — never-excited key runtime audit

Date: 2026-11-24. Scope: read-only implementation review; no DSP change or
performance measurement. Root owns source integration, Git and project state.

## Finding and preferred scope

An `excited` bit is a sound **numerical-zero invariant** for the present
uncoupled synthetic engine. Initially all modal coordinates are zero; only a
successful nonzero `note_on` can inject motion. Damping changes and homogeneous
modal transitions cannot create nonzero motion from zero. This reasoning does
not extend automatically to future coupled mechanics or reciprocal forces.

Prefer the integrator's conservative variant: retain every `sample()` modal
advance, in existing key/mode order with its existing error checks, while
avoiding never-excited keys' fractional modal observations and pickup conversion
in `observe()`/`observe_cell()`. In `sample()`, avoid their pickup accumulation
and conversion while still advancing all three modes. Preserve the complete
73-element source arrays and the unchanged `harp_.voltage()` call. Its fixed
source resistance and division by four/three retain every pickup branch.

Sources: [engine implementation](../../../src/research_engine.cpp),
[API and topology](../../../include/rhodes/research_engine.hpp), and
[numerical contract](../../NUMERICS.md).

## State and floating-point hazards

- Initialize the bit false in `Key` and successful `prepare()` candidates;
  clear it only on explicit `reset()` or successful preparation. Publish true
  with the successful `note_on` candidate, after all three impulses succeed.
  Invalid notes/preparations must preserve it atomically. Default copying must
  copy it with mechanics.
- Use the existing bit-based control classification: both signed zeros release;
  every valid positive subnormal strike marks the key excited even if DAZ/FTZ or
  impulse underflow leaves numerical motion zero. Never infer excitation from
  rounded impulse, energy, held state, or output amplitude.
- `note_off`, zero-intensity release, `all_notes_off`, sustain changes and long
  tails must never clear it. Keep existing damping updates. A key remains on the
  ordinary path for the rest of its lifetime until reset.
- Skipping **advancement** is not bit-identical: near the top of the keyboard at
  32 kHz, negative diagonal transition coefficients turn initial positive zeros
  into negative-zero modal coordinates. `modal_state()` exposes those bits.
  Retaining advancement preserves them and the partial-progress fault contract.
- Even with advancement retained, the source shortcut needs a declared FP
  boundary. Under nearest rounding, modal accumulation starting at positive zero
  gives positive-zero motion and the pickup returns negative zero. Replacing
  this with positive-zero array entries leaves the ordered harp result unchanged
  under that rounding mode. Under downward rounding, opposite-signed zero sums
  can be negative: the same substitution can change output zero bits. A strict
  broader claim needs an original-arithmetic fallback for the actual rounding
  environment, or exact signed-zero emulation. `fegetround()` alone is not a
  sufficient portability assumption when a host changes MXCSR independently.
- Preserve plan/rate/node validation before any shortcut, including completely
  silent engines. Valid private finite matrices and zero state cannot produce
  a nonfinite observation; the bit must never hide genuinely excited/faulting
  state. Skipped operations may change platform denormal-operand status for
  extreme subnormal plan coefficients; the public no-FP-effects guarantee is for
  control helpers, not general audio arithmetic.

## Required compatibility evidence before integration

Compare baseline and candidate sample **bits**, all 219 modal-state pairs,
energy, optional presence and process status at identical event boundaries.
Cover startup silence; delayed first strikes at keys 0/3/4/36/72; 1/8/73-key
chords; retrigger; release; pedal before first strike and during tails;
all-notes-off; signed-zero and positive-subnormal intensities; invalid events;
empty buffers; invalid prepare; reset and successful prepare after activity.
Include 32 kHz and all retained supported-rate fixtures, varied block partitions,
reused plans, invalid/default/mismatched plans, and arbitrary valid fractions.

Require bit agreement between scalar `observe` and `observe_cell`, observation
nonmutation, and unchanged wrapper fault coordinates/zeroed suffix/latch behavior.
Exercise declared rounding modes and FTZ/DAZ combinations explicitly; preserve
the control tests' exact treatment of subnormals. Replaying only an optimized
scalar oracle against an optimized batch does not establish baseline identity.
Retain the existing direct/filtered compatibility fixtures and full Q6 replay.

Relevant existing suites: `quadrature.analytic_nodes`, `plan_domains`,
`observation_nonmutation`, `damping_regimes`, `qengine_faults`, core
silence/pedal/retrigger/network tests, and control FP environment tests.

## Benchmark design and expected benefit

Use matched source/binary identities and compiler flags, alternating A/B order,
retained raw timing distributions and documented machine/competing load. Compare
0/1/8/16/73 **distinct keys excited since reset**, at 32/48/96/192 kHz and small
and normal blocks, across direct/filtered/Q6 paths. Include a sweep that touches
all 73 keys then releases them: this deliberately earns no lasting sparse-key
saving. Keep events and reset/warmup accounting consistent with
[the benchmark contract](../../BENCHMARKING.md).

Q6 should benefit most: each untouched key avoids six three-mode observation
evaluations and six pickups per frame, while the six full harp reductions and
all modal advances remain. Direct/filtered savings are smaller; dense workloads
may only add a branch. No speedup percentage is established by this audit.

## Checks and handoff

Read `AGENTS.md`, README, project state, roadmap and relevant implementation/tests.
Ran `python3 tools/project.py validate` (14 tasks/26 evidence records valid),
`sync --check` (current), and `next --json` (revision 32, no available tasks).
No compiled tests or benchmarks were run; this report makes no runtime pass claim.
Only this report was written. Next: implement the conservative variant after
choosing the zero-sign/rounding compatibility boundary, then collect the matched
regression and timing evidence above.
