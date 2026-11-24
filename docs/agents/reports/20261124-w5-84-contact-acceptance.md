# W5-84 — Finite-contact acceptance review

Agent: `/root/w5_84_contact_acceptance`

**Decision: PHY-002 remains incomplete.** This is a read-only criterion review,
not a new numerical experiment or canonical state transition.

## Frozen definition

The exact criterion text below, task ID `PHY-002`, dependency `["PHY-001"]`,
required evidence `["numerical"]`, and target **One documented 73-key Mark I
Stage specimen at its specified loaded electrical output; identity and
measurements pending.** reproduce definition SHA-256
`e51dfa484a4a2f600fa3b6eaf357a8a2a85bfade777745ceb55fd0af8ba4ee03`
using the prescribed sorted, compact UTF-8 JSON projection. PHY-001 is done;
state revision 27 leaves PHY-002 in progress and fidelity unvalidated.

| Exact criterion | Evidence and coverage | Limitation and decision |
| --- | --- | --- |
| **PHY-002/C01:** Use finite hammer mass, reciprocal contact forces, separation and recontact with persistent resonator state. | Current `observe_unchecked` applies `-F_h/m_h` to the hammer and projected opposite force to the two modes. Relaunch preserves resonator coordinates and velocities. The source-bound [branch-split audit](../../validation/contact-split-depth1-20261123.json) includes supported impact/recontact, relaunch, event-order and rescaled fixtures with independent sampled comparisons. | **Pass within the synthetic offline p=1 reference.** No calibrated action or plug-in contact implementation follows. |
| **PHY-002/C02:** Model damper/pedal contact and resting equilibrium; identify or label all assumed contact parameters. | The same retained matrix covers preload perturbations, withdrawal/advance, moving faces and signed actuator work. Current `fixed_damper_equilibrium` assembles modal compliance and checks separated hammer conditions; the header and frozen contract label parameters synthetic. | **Pass within the prescribed-face model.** This is not a complete mechanical pedal/key assembly. Rounded preload has bounded positive peak values, not an exact positive plateau. |
| **PHY-002/C03:** Pass energy, timestep convergence, retrigger, release and pedal stress checks with explicit numerical tolerances. | The branch-split source passed its declared 338-setting depth-1 refinements with zero missing slots and zero independent numerical failures. The [four-fixture replay](../../validation/contact-peaks-final-replay-20261124.json) separately passed requested value comparisons and supplemental damped timing. | **Incomplete.** Ten original-grid refinement failures remain in that source's result; all 338 general-audit runs lack independently accepted continuous extrema. Four analytic fixtures do not cover the other fixture families. |

## Source and result applicability

The current solver/header and main independent auditor exactly match the
branch-split record. Its historical predecessor had **26** original-grid failures;
the earlier depth-1 experiment retained **five** `event_after` failures, with
ratios around 0.825 exceeding the unchanged 0.8 rule. The repaired source then
reported **10 original-grid failures and zero declared depth-1 failures**. These
are distinct observations, not 26 current failures or a universal convergence pass.

Read-only comparison against `4f565f0` shows later producer changes only detect
stdout failure. Runner changes add opt-in peak collection/validation and portable
manifest paths. They do not turn the historical matrix into a fresh run. The
four-fixture archive used the earlier oracle hash `74030851…`; the separate
[UTF-8 result](../../validation/contact-worker-utf8-20261124.json) binds current
`e691093a…` transport and a retained damped-input replay. Producer hashing does
not prove who generated arbitrary input. Positive preload timing remains
`unresolved_near_equilibrium`; supplemental peak timing changes no frozen gate.

## First bounded next experiment — proposed, unexecuted

Predeclare the `supported_impact`/`supported_impact_scaled` pair before extending
the independent extrema implementation. Use the existing
[extrema design](../../specs/CONTACT_EXTREMA_AUDIT_DESIGN_20261123.md) R0/R1/R2
settings: maximum steps 5/2.5/1.25 microseconds, relative tolerances
1e-10/1e-12/1e-13, and its exact absolute vector/multipliers. A new independent
reference module must retain branch endpoints, stationary compression/force
roots, both physical-RHS and dense-polynomial candidates, and unresolved coverage.
Do not silently enlarge the adopted four-fixture contract.

After recording a current producer build identity, generate the 26 diagnostic
candidates with this existing command:

```sh
python3 tools/run_contact_sweep.py --executable build/vst3/rhodes_contact_reference --fixture supported_impact --fixture supported_impact_scaled --extra-step-levels 1 --locate-peak-candidates --output-dir build/contact-extrema-supported-next
```

Retain every result. Require both reference refinements within 0.1 of the frozen
compression `1e-8 m + 1e-4*abs(M)` and force `1e-4 N + 2e-4*abs(M)` budgets,
preserving coordinate-rescaling checks and the 0.8 convergence rule. This pair
tests coupled motion and recontact beyond the analytic slice. Its finite search
still needs a coverage review; action-side and sinusoidal-damper families remain
separate subsequent work. No new extrema-module CLI is claimed to exist.

## Review procedure and identities

Required `project.py validate`, `sync --check`, and `next --json` passed: 14 tasks,
22 evidence records. Python JSON/SHA-256 inspection checked definitions, counts
and source applicability; read-only Git comparison inspected the two later tool
deltas. No build, test suite, numerical sweep, archived command, Git mutation or
shared-state edit ran. Only this report was created. Hashes establish byte
identity, not experimental authenticity or physical fidelity.

| Examined path | SHA-256 |
| --- | --- |
| `src/contact_reference.cpp` | `0f9808442b5ddf6ddc6091c9b6294117b7aea85cd54707b9c3743121ac706100` |
| `include/rhodes/contact_reference.hpp` | `00b99e61dc73686c22675bedf647c3ad1b27ba13332180431f5a42e485057fda` |
| `tools/validate_contact_reference.py` | `c910da5da6b6ad957b709a414cef39a08ed6e8b75c6e36c9d73ef535a064dcdc` |
| `tools/run_contact_sweep.py` | `81b45563f7409b4c6378a7867d7b8d33394676482c14e165f9919c1bc40e627a` |
| `tools/contact_peak_oracles.py` | `e691093a11ef34ea68cbbe25614d42a2dbd4f4576535c2c35b289133e8fc2c50` |
| `docs/specs/CONTACT_ACCEPTANCE_20261123.md` | `c86669723c14c64864b04702780679f7a32f2416920f6fe198375acc4de34073` |
| `docs/specs/CONTACT_REFINEMENT_EXTENSION_20261123.md` | `839797b0fe6e1c63651085ef3b5aef43cf4a6bfb75abd15b8f91ceb61b6dd460` |
| `docs/validation/contact-extension-depth1-20261123.json` | `8b1cb0a9b197f6263d6139d15c0b6282fe83fbe2c433e258a4fe6fb78ef52c13` |
| `docs/validation/contact-split-depth1-20261123.json` | `37e4280a43e448335ef2af3f2a30e7b992cf8c67a624cb54c3f676f3841305fc` |
| `docs/validation/contact-peaks-final-replay-20261124.json` | `dfbe13724511c0437e2e6a266d1815360df35791608b6b0be5e4f0f29b60a879` |
| `docs/validation/contact-worker-utf8-20261124.json` | `9b1e313ade2bb2bca127d7f312690c6be030ab6c7ecc1b1454b2590b3d2b1ffd` |
