# Main-branch development — 24 November 2026

This campaign follows the user's request for **100 actual agents in five waves
of 20**, with development committed and pushed on `main`. The integrator owns
Git publication and canonical project decisions. The available concurrency is
four agents including the integrator, so a wave runs in smaller cohorts rather
than claiming 20 simultaneous workers. A reused agent's follow-up is not another
participant. Every completed assignment has a separate immutable report.

## Campaign record

| Wave | Assignments | Scope | Record |
| --- | --- | --- | --- |
| 1 | 1–20 | Platform repair, batched quadrature design, baseline observations | [Completed ledger](../../project/wave1-agent-ledger-20261124.json) |
| 2 | 21–40 | Independent four-fixture contact peak diagnostic | [Completed ledger](../../project/wave2-agent-ledger-20261124.json) |
| 3 | 41–60 | Runtime preservation, evidence verification and error handling | [Completed ledger](../../project/wave3-agent-ledger-20261124.json) |
| 4 | 61–80 | Documentation, native fault coverage and evidence integration | [Completed ledger](../../project/wave4-agent-ledger-20261124.json) |

Wave 5 remains planned until its actual participants are spawned and recorded.
The ledgers contain only assigned agents. Their report digests
bind the bytes reviewed; neither a populated ledger nor a matching digest can
authenticate execution or establish scientific acceptance.

```sh
python3 tools/validate_development_waves.py --json
```

The validator checks sequential wave/assignment ranges, unique canonical task
names and reports, schema, safe files, exact hashes, and declared completion.
`--require-complete` additionally requires all five waves and 100 completed
reports; it deliberately fails while the campaign is unfinished. Earlier setup,
development, continuation and quadrature campaign records remain separate.

## Engineering evidence and limits

The [batching result](../validation/batched-quadrature-20261124.md) preserves
the full Q6 and legacy output identities while retaining every observed timing
outlier. The [native-platform record](../validation/native-platforms-20261124.md)
identifies the tested compiler/platform revisions and explicit skips. The
[contact diagnostic guide](../CONTACT_PEAK_DIAGNOSTIC.md) describes the bounded
four-fixture slice, its byte-preserving workflow and incomplete timing cases.

Changes to diagnostic tools receive later reports; historical archives and
registered reports are not edited to erase earlier failures. The canonical
[project state](../PROJECT_STATE.md) and criterion reviews determine milestone
status separately from agent counts. Finite contact remains offline, measured
fidelity remains unvalidated, and this campaign does not publish a release or
claim native DAW/device qualification.
