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

## Numerical and runtime specifications

A026–A030 are consolidated in [the reference-solver contract](../specs/REFERENCE_SOLVERS.md):
assembled equilibrium, separate timestep/solver/event refinement, grazing and
branch-clamp cases, a cancellation-resistant energy ledger and nonlinear alias
validation. They are future coupled-reference gates; the current exact modal
primitive is not promoted into a completed contact solver.

A031–A040 are consolidated in [the runtime/asset contract](../specs/RUNTIME_AND_ASSETS.md):
bounded host events, explicit ordering, control costs, state ownership, fault
reporting, immutable assets, migration/serialization and long-tail policies.
Current call-boundary controls and Boolean process status are described honestly.
No host event API, serializer, asset loader or deadline guarantee is claimed.

A042 confirmed the scoped DAG ordering and the availability of PHY-002/RT-001
once analytical prerequisites complete. A043 clarified that leases coordinate
records within one authoritative checkout; they cannot stop a stale process or
provide distributed fencing. Reassignment requires acknowledged stopped writers
and preservation/reconciliation of their changes.

## Evidence and acquisition contracts

A041 showed that an arbitrary hashed artifact could be declared a measurement
without binding its contents to the specimen/session contracts. A044 added typed
profile/session paths, immutable linkage, an independent `specimen_identity`
kind, measured-session checks and exact same-profile/session coverage for held-out
validation. Its integrated run passed 88 Python tests. SPEC-001 now requires the
identity kind; its physical-access blocker remains. Metadata consistency is not
an attestation that an experiment actually occurred.

A045 found that JSON Schema's end-of-string anchor admitted a trailing newline
that the Python validators rejected. Data schemas now use an explicit absolute-end
guard; focused regressions cover identifier and digest suffixes. Unknown values
remain null rather than invented zeroes.

A046–A050 are consolidated in [the acquisition contract](../specs/DATA_INGESTION.md).
Version-1 metadata validation does not resolve calibration validity, raw payloads,
configuration history or derived-data ancestry. These limitations are documented
at the data entry point and remain explicit gates before real measured acceptance.
A050 also corrected the source-ledger summary to distinguish historical tonebar
stamp sequences from the project's normalized key mapping.
