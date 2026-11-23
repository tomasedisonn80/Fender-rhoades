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

Detailed review findings and integration outcomes will be appended as the setup
campaign completes.
