# W4-64 — Frozen benchmark capture and archive guide

**Agent:** `/root/w4_64_benchmark_guide`  
**Assignment:** 64, wave 4  
**Result:** documentation updated; no implementation or historical evidence changed.

## Changes

Replaced the obsolete “planned” comparison section in `docs/BENCHMARKING.md`
with the implemented frozen 72-process workflow. The guide now specifies Linux
affinity support, the fixed `build/vst3` path, Release/Ninja, exported compile
commands, uninstrumented binaries, matching clean SDK pins, and freshness.
Its configure/build example and launcher arguments follow the actual source
and help output. It distinguishes the resolved exclusive destination, retained
attempts, incomplete results, operational failure, and unattempted runs.

The three retained captures remain separately linked. Their Q6 miss counts and
maxima include the worse first batch and the larger final outlier. The guide
states the per-variant denominator while retaining the warning against treating
mixed workloads as comparable population rates. The 16-process CPU observation
is explicitly whole-child user CPU, including setup/warmup/reporting; the rounded
7.2% observation is not promoted into a causal or general speedup.

A separate read-only command checks all three sibling archives. The guide
explains exact native summaries, auxiliary Python-version sum differences,
continued processing after a rejection, and byte consistency versus acquisition
authenticity or current-source equivalence. Historical callback sections remain
unchanged. Numerical identity, allocation, device timing, physical fidelity and
unfinished RT-001 gates remain distinct.

## Actual checks

```sh
python3 tools/project.py validate
python3 tools/project.py sync --check
python3 tools/project.py next --json
python3 tools/run_vst3_matrix.py --help
python3 tools/verify_vst3_matrix_archive.py --help
python3 tools/check_docs.py
git diff --check -- docs/BENCHMARKING.md docs/agents/reports/20261124-w4-64-benchmark-guide.md
```

These checks passed. State revision 27 contained 14 tasks and 21 evidence
records, with fidelity unvalidated and the software work claimed by root.
Read-only JSON checks confirmed 72 runs and 40,128 callbacks in each capture;
Q6 miss/max pairs were `2947 / 91441.456`, `3852 / 96939.447`, and
`1716 / 99882.967` (maxima in microseconds). The plan records 3,846,144 frames.
All nine supporting source/evidence hashes below were unchanged across editing.

No build, benchmark, unit-test suite, archive-verifier execution, numerical
replay or hardware experiment ran. No Git publication, ledger or canonical-state
mutation occurred. Root can publish the two assigned documentation files; the
new examples are source/help-checked instructions, not newly executed captures.

## Exact identities

| File | SHA-256 |
| --- | --- |
| `docs/BENCHMARKING.md` | `02439bddb473c9abeb46714ee48c84030f2e06bafe8144e70a2b61214b82a4e8` |
| `tools/run_vst3_matrix.py` | `c91f2ec42bd2934120d63599b4d68c68295a99b2090f8bfc0b358ebe1e9c4159` |
| `tools/verify_vst3_matrix_archive.py` | `4fc824ebe5038fcda4170415b002dd844c53a0ea167705cdbee03a9f23a32162` |
| `docs/specs/QUADRATURE_BENCHMARK_20261123.md` | `f8c1af7a5acc7a31ba2d179492d3b08d20e86867e294a05e9622b2ea778cc14e` |
| `docs/validation/quadrature-benchmark-plan-20261123.json` | `1a90b748a5eee9c6bec641f4386cbcaa7ab1257f8db2c0100539fb8640832ea8` |
| `docs/validation/batched-quadrature-20261124.md` | `9a22e4fc3a2cb6373b04f67b8998a4edac5dd1a1d95484ce9cab0b65a170619c` |
| `docs/validation/q6-cpu-paired-20261124.json` | `e678e3a9e1610e21d33458fd9c945861adb03c3fe490533026d47b752b8511c7` |
| `docs/validation/vst3-matrix-20261124.json` | `021648832aa392fc68b1c7235db79c6489da93798a48c68321459985ac57328d` |
| `docs/validation/vst3-matrix-batched-20261124.json` | `264b56c4500a4ff5a80d40cb9c1af03339ddc4472d4d803f5ae9fa34384073f6` |
| `docs/validation/vst3-matrix-batch-final-20261124.json` | `e0c00b865e9a274be69e8d384ba9c31001e9bc27c3958ada00e1c5ffe78b54a9` |
