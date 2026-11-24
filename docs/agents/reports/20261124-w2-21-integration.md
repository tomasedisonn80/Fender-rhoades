# Wave 2, assignment 21 — contact peak implementation and integration

The independent four-fixture contact peak diagnostic is implemented and exercised
against retained native data. The final current-source replay passes the default
four-fixture collection and the separately requested damped timing comparison.
**PHY-002 remains incomplete.** This is a bounded synthetic diagnostic, with no
claim of measured Rhodes fidelity or general continuous-extrema certification.

The governing contract is [CONTACT_ANALYTIC_PEAK_SLICE_20261123.md](../../specs/CONTACT_ANALYTIC_PEAK_SLICE_20261123.md).
The new [usage guide](../../CONTACT_PEAK_DIAGNOSTIC.md) provides generation,
validation, output-retention, and platform-aware capture examples. The
[wave ledger](../../../project/wave2-agent-ledger-20261124.json) records the
actual participants and immutable report identities; final ledger closure follows
the closing audit. The coordinator owns this report, the comparator/CLI, and
integration bookkeeping. Root owns Git publication and canonical project state.

## Implemented behavior

`contact_peak_schema.py` exposes the frozen `ParsedCandidate` and bounded
`CandidateFormatError` interfaces. It validates exact native fieldsets, duplicate
keys, finite nonboolean numbers, nested arrays and diagnostics, chronological
samples/events, initial mechanical state, enabled/disabled semantics, and the
contract's size/count/depth limits. Unsupported fixtures still receive structural
validation. Valid producer failures keep their partial trajectories and failure
codes; omitted peaks remain distinct from supplied malformed peaks.

`contact_peak_oracles.py` constructs references only from the independent fixture
definitions. It provides conservative closed forms, a distinct damped force root,
a forced-clamp zero-force plateau, and rounded-preload exact-rational enclosures
with verified branch margins and outward-rounded endpoints. Scalar values need
only the standard library. Requested damped timing uses the module's own SciPy
worker with an actively enforced ten-second parent timeout, evaluation limits,
three declared quadrature refinements, and explicit unavailable/inconclusive
statuses. A timing failure preserves resolved scalar values and root evidence.

`validate_contact_peaks.py` reads candidate files as data. A declared producer
file is hashed and is never launched by the CLI. Comparison preserves separate
value, time, observation-consistency, coverage, and validity findings. It uses the
unchanged budgets below, including conservative interval decisions:

| Observable | Frozen value budget |
| --- | --- |
| Compression | `1e-8 m + 1e-4 * abs(M)` |
| Force | `1e-4 N + 2e-4 * abs(M)` |
| Isolated peak time, supplemental only | `1e-7 s + 1e-4 * abs(t_ref)` |

For a verified nonnegative value interval `[L,U]`, sufficient pass uses the
maximum endpoint error and `B(L)`; definite failure uses distance to the interval
and `B(U)`; the remaining case is inconclusive. A claimed maximum must also cover
exported observations and the native sampled diagnostic within the observable
budget. Independent peak time need not lie inside the candidate replay bracket.
Whole zero plateaus and disabled observables retain different semantics.

The CLI creates a new output directory exclusively, retains numbered raw files,
per-case records/comparisons, references, plan, versions and source identities,
and aggregates exits with **3 > 1 > 2 > 0**. It uses bounded reads while consuming
input, including one-byte limit lookahead, and refuses final symlinks and special
input files. Partial writes retain the observable prefix identity when inspection
is possible; unknown retained sizes stay explicit. Input collection charges are
separate from actual retained output bytes.

## Defects and defenses resolved during review

| Finding | Resolution and evidence |
| --- | --- |
| Plan hash omitted the stored newline | The manifest now hashes exact `plan.json` bytes. W2-25 retained the failing assertion and targeted passing rerun. |
| Surrogate-escaped POSIX paths could fail JSON encoding | Canonical CLI metadata uses ASCII escaping. W2-33 exercised actual Unicode/space and supported POSIX surrogate paths. |
| Earlier timing levels and unrepresentable numbers bypassed validation | W2-32 exposed malformed earlier levels, exponent overflow, and a huge final integer. Author 23 added all-level numeric/setting/count/refinement checks and local failure handling. Four targeted regressions passed. |
| Partial raw writes were unindexed and overcounted | W2-34 wrote a 17-byte prefix before injected storage failure. The corrected record retains its size/hash and incomplete status; total retained bytes became the actual 2,978 instead of 5,922, while the consumed-input collection limit remained exhausted. |
| Temporary cleanup masked the original manifest failure | Primary write/replace exceptions now survive cleanup failure, with a cleanup note when supported. W2-34's paired-failure regression passed. |

The default damped hammer times remain `not_available` when unrequested. Positive
preload damper values retain `verified_enclosure`, while their maximizing times
remain `unresolved_near_equilibrium`. Neither status is silently converted into a
resolved time. Default completion includes values, validity and available analytic
timing; the flag additionally requires damped timing. Every comparison and attempt
manifest retains `phy002_acceptance: incomplete`.

The private-worker review in [W2-37](20261124-w2-37-transport-contract.md) found
no remaining contract-relevant transport gap. The candidate exact-field and
duplicate-key rules stay scoped to native input; this work adds no unrelated
strict schema for the fixed internal worker.

## Current-source verification and exact identities

The coordinator ran the seven new test modules together after the production
repairs, using the repository root and `PYTHONPATH=tools:tests/python`:

```sh
python3 -m unittest -v test_contact_peak_schema test_contact_peak_oracles test_contact_peak_comparison test_contact_peak_cli test_contact_peak_backend_boundaries test_contact_peak_portability test_contact_peak_retention_errors
```

Result: **74 tests passed, no skips, in 2.966 seconds**. The outer subprocess
measurement was 3.194278363 seconds. All three production modules and seven test
files had identical before/after hashes. No UTC start/end was captured for this
unit-test command. Its complete log and metadata are retained in the final replay
archive rather than being reconstructed from these counts.

The tested production identities, also unchanged during both final CLI replays:

| Module | SHA-256 |
| --- | --- |
| `tools/contact_peak_schema.py` | `af5cbf87d155ecb40c176b78a7527b2a7bd686c7fd515be621af93d910db86c4` |
| `tools/contact_peak_oracles.py` | `74030851ce9511cd50c6952a11b43bfdf5e8da5d7fd10428c009be0210060789` |
| `tools/validate_contact_peaks.py` | `5bfc879d0a4a42d48667cc2370bafc85a2bd6c6b08c18071c1d60ab7b85be717` |

The initial 74-test cohort's exact test identities are preserved here and in its
archived before/after metadata:

| Test module | Methods | SHA-256 during the 74-test run |
| --- | ---: | --- |
| `test_contact_peak_schema.py` | 22 | `8556d104c5492257c2be33e022040c66429dd447b832ea116d51dd571ac2edec` |
| `test_contact_peak_oracles.py` | 15 | `9de8d7907b7a68fccf77788d9f04c93d4059d343bb4e6b1bacdc1b26bc2be1e6` |
| `test_contact_peak_comparison.py` | 15 | `9f24401f7b5dc4f115ec8ce6a62589c0811e33e9716a347c890561c92f38ef2e` |
| `test_contact_peak_cli.py` | 8 | `02cdf8a827c67962ea1b52307debe30ddee4e8e8082a487eb8a537c1d6865450` |
| `test_contact_peak_backend_boundaries.py` | 4 | `5868ec0c2428c2105beeec80b116efb4b3b0c4f4a664801c7ee65e50039d1166` |
| `test_contact_peak_portability.py` | 3 | `df60f3cb649d19a5d16f6a63aedb301b7b3001e680b6023f08af20f2efaa6a32` |
| `test_contact_peak_retention_errors.py` | 7 | `9e899b5a5d5c4d19dca7f99cb7a0dc99c3067b3620d3c7dd3c127b78954911e9` |

### Later root-owned test portability corrections

Native CI subsequently exposed two test-fixture assumptions. Root changed only
the worker fixture's Unicode stderr emission to explicit UTF-8 bytes and the
surrogate-filename fixture's capability handling. Unsupported fixture creation
now skips only on `UnicodeEncodeError` or `errno.EILSEQ`; unrelated I/O errors
still fail, and the validation assertions remain intact. These are later test
revisions, not changes to the production hashes above.

Root then supplied this exact repository-root command and its original log:

```sh
python3 -m unittest discover -s tests/python -p 'test_contact_peak_*.py' > build/contact-tests-portability-fixed-20261124.log 2>&1
```

Root observed **85 passed, no skips, in 2.853 seconds**, exit 0. The breakdown is
**74 new tests plus 11 pre-existing peak-sweep compatibility tests**. The sweep
file predates this wave and was already exercised by W1-08. Root took the hash
listing immediately after its command but did not take a pre-run snapshot, so
this report does not claim before/after test-source equality for that rerun.
W2-39 retained the original root log, command, attribution and this limitation.

| Later test identity | SHA-256 |
| --- | --- |
| `test_contact_peak_backend_boundaries.py` | `63c684d569bdbef4bbad3ce6663e1a46a79786af18a4ef7a7e740bd5188049d1` |
| `test_contact_peak_portability.py` | `14004d860445a1a6075228f641d72fca16ff8b24c55bed9702a01bec21922388` |
| Existing `test_contact_peak_sweep.py` | `69bc0ade362497ec42ba9833cc64e772aa87fcd00a752f6fd1ca188fb379169d` |

The 74-test log SHA-256 is
`e038627be7245eeb7857a6d5506579d7548cb8122b1d101c26bbcb468c812bac`.
The root 85-test log SHA-256 is
`b758e7726aa36146cd62bd841c44ae31b3819beedd5a808eb0c5dc59879f94d0`.

## Native and independent numerical evidence

[W2-27](20261124-w2-27-native.md) generated seven actual native inputs and retained
all original streams, invocation records and CLI evidence. The intentional
conservative failure returned native exit 2, failure code 9 and one initial
sample. The diagnostic preserved it as producer failure, exit 1. Missing peaks
and the unsupported `evolved_relaunch` input each remained exit 2; their mixed
collection correctly exited 1.

[W2-39](20261124-w2-39-current-replay.md) reused exact archived native bytes with
the repaired source, without executing the producer or rerunning test suites:

| Current-source replay | Outcome |
| --- | --- |
| All four supported native fixtures, default | Exit 0; all 16 values and observation checks pass; unrequested damped and unresolved preload timing stay visible. |
| Damped fixture with requested SciPy timing | Exit 0; both distinct hammer times pass; SciPy 1.17.0 available. |

All four default reference/comparison objects match the earlier evidence exactly.
The requested timing differences are confined to measured parent/worker elapsed
durations. Source hashes remain unchanged. The original archive stays immutable.
The new archive retains both current replay attempts, exact inputs, current
source identities, and both historical test cohorts.

[W2-29](20261124-w2-29-damped-crosscheck.md) independently integrated the two-state
relative contact ODE at two declared DOP853 tolerances, using only independent
fixture definitions. Its 349/673 RHS evaluations located one accepted force,
compression and separation event per run. The fine results were:

| Quantity | Independent numerical result | Absolute difference from public reference |
| --- | ---: | ---: |
| Maximum compression | 0.0001898828808852406 m | 7.50810e-18 m |
| Compression maximizing time | 0.0003450597379600364 s | 3.80555e-17 s |
| Maximum force | 6.191807278632382 N | 2.77582e-11 N |
| Force maximizing time | 0.00017769056225740937 s | 5.26651e-17 s |

All eight reference comparisons and four refinement differences fit 0.1 of the
applicable frozen value or supplemental time budgets. This is an independent
numerical cross-check, not a certificate from dense interpolation or measured
hardware.

[W2-36](20261124-w2-36-evidence-audit.md) independently checked the original
104-member archive, all 103 indexed payload hashes, source/definition/plan links,
eight native-to-CLI byte matches, retained exits, and the DOP853 embedded script,
12 calculations and six event records. It found no material mismatch. The final
replay archive has 78 verified safe regular members. The closing W2-40 audit
checks the integrated handoff and ledger without repeating experiments.

| Retained artifact | SHA-256 |
| --- | --- |
| Original `contact-peaks-native-20261124.tar.gz` | `b3ae5198fd610820cb516426e7f84dcc921035dc89ce42282fde530cd7175cec` |
| Damped `contact-peaks-damped-crosscheck-20261124.json` | `0bc2b5fbd5da891989de8afd595a7055224a80fd271a7aa291d50d487dc841a0` |
| Final `contact-peaks-final-replay-20261124.json` | `dfbe13724511c0437e2e6a266d1815360df35791608b6b0be5e4f0f29b60a879` |
| Final `contact-peaks-final-replay-20261124.tar.gz` | `fc48780e653fb4bdbd97dab153461797d2d7607086c8a9821e8f44f4e207e8e1` |

Artifacts are under [docs/validation](../../validation/contact-peaks-final-replay-20261124.json).
The original native archive retained three failed `strace` launches caused by
kernel ptrace restrictions. The successful attempts used a narrower inherited
Python audit guard. Neither those records nor producer-file hashing constitute
OS process-tree or binary execution attestation.

## Bounded adversarial and resource findings

W2-26 exercised 85 deterministic synthetic variants: 69 bounded format rejections,
16 expected accepted statuses, no mismatches or uncaught exceptions. W2-35's one
real-size probe opened a 2,952-byte file and grew it after descriptor metadata
capture to 33,554,433 bytes. The CLI returned input-limit failure and retained
exactly the permitted 33,554,432-byte prefix, with incomplete metadata and no
comparison or whole-input hash claim. The prefix SHA-256 was
`505959696cd95af294439988e577dc52813c27fea244bb7c1efc3a6462988337`.
A separate 65-argument misuse exited argparse 2 before input reads/output creation.
No full 128 MiB collection or valid maximum-row trajectory was executed.

The one-byte limit lookahead is explicit and distinct from charged candidate
prefix bytes and retained output bytes. The limits do not assert a whole-process
RSS ceiling. I/O failure tests used controlled injection, not a physical disk-full
or crash-durability experiment; inability to inspect a partial artifact remains
explicit rather than producing an invented size/hash.

## Preserved failures, scope, and wave handoff

Earlier red regressions remain recorded in their original reports. W2-30's
optional Fraction probe verified initial branches/nonzero modal residuals, then
failed a reviewer-only assertion demanding positive compression curvature and
force slope. That sign demand is not part of the enclosure proof; no unprinted
sign was invented and no rerun erased the failed attempt. The static enclosure
review found no source defect. The other proof/domain reviews and their limits
remain in their separate hash-bound reports.

Timing coordination is also retained. The coordinator's preliminary four-case
CLI ran from 2026-11-24T05:20:35.541449+00:00 through
2026-11-24T05:20:35.594737+00:00. Its preceding native-generation smoke had no UTC
capture, so potential benchmark-start overlap is uncertain. W2-26's approximately
75 ms probe also lacks UTC start/end, with possible startup overlap explicitly
uncertain. Workers paused new tests during root's requested captures and resumed
on its signals. The final evidence does not recast those observations as isolated
performance measurements.

The coordinator spawned exactly 19 distinct child agents, assignments 22–40, in
addition to its own assignment 21. Follow-up turns reused the existing 23/26
agents; they were not extra spawns. At most two children were active together.
Each worker has its own report in the ledger; no planned or unspawned entry is
counted as complete. No further Wave 2 agents will be spawned.

W2-31's read-only macOS diagnosis is historical evidence. Root subsequently
retained the native FIR contraction experiment and successful platform results in
[native-platforms-20261124.md](../../validation/native-platforms-20261124.md).
That root-owned platform work is separate from this coordinator's Linux
experiments and the contact scientific acceptance gate.

The diagnostic adds no production solver change, frozen budget amendment, or
canonical task completion. Root may register these bounded results and continue
development on main. General continuous-extrema coverage, unresolved positive
preload timing, optional numerical error certification, and measured specimen
inputs remain outside the passing slice. **PHY-002 remains incomplete.**

## Closing audit and coordinator checks

[W2-40](20261124-w2-40-closing-audit.md) completed the independent closing audit
and found no material contact-slice integration blocker or material inconsistency
in this summary. It verified the 18 finalized worker reports then available,
current source identities, both evidence archives, test attribution and acceptance
limits while correctly leaving coordinator/auditor ledger closure pending.

The coordinator ran `python3 tools/check_docs.py`: documentation integrity passed.
The final ledger closure computes the two remaining report hashes and verifies
all 20 entries, identities, paths and report digests; its completed status denotes
completion of bounded agent work. **PHY-002 remains incomplete.**
