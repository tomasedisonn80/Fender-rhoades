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

A051, A052, A054, A055 and A057 refine that contract's concrete pilot matrix:
native-action calibration, geometry/load perturbations, spatial observability,
fixture influence, complex impedance and combined Stage-control sweeps.

A023–A025, A056 and A058–A060 are consolidated in
[the optional circuit/acoustic contract](../specs/OPTIONAL_CIRCUITS_AND_ACOUSTICS.md).
It fixes the required revision provenance, initialization, shared-state tests,
optical histories and acoustic path definitions without claiming those optional
subsystems are implemented or measured.

A048's payload-integrity gap now has an opt-in local verifier implemented by A053.
It hashes the exact parsed manifest bytes and actual payload bytes, records
counts/failures, rejects unsafe and nonregular paths, detects ordinary concurrent
changes, and publishes a new report exclusively. Eighteen focused tests passed.
It is not an importer, authenticity check or adversarial-filesystem sandbox; full
session and calibration acceptance remain separate.

A067 independently found the current synthetic/unvalidated claims coherent; the
README's opening now says the specimen is a target “once identified” for clarity.

A068 reproduced a verifier cleanup error that returned a publication failure
after the final report already existed. Version 1.0.1 preserves the published
result and reports cleanup separately; it also preserves an earlier publication
error if cleanup fails. Two new regressions passed with the full 20-test payload
suite. Atomic writes do not establish parent-directory power-loss durability;
that boundary remains explicit.

A061–A066 are consolidated in [the acceptance-metric contract](../specs/ACCEPTANCE_METRICS.md):
absolute level and common timing, modal overlap/decay, contact landmarks,
identifiability, correlated uncertainty, condition-level decisions and preregistered
listening. These specify future evaluators and experiments; no fabricated
measurement, physical threshold or perceptual result has been added.

A070/A071 found ambiguous worker and resume instructions in the continuation
prompt. It now branches explicitly between scoped workers and the integrator,
defines placeholder replacement, recovers/renews a matching live claim, requires
coordination before expired-lease reassignment, restricts shared-state/Git writes
to integration and supplies a reusable factual handoff template. Verification
remains proportional to the changed behavior.

A072 demonstrated that kind coverage alone permits semantically irrelevant reuse.
The operating and continuation contracts now require a
[criterion-level completion review](ACCEPTANCE_REVIEW.md), tied to a frozen task
definition and tested source/model/input snapshot. This is an explicit integrator
obligation; semantic relevance and truthfulness are not automatically certified.
The foundation's own completion review must follow this contract.

A069/A074 added 27 isolated campaign regressions and hardened malformed metadata,
count/ID bounds, status/hash consistency and report containment. Final campaign
CI will require all recorded assignments to be complete after the actual 100
workers finish. Counts and returned task names remain integrator-recorded facts,
not cryptographic model-identity attestations.

A076 identified that Python tooling had only Linux CI coverage. The workflow now
configures its contract/test job across Linux, Windows and macOS alongside the
C++ matrix. Configuration is distinct from observed results. Narrow C++ CLI
argument handling and unverified Windows Unicode behavior are documented.

A077 found no demonstrated macOS Python failure in static review and confirmed
the temporary-root precautions, while noting architecture, filesystem-alias and
toolchain coverage limits. A078 fixed a silent sanitizer no-op: unsupported
compiler families or MSVC-style drivers now fail configuration when sanitizers
are requested. A real GNU sanitizer configure retained instrumentation on all
targets; native unsupported-platform execution was not claimed.

A073/A075 closed stale-evidence recompletion: a reopen snapshots all existing
registry IDs, and each required kind needs a passing record registered after
that snapshot. Direct validation binds the retained gate to the latest reopen
event. Fifteen focused regressions cover repeated reopens, old/unattached records,
same-second registration and failed-write preservation. Prior evidence remains
immutable; new IDs alone cannot prove an actual rerun.

A080 found that local sanitizer presets lacked CI's fail-on-UB policy. All local
test presets now reject empty selections and the sanitizer preset sets the same
UBSan halt/stack-trace options as CI. No leak-detection or deadline claim is inferred
from this configuration change.

A079 found that the pickup primitive's intermodulation test did not protect the
integrated engine's modal-summation order. A081 added an independent output oracle
using all 219 modal states, documented participation weights, an expanded linkage
derivative and a separately calculated 73-branch loaded network. Six observations
including retriggers passed, and each materially distinguishes the incorrect
per-mode nonlinear conversion. This strengthens PHY-001's synthetic acceptance;
it establishes no measured instrument accuracy.

A083 found no benchmark arithmetic/JSON defect in its bounded probe, but noted
that the CI smoke test did not parse the result. A085 documented clock overhead,
excluded control work, fixed 73-key traversal and build/environment limits.
These observations do not complete RT-001 or turn an observed maximum into a
worst-case deadline bound.

A086 found five supplied research leads missing from the source registry. They
and the separately cited action/regulation chapters are now registered explicitly
as `supplied_unreviewed`, with link-only reuse status. A normalized URL comparison
found all 25 unique links in the archived research represented in the 26-source
registry (which also includes the separately inspected key-numbering chapter).
Registration does not claim those sources were reviewed or grant reuse rights.

A082 confirmed valid IEEE-float WAV serialization but reproduced a dangling
symlink bypass of the renderer's overwrite check. A084 now stages, checks close
and publishes complete output atomically, refuses final symlinks, and preserves
existing bytes unless regular-file replacement is explicitly requested. Five
focused CLI cases passed, including the new format/path-preservation test and
both actual symlink branches on Linux. Parent directories remain trusted; no
hostile-race or power-loss guarantee is claimed.

A087 added a parsed JSON benchmark integration check: typed metadata, escaped
labels, counts, silence, one-observation statistics, exact deadline arithmetic
and invalid-zero-block behavior. Its direct CMake-script run passed; it is now
registered as `benchmark.json` in CTest without adding a Python build dependency.

A090 found the same worker/integrator ambiguity at the workflow's entry point.
The workflow now states the role exception before its first claim example,
explains recovery of a matching live claim and links the acceptance-review
contract before registration. The core/CLI versus future plugin boundaries remain
explicit and unchanged.

A091 clarified that immutable evidence links and version-1 metadata discovery are
different mechanisms. The acquisition contract now names the current discovery
rules and requires a coordinated configuration/identity/index migration; arbitrary
JSON archives must not be placed in the active session tree.

A092 found a future diagnostic-counter requirement incorrectly placed under the
current runtime boundary. It now states the actual Boolean API and labels richer
diagnostics as future work. A deterministic arithmetic-fault recovery fixture
remains unimplemented and explicitly unverified. A093 found the inspected unit,
port-polarity and energy conventions internally consistent within their declared
synthetic/reference scopes.

A094's fresh-reader review found the research/status boundary accurate. README
now qualifies task availability by software prerequisites and names the Windows
sanitizer-preset exclusion; CHANGELOG distinguishes CI configuration from observed
job results.

A089 closed malformed-input and Git-metadata path gaps in the project/campaign
loaders. Invalid UTF-8, excessive nesting, duplicate keys and nonfinite numbers
now produce controlled failures; project paths reject casefold Git components,
resolved metadata aliases and control characters. Twenty-one new regressions
preserve the existing revalidation rules. Trusted, quiescent directories remain
required; this is not protection against hostile path races or hard-link aliases.

A095 found canonical task paths narrower than their acceptance criteria. The
foundation now explicitly includes its C++/CMake/CI and root guidance files;
specimen/acquisition tasks include the schema, validator, evidence and regression
paths needed for their coordinated version upgrade. Scope additions do not alter
acceptance, dependencies or external-input blockers. A096 found one roadmap table
cell missing SPEC-001's FND-001 dependency; it now matches canonical state.

A097 found the supplied action-noise boundary missing from maintained contracts.
The optional-acoustics specification now requires identified acoustic/microphonic
transmission and excludes arbitrary direct-output noise injection. A098 clarified
the first RT-001 slice as a synthetic full-call harness with independent numerical
and runtime-policy gates; host/device evidence remains later work. The contact
contract now assigns the prerequisite fixture/tolerance freeze to the integrator.
PHY-002 and RT-001 scopes include the headers, build targets and relevant guidance
needed to implement their stated next steps without ambiguous write authority.
