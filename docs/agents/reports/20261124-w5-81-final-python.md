# W5-81 — Final local Python verification

Agent: `/root/w5_81_final_python`

Date: 2026-11-24

## Scope and preparation

This assignment executed one complete local Python discovery run after the
platform, contact-diagnostic, archive-verifier and campaign-tool changes had
been integrated. It supplies current executed evidence for the Python tooling;
it does not decide host acceptance or either unfinished scientific milestone.
The required operating contract, README, generated project state, roadmap and
canonical state were read. `project.py validate`, `sync --check`, and `next --json`
passed: revision 27 contained 14 tasks and 22 evidence records, with fidelity
unvalidated and the specimen/data blockers unchanged.

The integrator held source, canonical-state and ledger edits for the run. Both
pre-run and post-run Git HEAD were
`420c4c76160b5f5c631cd5e4e21fd1875461c5f5`. The working tree initially had only the new
Wave 5 ledger untracked. Two peer review reports appeared during execution;
those new untracked files were outside the fixed input inventory. No claim of
whole-worktree immutability is made.

## Executed result

```sh
python3 -m unittest discover -s tests/python -v
```

The process ran from the repository root from `2026-11-24T06:50:50.698777+00:00` to
`2026-11-24T06:52:54.433987+00:00`, returned **0**, and reported
**584 tests: 583 unskipped passes and one skip**. Unittest reported
**122.505 seconds**; the surrounding subprocess wall duration was
**123.735179 seconds**. The retained terminal result is
`OK (skipped=1)`. This was the sole full-suite execution in this assignment.
The log also records 16 actual CLI invocations made by the action-auditor tests.

The environment was Linux x86-64 with glibc 2.41, Python 3.12.14,
NumPy 2.3.5 and SciPy 1.17.0. The optional scientific packages were available,
so their numerical unit checks ran. The one skipped test was
`FilteredAudioNumericalTests.test_three_small_production_comparisons_keep_exact_internal_phase`:
`RHODES_AUDIO_PROBE` was unset. Its three production-binary comparisons therefore
remain outside this run. Existing separately retained binary/reference evidence
is not silently counted as execution of that skipped test.

## Integrity and interpretation

The JSON retains all **544 before/after input identities**, including all **65
Python source files** under `tools/` and `tests/python/`, tracked schemas, data,
profiles, project metadata, Markdown and the two filter coefficient headers,
plus the live Wave 5 ledger. Every captured entry stayed byte-identical.
The stated inventory and Git status snapshots define this claim precisely.
Hash agreement establishes byte consistency; it cannot authenticate a physical
measurement or prove that every relevant runtime input was inventoried.

The gzip artifact preserves the complete combined stdout/stderr bytes without
rewriting newlines or removing warnings. Decompression was checked against the
recorded raw-stream digest. The JSON, compressed log and this report are the
only repository files written by this assignment. No C++ build, native CI job,
audio matrix, benchmark, DAW session or physical experiment was run. No source
fix or repeated suite was required. Root can use this observation alongside the
separate current native and acceptance reviews, preserving the single optional
skip and the synthetic, unvalidated fidelity boundary.

## Retained artifacts

| Artifact | Bytes | SHA-256 |
| --- | ---: | --- |
| [Machine record](../../validation/python-final-20261124.json) | 176760 | `a1c5634bd91b1e8a59c2f63190753efa9d498153c1fcdbc54f4880466feec347` |
| [Complete combined log](../../validation/python-final-20261124.log.gz) | 15136 | `eec045f340d3ede9f260e69874445233d34eb539b4fbb8e5263c6d4618e25e86` |
| Decompressed log stream | 98525 | `0dd5321479af634200360343da36d3f630a6c5fabc659b14fcb2341cb3b55e87` |
