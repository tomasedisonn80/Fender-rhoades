# Optional contact peak candidates

Integrator development decision, 23 November 2026. This adds offline numerical
diagnostics without amending the frozen contact acceptance or the meaning of
existing sampled maxima. The independent continuous-extrema gate remains open.

## Scope

Add an opt-in `locate_peak_candidates` solver setting, default false, and CLI
`--locate-peak-candidates`. The ordinary reference and its existing sweep remain
unchanged when the option is absent. Keep these computations out of the audio
engine and VST3. Parameters are synthetic.

For each enabled contact, retain the largest located compression and force,
their time brackets, the selected value's time, and its origin (sample,
stationary candidate, or action side). Use fixed storage for these four records;
do not emit an unbounded history. Include start/end states and both sides of
external actions. An all-zero result is valid and needs an explicit origin.

For positive compression delta, relative velocity w, relative acceleration a,
and z = k + c*w, compression stationary candidates satisfy w = 0. On the
positive-force branch, force stationary candidates satisfy
`w*z + c*delta*a = 0`. Hammer relative acceleration subtracts projected modal
acceleration from hammer acceleration; damper relative acceleration subtracts
the prescribed face acceleration from projected modal acceleration. Force is
zero on separated or clipped branches.

## Evaluation and bounded failure

Use a separate collector that evaluates an accepted interval through one
consistent numerical trajectory: the existing two-half-step replay from its
fixed interval anchor. Re-evaluate bracket endpoints with that same evaluator.
Do not silently change the existing event detector or accepted endpoint state.
Search the four quarter-interval subsegments for sign changes or exact isolated
endpoint roots. Include all replayed sample points and refined root brackets in
the candidate maximum. A zero derivative plateau does not request infinitely
many roots. Reject nonfinite intermediate values with the existing typed failure.

All trial, RHS, root-iteration and minimum-progress limits still apply. There is
no new unbounded loop or allocation inside a root search. Failed calls preserve
the full prior state, energy ledger, events, sampled diagnostics and new peak
diagnostics through the existing transactional rollback. Opt-in work may exhaust
budgets sooner; report that failure rather than weakening the limits.

## Honest export and verification

Export an additive `located_peak_candidates` object with coverage
`uncertified_sign_change_search`, numerical evaluation method, and the four
records with SI units. Omit it when disabled. Never call it a continuous maximum
certificate: finite quarter-interval sampling can miss multiple or even roots,
and the replay trajectory has numerical error. Do not remove the auditor's
missing-extrema result or reinterpret this diagnostic as meeting that gate.

Tests should independently derive the conservative free-mass impact maximum,
find an off-grid damped force stationary point, cover the prescribed damper-face
acceleration sign, include a velocity-only action-side jump, and force budget
exhaustion with full rollback. Check default-mode byte output compatibility and
opt-in CLI schema. Any later independent peak acceptance requires its own
coverage argument and reference refinement; it is not authorized by this file.
