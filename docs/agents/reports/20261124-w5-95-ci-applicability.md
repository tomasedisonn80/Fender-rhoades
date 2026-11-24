# W5-95 — CI source applicability and acceptance checkpoint

Evaluator: `/root/w5_95_ci_applicability`; 24 November 2026.

**The retained VST3 results remain applicable to the inspected source, and the
acceptance commit's Foundation CI passed all seven jobs.** This report makes no
claim about the later campaign-closing commit or its planned stricter ledger gate.

## Inspected source and trigger boundary

I compared tested native checkpoint
`240170673983f9992e4657a2d96c2172ece1ac30` with acceptance commit
`afd69a6cedddc86f017e1634eb6b7d1aef510191`. All **51 changed paths** are
documentation, evidence or project metadata. There is no committed delta in
CMake configuration, workflows, SDK integration, runtime/host sources, application
sources, C++ tests, Python tools/tests or `docs/DEPENDENCIES.md`. Focused file
hashes below also match their tested and acceptance Git objects.

The [retained native record](../../validation/native-final-20261124.json) binds
the earlier actual VST3 execution to its exact source, five SDK pins and decoded
logs. Its 83 native/build/workflow and 65 Python source inventory remains a
historical execution manifest. My Git-delta inspection establishes applicability
of those unchanged tracked inputs; it does not rerun the native tests or infer
binary reproducibility from source identity.

`vst3.yml` triggers main pushes only for its own workflow, CMake/SDK integration,
Rhodes headers/sources, plug-in sources, the host benchmark, C++ tests or the
dependency document. None of the post-checkpoint changes matches those paths.
The acceptance head's returned run list contains Foundation CI only, as expected.
Rebuilding VST3 for this documentation/state delta is unnecessary for the stated
source-bound claims; the successful VST3 run remains
[45340923158 at 2401706](https://github.com/baabi-boo/Fender-rhoades/actions/runs/45340923158).

Foundation CI has no push-path filter. Its three project-state jobs inspect
canonical state, generated documents, data contracts, report hashes, retained
archives, coefficient freshness and Python tooling. Therefore changes to
completion evidence or campaign metadata require their own observed Foundation
result even when the audio implementation is unchanged.

## Actual observed acceptance CI

GitHub's [run 45345476481](https://github.com/baabi-boo/Fender-rhoades/actions/runs/45345476481)
reports `completed/success` at the exact acceptance head above. All seven job
records agree on that head, with no failed steps. The last job completed at
`2026-11-24T07:08:57Z`.

| Job | Job ID | Observed conclusion |
| --- | --- | --- |
| Project state / Ubuntu | 138278855673 | success |
| Project state / Windows | 138278856029 | success |
| Project state / macOS | 138278856003 | success |
| C++20 / Ubuntu | 138278855810 | success |
| C++20 / Windows | 138278855825 | success |
| C++20 / macOS | 138278855896 | success |
| Address and undefined behavior sanitizers | 138278855427 | success |

I also observed preceding Foundation run 45344849074 succeed at `93908da`;
that earlier outcome is not substituted for this acceptance result. The present
review uses run/job metadata for this new checkpoint, not newly retained complete
logs or a new independent count of every test selector.

## Remaining integration check and procedure

The inspected Foundation workflow still invokes the wave validator in progress
mode. Root plans to add `--require-complete` only after all 100 reports exist.
That invocation change and later reports/ledgers need their own final-head
Foundation check. A change confined to `ci.yml` does not match the VST3 trigger;
it also means the old native manifest's workflow byte identity must remain dated.
No future success is inferred here. Both workflows cancel older in-progress
runs, so the final report must name the actual completed head.

Required project preflights passed at revision 30: 14 tasks, 26 evidence records,
unvalidated fidelity and PHY-002/RT-001 available. Read-only Git diffs, file/Git
SHA-256 comparisons, workflow inspection and connector GETs for run lists,
run records and jobs supplied this review. Only this report was written. No
build, test suite, CI dispatch, Git mutation or canonical-state edit occurred.
CI success does not establish DAW/device deadlines, measured fidelity or release
qualification.

## Focused identities at the inspected checkpoint

| Path | SHA-256 |
| --- | --- |
| `.github/workflows/ci.yml` | `9a0d441f70dc60be6bdf7ec3b3ab5a3c5e046feed103a043633d1bd27911e94a` |
| `.github/workflows/vst3.yml` | `6cf2593353d05a6b122c99736399132b4c813ec8fba5af03b39ed4d5fdd8e3f3` |
| `CMakeLists.txt` | `2a70fba5924a3eeb01bf0dfd7c2a0f6a00726d27ac7535660a4bf267cf81ff3e` |
| `CMakePresets.json` | `a06e2a16dba0b4947352cbbcec2a7668d29b3f777c6f460f9d65f6bd0bb7f0aa` |
| `cmake/Vst3.cmake` | `e779743e238c5b7ac2632d73764eb19974fcef3f4ae5c8699ac6f188fde783a6` |
| `docs/DEPENDENCIES.md` | `2da17d8502587a45f83c900633a0a53f1fcbaf60a901bb589370b61c773b2126` |
| `docs/validation/native-final-20261124.json` | `6a31a715bdd9590f9ff789636df1bb948639120bed0811ec43b313e3a96833e2` |
