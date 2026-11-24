# Wave 2 assignment 40 — closing contact diagnostic audit

Date: 2026-11-24. Worker:
`/root/w2_contact_implementation/w2_40_closing_audit`.
Owned path: this report only.

## Result and scope

**No material contact-slice integration blocker was found.** The three current
modules, targeted reports, usage guide, retained tests, and finalized replay
support integration of the bounded four-fixture diagnostic under the unchanged
[frozen contract](../../specs/CONTACT_ANALYTIC_PEAK_SLICE_20261123.md).
**PHY-002 remains incomplete**, including its general continuous-extrema gate.
Measured fidelity remains `unvalidated`; specimen and acquisition blockers remain.

This is a closing source/document/evidence-integrity audit. No test suite,
producer, diagnostic CLI, quadrature, DOP853 experiment, build, or remote CI job
was executed here. No child worker was spawned. Existing reports and original
native/cross-check artifacts were read without modification.

Required operating and project context was read: `AGENTS.md`, `README.md`,
`docs/PROJECT_STATE.md`, `docs/ROADMAP.md`, and canonical `project/state.json`
(revision 27). This bounded assignment did not select or claim another task.
Project-state validation, evidence registration, and publication remain with the
integrator.

## Ledger closure check

The final inspected ledger snapshot was
`project/wave2-agent-ledger-20261124.json`, SHA-256
`0efca39e752bb9b9265b52dce79fb94f7a9f288699e6c6b3e0813acb1db04fb6`.

- Exactly **20 entries**, matching `requested_actual_agents: 20`, are numbered
  **21 through 40**, without gaps or duplicates.
- Assignment numbers, IDs, task names, and report paths are individually unique.
  All entries belong to wave 2 and have the matching `W2-<assignment>` ID.
  The coordinator is entry 21; entries 22–40 name its 19 child assignments.
- All **18 completed reports, 22–39**, exist and match their ledger SHA-256.
- Entries **21 and 40** correctly remain `in_progress` with null hashes at this
  snapshot. This audit does not invent completion of its own unwritten report or
  the coordinator's report, and it does not mark the entire wave complete.

These checks validate the recorded execution ledger and report identities. They
do not turn worker counts into scientific acceptance or independently recreate
historical worker execution. The parent owns final registration of 21 and 40.

## Current source binding

The inspected implementation matches the assigned final production identities:

| File | SHA-256 |
| --- | --- |
| `tools/contact_peak_schema.py` | `af5cbf87d155ecb40c176b78a7527b2a7bd686c7fd515be621af93d910db86c4` |
| `tools/contact_peak_oracles.py` | `74030851ce9511cd50c6952a11b43bfdf5e8da5d7fd10428c009be0210060789` |
| `tools/validate_contact_peaks.py` | `5bfc879d0a4a42d48667cc2370bafc85a2bd6c6b08c18071c1d60ab7b85be717` |
| Frozen analytic slice | `56e5caefaebc03596b2ed566d50578b2be2e4dc54e37bf7fdce31dab31243f03` |
| Frozen contact acceptance | `c86669723c14c64864b04702780679f7a32f2416920f6fe198375acc4de34073` |
| `docs/CONTACT_PEAK_DIAGNOSTIC.md` | `37825a75518ddc7baa212b302af4fe403666f055deb54bb3d92dd2b9d6c3cde4` |

All **17 replay before/after source entries** match one another, the corresponding
archive snapshots, and current files. The later root-test retention source map
also matches its snapshots and current files. Historical oracle/CLI hashes in
earlier reports remain legitimate records of earlier source versions.

## Bounded contract assessment

Reviewed findings from assignments 22–38 and the finalized
[W2-39 replay](20261124-w2-39-current-replay.md) against the current source.
The assessment below combines static inspection with identified existing
evidence; it is not a new numerical test result.

The coordinator's subsequently available draft report 21 also received a final
static pass. Its production/test identities, two-cohort attribution, replay
results, recorded failed probes, and acceptance caveats agree with the inspected
evidence. No material inconsistency was found; its ledger entry was still pending
finalization at the recorded snapshot.

| Contract area | Closing assessment and evidence |
| --- | --- |
| Strict native input | The parser preserves the exact native field/number rules, bounded ingestion constants, duration/initial-state checks, and missing/unsupported distinctions. W2-22 and W2-26 cover schema behavior; W2-35 documents the actual boundary probes and their limits. |
| Independent reference definitions | Oracle parameters come from independent fixture definitions. Conservative/forced-clamp domain arguments, disabled versus enabled zero semantics, and rounded-preload value enclosures agree with W2-23, W2-28, and W2-30. W2-29 supplies the separately retained damped numerical cross-check. |
| Comparison decisions | Endpoint value budgets, separate observation consistency, supplemental time checks, and `3 > 1 > 2 > 0` aggregation remain visible. Producer failure returns failure before successful comparison; missing and unsupported evidence cannot pass. W2-24/W2-25 and the historical mixed native collection support these paths. |
| Optional damped timing | The W2-32 repair validates all refinement levels and handles numeric/transport failure locally while retaining scalar values. The module actively bounds its own worker. W2-37 found no remaining contract-relevant gap; native-candidate exact-field rules are not expanded into a new private-worker contract. |
| Exclusive retention and failures | Exact retained plan hashing, source identities, numbered raw artifacts, and explicit operational errors remain. W2-34's repaired partial-write path separates consumed input bytes from retained bytes and preserves the primary storage failure. W2-35's limits review supports the resource boundary. |
| User instructions | The new guide follows the actual CLI, separates native byte capture from validation, explains binary hashing without execution/attestation, and states optional timing, exit codes, incomplete writes, and the acceptance boundary. W2-33/W2-38 distinguish platform advice from executed platform evidence. |

W2-30's failed extra sign assertion is explicitly retained as a reviewer
assumption, not silently converted to a passed proof experiment. The existing
preload author test and later retained cohorts provide their own execution
evidence. W2-31 is a historical, separate macOS diagnosis: the later
[native-platform confirmation](../../validation/native-platforms-20261124.md)
records its repair passing at commit
`015767fee5635ff85a717739188ff4af60fe84f2`. This audit read that summary but did
not independently retrieve or validate its hosted logs; it does not treat the
older diagnosis as a current contact-slice blocker.

## Independently checked retained evidence

Read-only Python stdin checks used `json`, `hashlib`, `tarfile`, and `pathlib`;
all audit assertions completed with exit 0. Archive members were read through
`extractfile`, without extraction or execution.

| Artifact | SHA-256 |
| --- | --- |
| Final replay JSON, 54,495 bytes | `dfbe13724511c0437e2e6a266d1815360df35791608b6b0be5e4f0f29b60a879` |
| Final replay archive, 228,021 bytes | `fc48780e653fb4bdbd97dab153461797d2d7607086c8a9821e8f44f4e207e8e1` |
| Original native archive, 277,064 bytes | `b3ae5198fd610820cb516426e7f84dcc921035dc89ce42282fde530cd7175cec` |
| Original damped cross-check JSON | `0bc2b5fbd5da891989de8afd595a7055224a80fd271a7aa291d50d487dc841a0` |

The final archive has **78 unique safe regular members**, including **77 indexed
payloads**, totaling **1,346,562 uncompressed bytes**. The original archive has
**104 members / 103 indexed payloads**, totaling **1,618,006 bytes**. Every
indexed length/hash and exact member set matched; both archived `summary-core`
objects match their external summaries with the archive field removed. The
original native summary hash matches its recorded identity. The damped
cross-check hash matches the immutable identity previously audited by W2-36.

All four replay inputs equal the original native stdout bytes. Across the two
new invocations, all **five** raw artifacts, case records, candidate hashes,
reference-definition hashes, plans, manifests, and declared source identities
agree. Their retained-byte totals are **239,560** and **20,275**. All **20**
retained peak comparisons show passing values and observation consistency;
every required timing check passes. Process and manifest exits agree at **0**.
The declaration remains `executed: false` and
`candidate_production_attested: false` for the supplied binary.

Default damped hammer timing remains `not_available`; positive preload damper
timing remains `unresolved_near_equilibrium`. Those statuses are preserved
separately from passing values. No replay bracket is promoted to a certificate.
An earlier 75-member archive was observed while W2-39 was still packaging; the
counts and hashes above refer only to its confirmed final 78-member artifact.

## Retained test cohorts and remaining boundary

Both logs and their metadata are present in the final archive and their hashes
were independently checked:

| Cohort | Retained observation | Attribution limit |
| --- | --- | --- |
| Earlier 74 new tests | `Ran 74 tests in 2.966s`, `OK`; captured metadata exit 0 and equal before/after source maps | Exactly two later test-only files differ: backend-boundary UTF-8 stderr and surrogate-filename portability handling. Production identities match the current modules. |
| Later 85 total tests | `Ran 85 tests in 2.853s`, `OK`, with no skips reported; exact root command and root-reported exit 0 retained | Root ran 74 new tests plus 11 existing peak-sweep tests. No pre-run source snapshot exists for this cohort; current retention/post-run identities must not be described as before/after process attestation. |

The log hashes are respectively
`e038627be7245eeb7857a6d5506579d7548cb8122b1d101c26bbcb468c812bac`
and `b758e7726aa36146cd62bd841c44ae31b3819beedd5a808eb0c5dc59879f94d0`.
Neither cohort was rerun by this worker.

The remaining limitations are part of the declared scope: four synthetic
fixtures, uncertified candidate sign-change coverage, unresolved positive
preload maximizing times, and numerical timing refinement rather than rigorous
quadrature certification. Producer identity/initial-state consistency does not
authenticate arbitrary physical parameters. Storage failures may leave partial
evidence or an earlier manifest and therefore remain operational failures.

Next action: the parent can finalize reports/ledger entries 21 and 40 and hand
the bounded diagnostic to integration. No further source change is requested
by this audit. General PHY-002 acceptance, measured-model gates, and fidelity
status are unchanged.

This report passed a local UTF-8/final-newline/trailing-whitespace check and all
three local Markdown links resolved. The three production module hashes were
rechecked unchanged at handoff.
