# W5-89 — Final retained artifact audit

Evaluator: `/root/w5_89_final_artifact_audit`; 24 November 2026.

**No material artifact-consistency or source-binding defect was found.** This
review inspects retained observations; it does not rerun tests or decide
scientific acceptance.

## Python and frozen definitions

The [local Python record](../../validation/python-final-20261124.json) and gzip
log match their compressed and decompressed hashes and declared sizes. Independent
verbose-log counting gives **583 passes and one skip**, agreeing with the
584-test summary and recorded exit zero. The skip explicitly requires
`RHODES_AUDIO_PROBE`; available NumPy/SciPy checks are distinct from that missing
production-probe comparison.

All **544** before/after input identities agree. Of these, **543** match actual
Git blobs at `420c4c76160b5f5c631cd5e4e21fd1875461c5f5`; the sole exception is the
then-untracked Wave 5 ledger, whose recorded run bytes remained stable. New peer
reports were outside the fixed inventory. Later metadata edits are not silently
represented as inputs to that run.

The [acceptance source manifest](../../validation/development-source-manifest-20261124.json)
binds **156 files** to actual `766dd11c73cf9d1dcf6c1960b4333767356f9937` Git objects
and current bytes: 74 native inputs, 65 Python sources and 17 review documents.
All 74 native inputs also match the tested `2401706` checkpoint. Independently
recomputed HOST-001, PHY-002 and RT-001 projection digests match the
[frozen definitions](../../validation/development-task-definitions-20261124.json)
and unchanged current acceptance text, dependencies, required kinds and target.

## Current native archive

After assignment 88 declared its artifacts frozen, I verified the
[native record](../../validation/native-final-20261124.json) and every archive
member without extracting or executing files. The 199,986-byte gzip expands to
1,402,880 bytes under a 32 MiB limit. Its **12** unique, sorted, flat regular
members have matching lengths/hashes, mode 0644 and zero ownership/timestamps;
no links, sparse entries or path traversal are present. Ten normalized logs,
API records and the collector source are retained.

All ten job identities, successful conclusions and head SHAs agree with the
retained API records for runs **45340923223** and **45340923158**, at exact commit
`240170673983f9992e4657a2d96c2172ece1ac30`. Independent log parsing reproduces:

| CTest job | Passed | Skipped | Registered |
| --- | ---: | ---: | ---: |
| Core Linux | 150 | 0 | 150 |
| Core Windows | 147 | 0 | 147 |
| Core macOS ARM | 141 | 6 | 147 |
| Linux ASan/UBSan | 150 | 0 | 150 |
| VST3 Linux | 138 | 0 | 138 |
| VST3 Windows | 134 | 0 | 134 |
| VST3 macOS ARM | 125 | 9 | 134 |

Each CI Python log reports 584 tests: Linux **564+20**, Windows **561+23**, macOS
**563+21** passes plus skips. Six SDK/metadata selectors and bundle archive/upload
steps pass on each VST3 platform. All **148** execution-manifest sources match
actual tested Git objects and current bytes: 83 native/build/workflow and 65
Python files. The source tree ID also matches. Uploaded bundle names, digests
and expiry agree with API metadata; those bundle bytes were not downloaded.

## Procedure and limits

Required project preflights passed at revision 27 with 23 evidence records.
Read-only Python JSON/gzip/tar/hash inspection and Git object reads supplied this
audit. Initial parser assumptions were corrected for interleaved Windows worker
stderr and CTest skipped-line padding; retained artifacts were unchanged. No
build, suite, benchmark, collector or archived command ran. Only this report was
written. All six artifact hashes below remained stable at finalization.

Normalized-log identity is not independent server-log authentication, physical
measurement authenticity or binary reproducibility. Architecture skips remain
explicit. These results do not establish arbitrary compiler flags, native DAW
or device behavior, specimen fidelity, release signing or minimum deployment
baselines. Milestone decisions remain the integrator's criterion-bound review.

## Verified artifact identities

Paths are under `docs/validation/`.

| File | SHA-256 |
| --- | --- |
| `python-final-20261124.json` | `a1c5634bd91b1e8a59c2f63190753efa9d498153c1fcdbc54f4880466feec347` |
| `python-final-20261124.log.gz` | `eec045f340d3ede9f260e69874445233d34eb539b4fbb8e5263c6d4618e25e86` |
| `development-source-manifest-20261124.json` | `f6e903a3f45b682b9f3c604ae0c49f84fd69419b8ee74ec390bf479905ff0263` |
| `development-task-definitions-20261124.json` | `bf352698c5b1c745ada8b6851231eb2104b772ebaca9d39a638cfcdd7ace7d45` |
| `native-final-20261124.json` | `6a31a715bdd9590f9ff789636df1bb948639120bed0811ec43b313e3a96833e2` |
| `native-final-20261124.tar.gz` | `77c2f598b07d971d74a7f986a3230bf378e51a710ad652dc8382082943c902a5` |
