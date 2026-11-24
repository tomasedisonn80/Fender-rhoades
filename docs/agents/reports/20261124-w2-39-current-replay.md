# Wave 2 assignment 39: current-source contact peak replay

**Result:** Both authorized CLI invocations returned **exit 0** at the current diagnostic sources. **PHY-002 remains incomplete.** This report adds bounded replay evidence; it makes no acceptance or fidelity promotion.

Parent: `/root/w2_contact_implementation`. Replay interval: 2026-11-24T05:53:12.928393+00:00 through 2026-11-24T05:53:13.918313+00:00. No native producer or test suite was executed by this replay agent.

## Exact retained inputs

The immutable `docs/validation/contact-peaks-native-20261124.tar.gz` matched its existing summary: **277,064 bytes**, SHA-256 `b3ae5198fd610820cb516426e7f84dcc921035dc89ce42282fde530cd7175cec`. All **104** unique members were checked as safe relative regular files, and all indexed sizes and hashes were verified. The archive stayed byte-identical. Selected members were read with `tarfile.extractfile`, without filesystem extraction or execution.

| Fixture | Retained bytes | SHA-256 |
| --- | ---: | --- |
| `conservative_impact` | 19,831 | `3306db341920c09ac0604c61d4e0cf19a8c2c6b925bcb9a9c63cf2bbaada3085` |
| `damped_impact` | 20,275 | `8eac863171810777097dc2949a0d035560b5e680889cd9d1a7909353c7ad80de` |
| `forced_clamp` | 4,102 | `b941313b4b93d3e62a7548829ae5c34d18d98c533349138d74a8a07d79b7de74` |
| `preload_equilibrium` | 195,352 | `4be340044dcdb5cc6cdb9ab723889584cfa9a060121e46b2f1df59f77c5d4e85` |

The four inputs came from `native/<fixture>/stdout.json`. Their archive-manifest identities, original native invocation stdout identities, replay input bytes, and validator-retained raw bytes all matched. The input envelope remains insufficient to attest arbitrary physical parameters.

## Commands and observed results

Each output directory was absent before its one CLI invocation; the CLI created it exclusively. The archive retains exact argv, cwd, timestamps, stdout/stderr, plan, manifest, case records, raw inputs, references, comparisons, and source snapshots. The actual two CLI argv arrays are represented below as shell commands:

```sh
/opt/codex/runtimes/codex-primary-runtime/dependencies/python/bin/python3 /workspace/scratch/d6370337c6a4/Fender-rhoades/tools/validate_contact_peaks.py --candidate /workspace/scratch/d6370337c6a4/contact-peaks-final-replay-20261124-0zolf4pv/inputs/conservative_impact.json --candidate /workspace/scratch/d6370337c6a4/contact-peaks-final-replay-20261124-0zolf4pv/inputs/damped_impact.json --candidate /workspace/scratch/d6370337c6a4/contact-peaks-final-replay-20261124-0zolf4pv/inputs/forced_clamp.json --candidate /workspace/scratch/d6370337c6a4/contact-peaks-final-replay-20261124-0zolf4pv/inputs/preload_equilibrium.json --output /workspace/scratch/d6370337c6a4/contact-peaks-final-replay-20261124-0zolf4pv/cli/all_four_default/evidence --producer-binary /workspace/scratch/d6370337c6a4/Fender-rhoades/build/vst3/rhodes_contact_reference
```

```sh
/opt/codex/runtimes/codex-primary-runtime/dependencies/python/bin/python3 /workspace/scratch/d6370337c6a4/Fender-rhoades/tools/validate_contact_peaks.py --candidate /workspace/scratch/d6370337c6a4/contact-peaks-final-replay-20261124-0zolf4pv/inputs/damped_impact.json --output /workspace/scratch/d6370337c6a4/contact-peaks-final-replay-20261124-0zolf4pv/cli/damped_with_time/evidence --producer-binary /workspace/scratch/d6370337c6a4/Fender-rhoades/build/vst3/rhodes_contact_reference --with-damped-time
```

| Invocation | Cases | Process exit | Elapsed | Stderr |
| --- | ---: | ---: | ---: | ---: |
| `all_four_default` | 4 | 0 | 0.194167 s | 0 bytes |
| `damped_with_time` | 1 | 0 | 0.759145 s | 0 bytes |

Runtime: Python 3.12.14, NumPy 2.3.5, SciPy 1.17.0. The existing native binary was only hashed through `--producer-binary`: SHA-256 `4ed2552430f5de5846adcb1a7b99521b118a12b714ad61b146ede7bab24f8f2d`, 127,088 bytes. That current declared identity matched the separately retained original production record. It was not executed or used to regenerate trajectories.

| Fixture / invocation | Value checks | Observation consistency | Timing |
| --- | --- | --- | --- |
| Conservative, default | All four pass | All four pass | All four pass |
| Damped, default | All four pass | All four pass | Hammer timing `not_available` because unrequested; disabled damper checks pass |
| Forced clamp, default | All four pass | All four pass | All four pass; enabled zero-force plateau remains distinct from disabled damper semantics |
| Preload, default | All four pass | All four pass | Hammer checks pass; positive damper times remain `unresolved_near_equilibrium` |
| Damped, requested timing | All four pass | All four pass | All four pass under supplemental timing budgets |

The requested damped hammer reference maxima remain distinct: compression at `0.00034505973795999833 s`, force at `0.00017769056225746204 s`. Numerical quadrature and refinement do not constitute a continuous-extrema certificate. Preload positive values remain verified enclosures, not exact plateaus.

## Current-source and prior-evidence comparison

All recorded current sources stayed unchanged across the replay, and each validator manifest independently reports matching before/after source hashes. The main module identities are:

| Source | Current SHA-256 |
| --- | --- |
| `tools/contact_peak_schema.py` | `af5cbf87d155ecb40c176b78a7527b2a7bd686c7fd515be621af93d910db86c4` |
| `tools/contact_peak_oracles.py` | `74030851ce9511cd50c6952a11b43bfdf5e8da5d7fd10428c009be0210060789` |
| `tools/validate_contact_peaks.py` | `5bfc879d0a4a42d48667cc2370bafc85a2bd6c6b08c18071c1d60ab7b85be717` |

Compared with the prior native replay, the oracle and validator source hashes changed; the schema hash did not. Every default reference and comparison JSON object is unchanged. In the requested damped run, the only reference/comparison differences are `parent_elapsed_s` and `worker_elapsed_s`; all definitions, values, intervals, statuses, coverage, budgets, errors, and outcomes remain identical. Full differences are retained in the new JSON.

Current records add `bounded_input_bytes`, and manifests add `input_bytes_charged_to_collection_limit`. The existing `unattempted_inputs` field remains present. This describes corrected partial-retention accounting; these successful retained files are complete and identical. No storage failure was induced here.

## Retained focused-test evidence

The archive retains both distinct cohorts. This agent did not rerun either command.

| Cohort | Actual recorded outcome | Attribution and source boundary |
| --- | --- | --- |
| Earlier 74 new focused tests | `Ran 74 tests in 2.966s`, `OK`; recorded process exit 0 | Supplied log and metadata verified. Ten recorded source hashes match before/after within that historical run. Production module hashes match this replay; two subsequent test-only portability revisions are explicitly distinguished. |
| Later 85 total tests | `Ran 85 tests in 2.853s`, `OK`; root-reported exit 0, no skips | Root executed the 74 focused tests plus 11 existing `test_contact_peak_sweep` compatibility tests. Root reports an immediate post-run hash listing; **no pre-run snapshot was taken**, so this report claims no before/after equality for this 85-test run. Current retention-time hashes and root-supplied test hashes match. |

Root supplied the exact later command from the repository cwd:

```sh
python3 -m unittest discover -s tests/python -p 'test_contact_peak_*.py' > build/contact-tests-portability-fixed-20261124.log 2>&1
```

Earlier log SHA-256: `e038627be7245eeb7857a6d5506579d7548cb8122b1d101c26bbcb468c812bac`; earlier metadata SHA-256: `8a155a8c3be74847cb3c1c66495bcd36bfb1117a69b55f16f8cfed08144a3bca`. Later root log SHA-256: `b758e7726aa36146cd62bd841c44ae31b3819beedd5a808eb0c5dc59879f94d0`. The later test edits use explicit UTF-8 stderr bytes and capability-skip unsupported surrogate filenames. Full old/current test hash mappings are retained.

An initial preparation guard stopped before any CLI or staging creation when it detected the two test-only hash changes. After the parent identified those authorized later edits, the guard explicitly allowed exactly those paths and retained both cohort identities. There were exactly two actual replay CLI invocations; no diagnostic failure was retried. This preparation observation is preserved in `preparation-checks.json`.

## Artifacts and handoff

| Artifact | Bytes | SHA-256 |
| --- | ---: | --- |
| `docs/validation/contact-peaks-final-replay-20261124.json` | 54,495 | `dfbe13724511c0437e2e6a266d1815360df35791608b6b0be5e4f0f29b60a879` |
| `docs/validation/contact-peaks-final-replay-20261124.tar.gz` | 228,021 | `fc48780e653fb4bdbd97dab153461797d2d7607086c8a9821e8f44f4e207e8e1` |

The final archive has **78 safe regular members**; all member bytes and manifest sizes/hashes were reread and verified. Only the assigned new report, summary JSON, and replay archive were written in the repository. No Git, canonical-state, existing auditor, production, frozen-contract, or original-evidence changes were made by this agent.

Next: the integrator can incorporate this supplementary current-source replay and close assignment 40 / the 20-agent wave. General continuous-extrema coverage, full PHY-002 acceptance, and measured-instrument fidelity remain incomplete.
