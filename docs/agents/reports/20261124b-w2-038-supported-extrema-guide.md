# W2-038 — supported-impact extrema guide

Date: 24 November 2026. Actual worker 038/100, wave 2. The integrator owns Git,
canonical state, source changes and evidence registration. This assignment
changed only [the new guide](../../SUPPORTED_CONTACT_EXTREMA.md) and this report.

## Delivered scope

The guide separates the 26 native candidate files from the six fresh independent
R0/R1/R2 references. It gives the two-fixture generation command, required
`--extra-step-levels 1` and `--locate-peak-candidates` options, and a Python
argument-list example that selects only numbered one-run JSON inputs. The
optional producer argument is correctly described as an identity declaration,
not executable invocation or candidate-production attestation.

It documents standard-library help/plan operation, optional NumPy/SciPy execution,
exclusive outputs, per-worker work/deadline caps, separate result fields and
exit codes, retention of failed/partial evidence, source and artifact identity
checks, and the eleven-array NPZ representation and storage ceilings. The
reference API section distinguishes completed integration, cross-level
refinement, partial dense coverage and separately formatted checkpoints.

Both successive peak-value differences use `0.1*B(M_R2)`; supplemental timing
uses its full declared budget. The guide links the immutable timing correction,
initial comparison/reference/artifact records, the current-native exact replay,
and workers 030/031's hardening and independent tests. It does not copy the old
timing typo or infer certified coverage, full convergence, task completion or
physical fidelity from the passing scoped record.

## Actual verification

Read `AGENTS.md`, `README.md`, `docs/PROJECT_STATE.md`, `docs/ROADMAP.md`, the
canonical task records, relevant tools and retained reports. Startup commands
all exited 0:

```sh
python3 tools/project.py validate
python3 tools/project.py sync --check
python3 tools/project.py next --json
```

Observed revision 34: 15 tasks, 26 evidence records, no available unclaimed
task, and fidelity `unvalidated`. Work remained under the integrator's existing
PHY-002 assignment; separate HOST-002 ownership was preserved.

The following read-only CLI checks exited 0 with empty stderr:

```sh
python3 -S -B tools/validate_contact_extrema.py --help
python3 -S -B tools/validate_contact_extrema.py --plan
python3 -S -B tools/contact_extrema_reference.py --help
python3 -S -B tools/run_contact_sweep.py --help
python3 -S -B tools/run_contact_sweep.py \
  --executable build/rhodes_contact_reference \
  --output-dir build/supported-inputs-001 \
  --fixture supported_impact --fixture supported_impact_scaled \
  --extra-step-levels 1 --locate-peak-candidates --plan
```

Plans were captured and parsed in memory. Assertions confirmed 26 sweep slots,
peak diagnostics requested in every slot, two fixtures, and six driver
references. The guide's exact sweep command with `--plan` created no output
directory. A standard-library import/inspection confirmed the three documented
storage ceilings and all eleven array layouts, with neither NumPy nor SciPy in
`sys.modules`. The embedded Python example passed `ast.parse`; it was not run.

`python3 tools/check_docs.py` passed after the guide was written and again after
this report was added. The checker covers local Markdown integrity and links;
it does not execute examples or verify external websites. No native process,
reference generation, numerical integration, test suite, build, Git command or
canonical-state mutation was performed by this worker.

## Frozen bindings and handoff

The five inspected tool hashes matched before and after documentation checks:

| Path | SHA-256 |
| --- | --- |
| `tools/validate_contact_extrema.py` | `65e38326fd3f07566ca37d84513b34a0be24bf916f8d5f4ff541fb2225325dfe` |
| `tools/contact_extrema_reference.py` | `39bed1d73f4482455c9268846987aa91a4200188b0eb83ff0efbc5759a71263e` |
| `tools/contact_supported_schema.py` | `42d821bf535d77e7fad29da48c25dc9ac1bf2cba2fb68e85efe92afa4a82c7c7` |
| `tools/run_contact_sweep.py` | `81b45563f7409b4c6378a7867d7b8d33394676482c14e165f9919c1bc40e627a` |
| `tools/validate_contact_reference.py` | `c910da5da6b6ad957b709a414cef39a08ed6e8b75c6e36c9d73ef535a064dcdc` |

Guide SHA-256:
`e1ff234164a30439527098f35701c81a90d159f775ee7d320c008d225da07767`.
Root can link the guide from the appropriate project index when integrating;
README and existing guides were intentionally outside this assignment. The
recorded numerical findings are attributed to their original evidence, not
re-executed or promoted by this documentation work.
