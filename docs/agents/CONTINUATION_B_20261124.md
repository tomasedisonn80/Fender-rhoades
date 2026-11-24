# Numerical development continuation — 24 November 2026, session B

Starting revision: `a17e665454c1daa1c1aff3d71b790b28fe145e12` on `main`.
Integrator: `continuation-b-root`. The user authorized development, incremental
commits and pushes on `main`, and 100 actual workers across five waves.
The execution service permits four concurrent agents including the integrator;
each wave therefore consists of sequential batches of at most three workers.

## Work selected from the previous handoff

The previous [criterion review](../validation/research-gates-20261124.md) leaves
PHY-002 and RT-001 available. Their definitions, numerical budgets and physical
evidence requirements remain unchanged. This continuation targets:

1. Independent continuous peak diagnostics for `supported_impact` and
   `supported_impact_scaled`, including R0/R1/R2 refinement, branch endpoints,
   stationary candidates, coordinate invariance and explicit coverage limits.
2. A reproducible filtered-path error decomposition beginning with
   `r08-chord-all73-u10`, preserving onset, event order, 48-frame latency and
   all existing thresholds.
3. Implementation improvements justified by these results, with compatibility,
   failure handling and native build verification appropriate to each change.

Contact remains an offline reference unless a separately bounded audio algorithm
is developed and verified. Public papers and factory documents are background
sources, not measurements of the target instrument. No unavailable specimen,
calibration, physical fidelity, DAW qualification or release is assumed.

## Five waves

| Wave | Assignments | Purpose |
| --- | --- | --- |
| 1 | 001–020 | Audit public references and implement the first bounded diagnostics, optimizations and regression checks |
| 2 | 021–040 | Implement independently specified diagnostics and targeted regression checks |
| 3 | 041–060 | Execute retained experiments, investigate failures and improve implementation |
| 4 | 061–080 | Integrate, verify native behavior and review numerical and runtime boundaries |
| 5 | 081–100 | Independent acceptance review, reproducibility and final handoff |

The [actual worker ledger](../../project/continuation-b-20261124.json) records
only spawned assignments. A completed entry requires the worker's retained
report and its SHA-256. Counts describe actual work delegation; metadata cannot
certify scientific acceptance. Reports from earlier sessions do not count here.

One integrator stages and pushes changes. Workers own distinct assigned paths
and do not mutate task state. Historical evidence remains immutable. New results
will be retained under new paths, including failures and unresolved conditions.

## Development results and entry points

The first two waves completed 40 actual assignments. Their
[integration record](../validation/wave2-integration-20261124b.json) binds the
tested source and retains the complete Linux results: 301 native CTests and
848 Python tests passed with no skips. Later work retains its own source and
verification bindings; these counts describe that recorded checkpoint.

| Work | Guide or retained result |
| --- | --- |
| Independent supported-impact peak comparisons and bounded retained arrays | [Supported extrema guide](../SUPPORTED_CONTACT_EXTREMA.md) |
| Stationary pickup numerical error and phase-dependent alias contribution | [Direct alias guide](../DIRECT_ALIAS_DIAGNOSTIC.md) |
| Filtered-path endpoint, integration and normalization error decomposition | [Retained decomposition](../validation/filtered-decomposition-20261124b.md) |
| Contiguous quadrature history and never-excited observation optimization | [Output compatibility](../validation/silent-observation-20261124b.md) |
| Paired callback observations and a known build reproducing the measured binaries | [Initial capture](reports/20261124b-w2-021-callback-capture.md) · [Build reproduction](reports/20261124b-w2-035-callback-build-provenance.md) |
| Fixed-anchor grazing timestamps across the declared refinement axes | [Grazing slice](reports/20261124b-w2-037-grazing-refinement-slice.md) |

The passing supported-impact and stationary diagnostics retain their limited
scope. General contact coverage, the other historical refinement failures,
filtered/direct convergence, and instrument calibration remain separate work.
The concurrent continuation C owns the separately claimed HOST-002 MIDI work;
its commits are incorporated without replacing this campaign's records.
