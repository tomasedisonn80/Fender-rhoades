# Worker 026 — supported-impact native replay

## Result and scope

**All 26 selected current-native candidates are byte-for-byte identical to the
preserved candidates used by the initial independent extrema comparison.** The
one native generation invocation returned exit 0. All 26 parsed JSON objects,
all 26 stderr streams, and the combined candidate JSON also match exactly. No
field, diagnostic, work counter, event, sample or failure code was removed or
normalized before comparison.

The result is recorded in
[supported-native-replay-20261124b.json](../../validation/supported-native-replay-20261124b.json).
It binds these observed outputs to the current producer supplied by the
integrator after the `caed228` anchor-based grazing change. This worker recorded
the source and binary identities independently but did not rebuild the binary or
run Git. Source paths alone were not used to infer output compatibility.

The [initial independent-reference comparisons](../../validation/supported-extrema-initial-20261124b.json)
therefore apply to identical candidate bytes produced by the selected current
native runs. **No SciPy reference was recomputed.** This replay does not execute
the full 338-run matrix, establish the original convergence/decrease gates,
certify continuous extrema coverage, resolve the earlier refinement failures,
or validate physical fidelity. **PHY-002 remains incomplete.**

## Frozen inputs and exact comparison

The baseline is `build/supported-extrema-candidates`, generated with
`build/baseline/rhodes_contact_reference`. The new generation is isolated in
`build/w2-026-supported-native-replay/candidates`. Both select only
`supported_impact` and `supported_impact_scaled`, request peak candidates, and
use `--extra-step-levels 1`: 13 settings per fixture. The current sweep's
deterministic plan is exactly equal to the original manifest metadata.

Before execution, the replay froze 156 protected files: the relevant sources,
header, application, scripts, specifications, original plan and reports, both
executables, the complete baseline generation, and all 75 artifacts listed in
the original independent experiment's manifest. Every listed artifact matched
its original hash and size. Each original candidate was also checked against
the initial report's retained raw input bytes and hashes.

After execution and comparison, all protected identities were unchanged. The
54 new generation files were separately frozen before and after comparison and
also remained unchanged. The replay plan was frozen before and after the native
generation. Complete identities and all before/after comparisons are in the
durable JSON.

| Observation | Recorded result |
|---|---:|
| Native generation invocations | 1 |
| Native candidate processes | 26 |
| Candidate process failures / recorded candidate errors | 0 / 0 |
| Exact stdout matches | 26 / 26 |
| Total candidate stdout bytes | 24,559,764 |
| Exact parsed JSON matches | 26 / 26 |
| Exact stderr matches | 26 / 26; all empty |
| Sample rows per candidate | 1,001 |
| Event rows per candidate | 6 |
| Aggregate candidate bytes | 24,114,469; exact match |
| Protected-file changes | 0 |
| New-generation changes during comparison | 0 |
| Independent reference recomputations | 0 |

The aggregate SHA-256 is
`89130a93ae91a75643411d3e246b7fe27aa9782331d65ec4383b1d94d0568827`
for both generations. Each candidate's complete argv, settings, source/target
paths, stdout/stderr hashes and sizes, exit, errors, success/failure fields,
diagnostics, and initial comparison binding is retained in its durable row.

The operational manifests have 31 expected differences: creation time, producer
path and binary hashes, the 26 producer command paths, and the aggregate audit
input path. Every difference is retained by JSON pointer with its old and new
value. There are no other manifest differences. In particular, settings,
generation status, errors and diagnostic counts were not suppressed.

## Source and producer identities

| File or producer | SHA-256 at both replay boundaries |
|---|---|
| Current `src/contact_reference.cpp` | `70dde1bfab9c2e35457da2ebf66ff4638cb6f56f1f19a84dbe55a1752d5166fd` |
| `include/rhodes/contact_reference.hpp` | `00b99e61dc73686c22675bedf647c3ad1b27ba13332180431f5a42e485057fda` |
| `apps/contact_reference.cpp` | `36f30eebe08856989eeefd940acdff491c8f9cfc217342893f8d113c61865113` |
| `tools/run_contact_sweep.py` | `81b45563f7409b4c6378a7867d7b8d33394676482c14e165f9919c1bc40e627a` |
| `tools/validate_contact_reference.py` | `c910da5da6b6ad957b709a414cef39a08ed6e8b75c6e36c9d73ef535a064dcdc` |
| Preserved baseline executable | `4ed2552430f5de5846adcb1a7b99521b118a12b714ad61b146ede7bab24f8f2d` |
| Current native executable | `3f5fc5ed244f320fc7d18889a5266ae923a11cee13b99e4bfab0b1348867dbec` |
| Original extrema plan | `6692f89fe6e3e33728dce2f238306fbd7fd0ea2604ed6d38e9566c08cd827bc3` |

The initial independent experiment recorded a different contact source,
`0f9808442b5ddf6ddc6091c9b6294117b7aea85cd54707b9c3743121ac706100`,
and the preserved baseline executable. The executable identities are distinct;
their selected outputs are identical in this replay. The machine-readable
record retains the initial source identities alongside the current ones.

## Work counters retained

Every output diagnostic is compared in full and retained in each durable row.
The following ranges summarize the 26 identical old/new counter sets; the zero
values are observed counters, not omitted work or assumed equivalence.

| Counter | Minimum | Maximum | Total |
|---|---:|---:|---:|
| Accepted steps | 3,229 | 16,621 | 274,479 |
| Rejected steps | 26 | 60 | 1,029 |
| RHS evaluations | 251,627 | 1,268,228 | 21,000,890 |
| Analytic free evaluations | 0 | 0 | 0 |
| Event subdivisions | 0 | 0 | 0 |
| Root iterations | 313 | 445 | 9,610 |
| Grazing candidates | 0 | 0 | 0 |

These supported-impact outputs do not establish behavior for grazing fixtures.
The observed zero analytic-free and grazing counters reinforce the limited
fixture scope; exact output comparison, rather than that observation alone,
establishes compatibility here.

## Commands, retention and checks

Execution ran from 2026-11-24 08:39:31 to 08:39:44 UTC. The native sweep itself
took about 7.69 seconds. This elapsed time is operational context, not a runtime
benchmark or deadline claim.

The exact executed sweep was:

```sh
python3 tools/run_contact_sweep.py \
  --executable build/vst3/rhodes_contact_reference \
  --output-dir build/w2-026-supported-native-replay/candidates \
  --fixture supported_impact \
  --fixture supported_impact_scaled \
  --extra-step-levels 1 \
  --locate-peak-candidates
```

The retained command uses absolute paths and the actual Python interpreter;
the displayed relative command is equivalent from the repository root. The
same command with `--plan` ran before native execution and returned exit 0.
Reproduction requires an unused output directory; the existing evidence must
remain unchanged. No `--force` option was used.

Read-only project checks also returned exit 0:

- `python3 tools/project.py validate`: 14 tasks, 26 evidence records,
  fidelity `unvalidated`.
- `python3 tools/project.py sync --check`: generated state current.
- `python3 tools/project.py next --json`: revision 32, no unclaimed available
  task; specimen identity and measurement blockers retained.

The scratch replay directory retains `replay.py`, complete driver and command
stdout/stderr, both provenance snapshots, the frozen plan, new-generation
snapshots, command records, and the complete new candidate generation. All raw
candidate bytes remain there; the durable JSON carries hashes, settings and
results without embedding duplicate raw streams. No source, test, CMake, shared
state, old report or prior generation was changed by this worker.

The [timing clarification](../../validation/supported-extrema-clarification-20261124b.md)
was read before replay: the original one-tenth condition applies to the two
successive value refinements, while supplemental isolated-peak timing uses its
full declared budget. This replay changes neither rule and recomputes neither
independent reference comparison.

## Handoff

The integrator may associate the original scoped independent-reference
comparison data with these exact current-native candidate bytes, retaining both
producer identities and this replay record. The unresolved matrix, convergence
and extrema-coverage requirements remain separate PHY-002 work; measured physical
fidelity retains its own missing-data gates. No acceptance promotion is requested
by this report.

Changed durable paths are this report and
`docs/validation/supported-native-replay-20261124b.json` only. Scratch work is
confined to `build/w2-026-supported-native-replay`.
