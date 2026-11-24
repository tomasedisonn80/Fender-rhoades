# W5-88 — Retained current native CI evidence

Evaluator: `/root/w5_88_current_native_archive`; 24 November 2026.

**Both completed native workflows passed at exact source/test checkpoint
`240170673983f9992e4657a2d96c2172ece1ac30`.** This collection preserves their
actual logs and source bindings; it executed no build or test.

The [Foundation run](https://github.com/baabi-boo/Fender-rhoades/actions/runs/45340923223)
contains seven successful jobs. The [VST3 run](https://github.com/baabi-boo/Fender-rhoades/actions/runs/45340923158)
contains three. I fetched each run, both complete job lists, artifact metadata
and all ten decoded job logs through the connected GitHub API. Every run/job
head equals the tested commit. Full API payloads and exact retrieval parameters
are retained with the logs in the [archive](../../validation/native-final-20261124.tar.gz).

## Observed results

| CTest job | Compiler | Passed | Skipped | Registered |
| --- | --- | ---: | ---: | ---: |
| Linux core | GNU 15.2.0 | 150 | 0 | 150 |
| Windows core | MSVC 19.52.37029.0 | 147 | 0 | 147 |
| macOS ARM core | AppleClang 21.0.0.21000101 | 141 | 6 | 147 |
| Linux ASan/UBSan | GNU 15.2.0 | 150 | 0 | 150 |
| Linux VST3 | GNU 15.2.0 | 138 | 0 | 138 |
| Windows VST3 | MSVC 19.52.37029.0 | 134 | 0 | 134 |
| macOS ARM VST3 | AppleClang 21.0.0.21000101 | 125 | 9 | 134 |

| Python CI job | Interpreter | Unskipped passes | Skipped | Reported total |
| --- | --- | ---: | ---: | ---: |
| Linux | CPython 3.11.18 | 564 | 20 | 584 |
| Windows | CPython 3.11.9 | 561 | 23 | 584 |
| macOS | CPython 3.11.9 | 563 | 21 | 584 |

The [machine-readable record](../../validation/native-final-20261124.json)
lists every skip and the observed OS/image versions. All five quadrature-engine
selectors passed in each core/sanitizer job. All six SDK/module-metadata
validator selectors passed on each VST3 platform, alongside the inspected scalar
output and allocation guards. Four module coexistence/duplicate selectors are
Linux-only. The macOS CTest skips are the stated x86 MXCSR modes; none masks the
new engaged-cell fault coverage. Python optional-backend/capability skips remain
distinct from [W5-81's later local NumPy/SciPy run](20261124-w5-81-final-python.md).

Every VST3 job printed and verified the same five SDK pins. Configure commands
record Unix Makefiles on Linux/macOS and x64 Visual Studio on Windows. Archive
verification and upload steps succeeded for all three complete bundle sets.
The API reported three unexpired artifacts, with expiry on 1 December 2026 at
06:32–06:34 UTC. Their API names, sizes and digests are retained; I did **not**
download or independently hash those uploaded binary artifacts.

## Source and preservation checks

All **148** listed source identities match both `git show 2401706:path` and
local bytes before/after collection at `766dd11c73cf9d1dcf6c1960b4333767356f9937`.
This comprises 83 native/build/workflow files and 65 Python tool/test files.
The native set extends the historical 74-file manifest with nine application,
preset and workflow inputs. Its only changed original entry is the expanded
`quadrature_engine_test.cpp`; this checkpoint actually exercised that version.
Later documentation, evidence, state and campaign edits are not relabeled as
having run in these jobs. The tested Git tree identifies other committed inputs.

The deterministic archive contains **12 flat regular members**: ten normalized
logs, full API records and the collection script. I verified each member's bytes,
size, type, name and hash without extraction or execution. Normalization replaces
CR/CRLF with LF and retains exactly one final LF, preserving other content.
These hashes identify connector-decoded text, not original server ZIP bytes or
independent authenticity. The collector and its exact command are retained.

Required project preflights passed at revision 27 with 23 evidence records.
No production, canonical-state, ledger or historical artifact was changed.
These observations do not establish DAW/device operation, universal deadlines,
physical fidelity, minimum deployment support, publisher signing/notarization
or a release. SDK ad-hoc macOS signing messages do not establish release signing.
Root can use this record for source-specific prototype acceptance; broader
physics/runtime gates remain separate.

## Frozen artifact identities

| Artifact | Bytes | SHA-256 |
| --- | ---: | --- |
| `native-final-20261124.json` | 80,992 | `6a31a715bdd9590f9ff789636df1bb948639120bed0811ec43b313e3a96833e2` |
| `native-final-20261124.tar.gz` | 199,986 | `77c2f598b07d971d74a7f986a3230bf378e51a710ad652dc8382082943c902a5` |

The JSON contains every log/member digest and all 148 source hashes. Its archive
does not contain itself, preventing a circular evidence identity.
