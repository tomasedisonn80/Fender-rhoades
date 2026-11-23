# Review resolutions

This file is maintained by the integrator. Individual agent reports remain
immutable once the campaign ledger records their hashes. A completed assignment
does not mean that it approved a release or verified a physical instrument.

## Foundation decisions

| Concern | Resolution | Remaining boundary |
|---|---|---|
| Unknown specimen and component values | Unidentified profile with explicit nulls and provenance | SPEC-001 requires a real specimen and evidence |
| Synthetic output mistaken for fidelity | Research labels, separate evidence kinds, measured gates | No hardware accuracy claim |
| Agent progress becoming stale | Canonical task DAG and deterministic generated status checked by CI | Agents must honestly record evidence |
| Concurrent edits | Explicit file scopes, task ownership, serialized integration | Repository-wide Git operations belong to integrator |
| Unlicensed research code | Original implementation; source links only | Project license remains an owner decision |

## Implemented corrections

| Reports | Finding | Integration outcome |
|---|---|---|
| A004 | Component inventories were skipped; calibration units and split identity could disagree | Validator now checks inventories, source/task/session references, scalar units, fit-only parameter evidence, condition splits and duplicate observation hashes; 23 focused data tests |
| A005 | Empty test selection and recoverable UBSan diagnostics could appear green | CI now uses `--no-tests=error` and halts on undefined behavior; multi-configuration Linux build tested |
| A006 | Extreme finite source/load values could yield successful zero from an overflowing ratio | Scaled mantissa/exponent divider preserves representable output; new boundary, atomicity and divider regressions |
| A006 | Build documentation and CMake minimum differed | CMake and presets now declare 3.24 |
| A007 | Tool-managed mutable state could be registered as immutable evidence | Canonical state/evidence/generated dashboards and lock files are rejected by resolved artifact path |
| A007 | Unicode claim token raised an uncaught comparison exception | Tokens are compared as UTF-8 bytes and invalid ownership returns a controlled error |
| A007 | Crash between state and dashboard writes needed explicit recovery | Regression proves canonical state survives, stale dashboard is detected, and sync restores it without a new revision |
| A008 | Numerical/API conventions needed a single precise reference | `docs/NUMERICS.md` specifies equations, units, energy, observation timing and fault behavior |
| A009 | Early runtime risk needed a reproducible fixture | Synthetic callback benchmark added with explicit scope, percentiles, observed maxima and deadline comparisons; RT-001 remains incomplete |

## Open scientific implementation requirements

A011 requires PHY-002 to freeze signed compression, reciprocal participation,
contact/clamp event rules, action-energy accounting and convergence tolerances.
These are next-phase implementation requirements. The present kernel still uses
an explicitly synthetic impulse and switched-loss surrogate.

Reviews A011–A015 are consolidated in [the contact specification](../specs/CONTACT_REFERENCE.md)
and the requirement to record mechanical-boundary configurations before fitting
modal assets. Reviews A016–A020 are consolidated in
[the transduction/network specification](../specs/TRANSDUCTION_NETWORK.md), including
terminal signs, reciprocal energy, domain-limited smooth maps, complete branch
membership, and asymmetric loading fixtures. These are specifications for the
future identified reference, not completed physical implementations.

A021 exposed a roadmap gap for passive Stage controls. The canonical task graph
now includes `STG-001`, the inventory assigns volume/bass controls to it, and
[ADR 0004](../adr/0004-explicit-stage-output-gate.md) records the change. A022's
profile-isolation tuple is part of that acceptance contract. Optional Suitcase
work now depends on this explicit Stage gate.

Subsequent review findings and integration outcomes are appended here or in the
consolidated implementation specifications. Individual reports remain available
through the campaign index.
