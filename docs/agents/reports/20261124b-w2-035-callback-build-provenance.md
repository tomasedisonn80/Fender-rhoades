# Worker 035 — reproducible callback binary provenance

24 November 2026; actual worker 035, continuation B wave 2. Bounded RT-001
assignment; the integrator owns Git, canonical state and evidence registration.

## Result and interpretation

**The two clean builds reproduce the exact executable bytes used in the
[initial paired callback capture](20261124b-w2-021-callback-capture.md).** The
[new verification archive](../../validation/callback-clean-build-20261124b.json)
records a known source, compiler and explicit flag configuration for those
bytes. Both binaries are 308,144 bytes.

| Role | Archived source commit | Reproduced binary SHA-256 |
| --- | --- | --- |
| Baseline | `a17e665454c1daa1c1aff3d71b790b28fe145e12` | `f6ffbc20500a173528d911c58997fb0be1569f0fb072dfd66c57fc8716518b31` |
| Candidate | `20a77e8b2033d435839df594c783837befb07938` | `e97af0f971eb38e713b57713cb9bb435381758432b6f15e2fea2b12ff7ac1c6f` |

This establishes a reproducible configuration for the measured binaries. It
does **not** recover or authenticate the compiler commands that originally
created them. The historical capture report, its unavailable baseline metadata,
and all observed timing values remain unchanged. No second timing run was
needed or performed. Observational ratios, outliers and shared-host limitations
retain their original scope; there is no new causal speedup, portable timing,
WCRT, native DAW, physical-fidelity or task-completion claim.

## Observed build inputs

The integrator executed two clean configurations/builds before this assignment,
using separately extracted Git archives and the same external pinned SDK. The
retained receipts place baseline compilation at
`09:04:58.371229–09:05:11.921010 UTC` and candidate compilation at
`09:05:13.756098–09:05:27.432941 UTC`. Both configure and build commands exited
0, with empty stderr. This worker inspected the execution records and resulting
files independently; it did not rebuild or launch either benchmark.

The explicit compiler was `/usr/bin/x86_64-linux-gnu-g++-14`, Debian GCC
14.2.0-19. Its current executable hash and exact `--version` output match the
build environment record. CMake was 4.4.5 with the Unix Makefiles generator.
Both configurations explicitly selected Release, empty global C++ flags,
`-O3 -DNDEBUG` Release flags, warnings as errors, sanitizers off, compile-command
export, and the headless VST3 configuration. Both built only
`rhodes_vst3_quadrature_benchmark` and its dependencies with `--parallel 2
--verbose`. Exact argv arrays, logs, generated target flags and link commands
are retained, including target-specific language and visibility options.

The SDK root and four required submodules match the recorded revisions and
have empty Git porcelain status both during the audit and at its final check:

| SDK component | Commit |
| --- | --- |
| Root | `3cdf9ca5d1f5b1b21e0a86832aa4abe55607bd96` |
| base | `fcf9da0bd27a16f7f03773a3a39822f28f5c8477` |
| cmake | `054c9143cbb8d47fc4694e473f2ee3b4d951a8f5` |
| pluginterfaces | `4f547e8e102b47de4a8b8aaf343c73b700786372` |
| public.sdk | `586dc5e6c8012c3e4b01c79389375cbe96bdb1da` |

## Independent verification

The final audit ran once successfully at
`09:14:02.205368–09:14:04.084603 UTC`, after correcting two audit-harness
assumptions described below. It verified:

- Both commit/tree identities and archive commit headers. Streaming a fresh
  `git archive --format=tar COMMIT` reproduced each retained archive hash exactly.
  Every archived file matched its Git blob identity, source inventory SHA-256,
  and extracted working bytes. No source symlinks were present; all executable
  bits agreed with Git. This was archive verification, not a rebuild.
- All **709 baseline and 782 candidate source files**, plus all **199 baseline
  and 207 candidate recorded build files**, including objects, static libraries,
  binaries, CMake caches, compile databases and target metadata.
- **45 actual compile commands and one actual link command per build.** After
  substituting only the respective source/build roots with placeholders, all
  46 command pairs are identical. Compiler/SDK paths, every flag and argument
  order remain intact. Commands are paired by source identity because the
  parallel build logs need not have identical ordering.
- Every actual compile command against its specific compile-database entry,
  keyed by working directory, source and output object. Only the generated
  dependency-output options `-MD`, `-MT ARG` and `-MF ARG` are removed for that
  database cross-check. They remain in the full baseline/candidate comparison.
- The original capture archive's before/after binary identities and exact
  retained input snapshots, as well as the still-present original binaries.
  Both match the reproduced binaries in SHA-256 and size.
- **2,014 original input files** had identical hashes and sizes at the end of
  this audit. No protected input changed.

Of 45 object files, **43 are byte-identical**. The differing objects are
`src/research_engine.cpp.o` and the VST3 `processor.cpp.o`. All four SDK static
libraries match; `librhodes_core.a` differs. Compiler dependency files identify
18 project source/header inputs per build. Three of those changed between the
baseline and candidate commits: `include/rhodes/quadrature_fir.hpp`,
`include/rhodes/research_engine.hpp`, and `src/research_engine.cpp`.

The captured candidate's 27-file source inventory matches the reproduced
candidate in 26 entries. The remaining file is `CMakeLists.txt`: the later
commit adds the separate offline alias primitive target and its help test.
All 18 compiled project dependencies match the captured candidate exactly.
The recorded benchmark commands and resulting binary also match. Configured
compile databases contain 260 baseline and 264 candidate entries; those counts
include targets that were **not** compiled by these two builds.

## Retention, actual commands and limits

The new JSON is **2,714,253 bytes**, SHA-256
`5e6dc192513328f5a6ccb2b31437106f287d5b33b158afb1ded5b3be71dfd83f`.
It retains **418 exact UTF-8 text artifacts / 2,437,265 original text bytes**:
build and environment receipts; root's build script; this audit's script;
source inventories; configure/build logs; all recorded textual build metadata;
90 compiler dependency files; and the selected original capture snapshots,
manifest and immutable worker-021 report. Every embedded string was decoded
back to bytes and checked against its size and SHA-256 after writing the JSON.
Archives, compiler, SDK sources, object files, libraries and executables are
represented by their recorded identities, not duplicated as binary blobs.
Retained text was scanned for private-key blocks, URL userinfo and credential-like
uppercase assignments; none matched. No process environment values were copied.

Startup commands actually run from the repository root were:

```sh
python3 tools/project.py validate
python3 tools/project.py sync --check
python3 tools/project.py next --json
```

At that startup observation they returned 0: revision 32, 14 tasks, 26 evidence
records, fidelity `unvalidated`, and no unclaimed available work. The integrator
subsequently reported a separate main-branch change adding HOST-002; this audit
continues to bind the immutable source commits above, not that later live state.

The final substantive command was:

```sh
PYTHONDONTWRITEBYTECODE=1 python3 /workspace/scratch/cc294adc3e2f/audit_callback_builds_035.py
```

It exited 0. Before that success, two scratch audit assertions failed without
writing an artifact: exact read/write permission equality was too strong for
the observed Git tar members, and a compile-database lookup by source alone
selected an unused `sdk_hosting` entry for a source also built into `sdk`.
The audit now verifies regular-file/executable-bit agreement plus the exact
regenerated archive, and includes the output object in the database lookup.
No source, build receipt, historical report, scientific threshold or runtime
behavior was changed to make a check pass.

`python3 tools/check_docs.py` subsequently passed. A separate final parse also
confirmed all 418 retained artifact names are unique and the published JSON
hash matches the value above.

The complete OS/system-header/linker/runtime toolchain closure is not archived.
The result proves this observed known configuration reproduces the captured
bytes, not reproducibility across other machines. Recorded logs and hashes
provide consistency evidence rather than external attestation of execution.
This worker performed zero builds, zero benchmark launches, zero test-suite
runs and zero Git/state mutations. Only this report and the assigned verification
JSON were added to the repository; scratch scripts are embedded in the JSON.
The next action is integrator review and reference from the runtime evidence
summary, retaining the initial timing experiment and its limitations unchanged.
