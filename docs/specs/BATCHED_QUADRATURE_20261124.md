# Batched quadrature observation contract — 24 November 2026

The frozen callback matrix exposed substantial quadrature cost at high sample rates and very small blocks. This candidate reduces repeated state loads in the existing six-node observation. It introduces no new physical model, parameter, filter, latency or saved-state identity.

## Invariants

- Retain the same six prepared fractional transitions, two damping regimes, three modes per key, and all 73 electrical branches.
- Store the private transition plan with the six node coefficients contiguous for each matrix component. The public plan remains opaque and fixed-size.
- Add `ResearchEngine::observe_cell(plan)` to load each modal state once while evaluating all six nodes. For each individual node, preserve expression grouping, mode order, nonlinear pickup conversion after modal summation, and harp summation order.
- Keep independent node arithmetic in fixed-size loops, validate all modal results before accumulating them, and retain ordinary compiler floating-point semantics. No fast-math flags, platform intrinsics, reassociation or assumed vectorization are introduced.
- Keep scalar `observe(plan, node)` as an independently traversed compatibility check and diagnostic interface. Invalid/default/mismatched plans return no result; observation does not mutate mechanics.
- Keep the original endpoint observation/advance and FIR algorithm. The optimization does not remove arithmetic-fault checks, skip quiet pickups, or alter event ordering.
- `BasicQuadratureEngine` publishes a frame only after valid cell observation, successful endpoint advance and FIR insertion. Partial/fault behavior, model ID 3, format version 1 and 48-frame latency remain unchanged.

## Acceptance for this code change

1. Exact scalar-versus-batched output bits across the retained independent analytic fixtures, rates including adjacent boundary rates, held/released/pedal/retrigger states, and arbitrary valid fractional nodes.
2. Existing nonmutation, plan-rejection, fault-prefix, partition, host-event, recall, allocation and latency contracts continue to pass.
3. The full 2,097-case independent Q6 comparison retains the predeclared numerical budgets. Exact replay of all retained production float64 and stream hashes can establish unchanged outputs and therefore preserve those prior comparisons; this is compatibility evidence, not a fresh continuous-reference calculation. The 24 historical direct/filtered WAV fixtures remain byte-identical.
4. Preserve measured old/new binary and source identities. Use the existing 72-process protocol for another separately retained observation. Report each round and outlier; shared-host differences do not establish device deadlines or a causal speedup guarantee.
5. Inspect native CI before claiming Windows or macOS validation for this revision.

The original callback matrix, old source and scalar rendering are retained. Any failed numerical case or unsupported platform remains visible. This optimization does not close physical calibration or the general contact-extrema gate.
