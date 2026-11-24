# W5-96 — Campaign closure contract

Evaluator: `/root/w5_96_campaign_closure_contract`; 24 November 2026.

**No material closure-contract defect was found. The campaign was correctly
incomplete at this review snapshot.** Root must finish actual assignments and
enable the complete-campaign CI gate before declaring the requested five waves
closed.

## Observed snapshot

Observed checkout HEAD: `93908da683abfb12979cac0044be27703cadec25`.
The table records working-tree metadata, including pending integrator updates;
it does not assert that every ledger entry already existed in that commit.

| Wave | Recorded assignment range | Recorded | Completed | Declared state |
| --- | --- | ---: | ---: | --- |
| 1 | 1–20 | 20 | 20 | complete |
| 2 | 21–40 | 20 | 20 | complete |
| 3 | 41–60 | 20 | 20 | complete |
| 4 | 61–80 | 20 | 20 | complete |
| 5 | 81–97 | 17 | 14 | in_progress |

All **97 canonical names** were unique. The **94 completed reports** had unique
paths and digests, and every report's actual bytes matched its recorded SHA-256.
Assignments 95, 96 and 97 were named running workers without claimed reports;
98–100 had no placeholder rows. The four completed waves each contain exactly
20 consecutive assignments. This is metadata reconciliation, not independent
authentication that a named worker executed.

## Executed checks and gate behavior

Required `project.py validate`, `sync --check` and `next --json` passed at
revision 30 with 14 tasks, 26 evidence records and unvalidated fidelity.
HOST-001 was done; PHY-002 and RT-001 were available with released claims.

I ran each campaign command once:

| Command | Result |
| --- | --- |
| `python3 tools/validate_development_waves.py --json` | Exit 0: 97 recorded, 94 completed, four completed waves |
| `python3 tools/validate_development_waves.py --require-complete` | Expected exit 1: `campaign not complete: require five waves of 20 completed agents` |

All ten inspected ledger, validator, test, CI and index files stayed unchanged
across these calls. No entries or hashes were generated or repaired.

Source inspection confirms rejection of missing/out-of-order waves, premature
next-wave records, incorrect ranges/IDs/counts, duplicate or cross-wave names,
reused report paths/content, absent or changed completed reports, unsafe/symlink
paths and nonregular/oversized inputs. Strict JSON rejects duplicate keys,
nonfinite numbers and invalid types; bounded reads detect observed file growth
or changes during each read. Complete status requires exactly 20 completed
records, and `--require-complete` requires all five complete waves.

The existing tests contain **24 primary and five adversarial methods**. Their
retained [full-suite log](../../validation/python-final-20261124.log.gz) records
all 29 as passing, including the final-report mutation, rehashed report replay,
nested-name reuse, escaped duplicate-key and growing-file probes. I inspected
those tests and log entries; I did not execute them again.

## Remaining integrator transition

CI currently invokes the validator in progress mode, and the campaign index
honestly describes Wave 5 as live. Leaving that invocation unchanged at closure
would allow a later valid incomplete prefix to pass this CI step. The concrete
remaining action is therefore to record each remaining actual spawn and frozen
report, finish Wave 5 at 20/20, then change the CI invocation to
`--require-complete` and update the index. Root should run the complete gate,
project/document checks and final publication/CI checks on that resulting state.
No future successful run is claimed here.

This gate cannot judge report quality, prove actual agent execution, establish
scientific acceptance or promote physical fidelity. The integrator must reconcile
actual tool assignments separately and preserve every historical report hash.
Only this report was written; no source, Git, ledger or canonical-state mutation,
build or test suite ran.

## Snapshot identities

These hashes preserve the audited snapshot; the live Wave 5 ledger, index and
CI invocation are expected to change during authorized finalization.

| Path | SHA-256 |
| --- | --- |
| `project/wave1-agent-ledger-20261124.json` | `80b481147eea3e434684bb08a07fd94b3b4fd90db145c4efbf799d9e39db9e57` |
| `project/wave2-agent-ledger-20261124.json` | `c5f0fb6b4dfc5eaa86eec9449b049033f86e669118216db5ef1ec06df8e51e7b` |
| `project/wave3-agent-ledger-20261124.json` | `9d22d4e15652c1e3b3f2164cfa5ebb148f669a444f5a54353e08cc485a18b3fb` |
| `project/wave4-agent-ledger-20261124.json` | `b627c1ade1056298879de8a9ba30bd243612b8ac66cb3a4ed8e7f682483bea86` |
| `project/wave5-agent-ledger-20261124.json` | `16b1c0903c2f5ae741c18c78dd0c8861dbd11c3a5e9232a78eddd8bbc47f7f8c` |
| `tools/validate_development_waves.py` | `5a93e12b045ddc7ee617de954f4a9cd8af625bca3bcc245e47d929bca4449e25` |
| `tests/python/test_development_waves.py` | `762424831ef9072ec516cb59072338f7a0d0d1777c90002a359372bec5ecef6c` |
| `tests/python/test_development_waves_adversarial.py` | `d9ff42c46face731f8aa66f25e39f9e73155576610b7a6a1d3538b620f8d3a47` |
| `.github/workflows/ci.yml` | `9a0d441f70dc60be6bdf7ec3b3ab5a3c5e046feed103a043633d1bd27911e94a` |
| `docs/agents/DEVELOPMENT_WAVES_20261124.md` | `f4ca0e8bc6d956e83b111cd9c135819ff2e6b4b749a8bff0322f2865dadf71b5` |

