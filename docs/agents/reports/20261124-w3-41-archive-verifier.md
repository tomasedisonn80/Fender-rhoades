# W3-41 — Retained VST3 matrix verification

**Agent:** `/root/w3_41_archive_verifier`  
**Assignment:** 41, wave 3  
**Result:** implemented and checked; no existing evidence altered.

## Scope and implementation

Added `tools/verify_vst3_matrix_archive.py` and
`tests/python/test_vst3_matrix_archive.py`. Root retains Git and canonical-state
ownership. Read the operating contract, README, roadmap and current state.
`project.py validate`, `sync --check` and `next --json` passed at state revision
27; the three software tasks were already claimed by the integrator.

The new CLI reads one or more retained matrix summary JSON files and their
same-directory archives. It neither extracts files nor executes archived paths,
commands, source or binaries. Historical working directories are compared as
POSIX metadata without resolving them on the current filesystem.

Input limits are explicit: 2 MiB summary, 8 MiB compressed archive, 32 MiB
decompressed tar, 4 MiB per member, 16 MiB payload total, 512 members, JSON depth
64 and 250,000 JSON value nodes. Files must be regular. Archive names are flat
and canonical; links, devices, directories, sparse files, duplicate names,
truncated members and nonzero data beyond the tar end are rejected. JSON rejects
duplicate keys, nonfinite numbers, excessive integer tokens and invalid UTF-8.

The verifier checks compressed bytes/hash, the complete indexed member hashes,
the frozen 298-member inventory and exact canonical plan digest. It recomputes
all 72 workloads and timing summaries through the repository's independent
Python verifier, compares published and retained execution records, validates
argv/affinity/sequential boundaries and recomputes all 36 repeat-pair findings.
Before/after input hashes, launcher identity, binary inventory membership,
reported Git metadata and frozen workload totals are cross-checked. Tar member
lengths are bounded and checked against actual reads; the original summary
format indexes member hashes, not a separate per-member byte-count manifest.

The normative timing summary uses explicit sorted binary64 accumulation and is
checked exactly, as is its `math.fsum` audit. The auxiliary historical Python
`mean_input_order_us` is finite-checked and any difference from the executing
interpreter is reported separately, because built-in summation behavior is not
the native summary contract. No such difference occurred in these three checks.

## Executed verification

```sh
python3 -m unittest discover -s tests/python -p test_vst3_matrix_archive.py -v
python3 tools/verify_vst3_matrix_archive.py \
  docs/validation/vst3-matrix-20261124.json \
  docs/validation/vst3-matrix-batched-20261124.json \
  docs/validation/vst3-matrix-batch-final-20261124.json
```

The 20 focused tests passed in 4.327 seconds. Cases include malformed archives
and JSON, budget exhaustion, regenerated outer hashes around changed workload,
raw timing and repeat fingerprint data, changed expectations, input-hash drift,
wrong affinity, false completion, a missing indexed member and multi-argument
failure retention. Mocks also forbid subprocess execution and filesystem
extraction during the retained-matrix test. Root then requested explicit
`O_BINARY` on platforms that expose it; the focused regular-input/read-limit
test passed again after this small transport clarification. Native Windows CI
is still required. Documentation integrity checks passed.

The three-archive CLI returned 0. Each archive independently verified 298
members, 72 complete runs, 36 matching pairs, 40,128 measured callbacks,
3,846,144 host frames, 2,136 warmup callbacks, 17,976 note events and 3,053,568
parameter points. Payload byte totals were respectively 2,536,210, 2,544,834 and
2,541,402. All source archive hashes still matched their original summaries
after testing. No C++ build, benchmark or numerical reference was rerun.

## Exact identities

| Input or changed source | SHA-256 |
| --- | --- |
| `tools/verify_vst3_matrix_archive.py` | `1cd002d895caf86a456bd5ed67ade854251de00406d5a5a2f604dd8f12ab4e68` |
| `tests/python/test_vst3_matrix_archive.py` | `1c37d622041c90392e85e8088be2ab500f5e8980f5bc1c33632bc45ad8c62583` |
| `tools/run_vst3_matrix.py` | `ebcd2fd1b0246334f6b1e13fc5c665bfd80394c8a0706d7d251fa32c93e50969` |
| Frozen plan exact file | `1a90b748a5eee9c6bec641f4386cbcaa7ab1257f8db2c0100539fb8640832ea8` |
| Original matrix archive | `366c6935c98f143c1fa3d6d6014a83058391d73391fb6bf0047629650783fad4` |
| First batch archive | `e5bcef9892ba465130592b72021daa503784acb433d5cb06ee325fa29ca67c6a` |
| Final batch archive | `1c4871b671512a5ae497e41248d07ea4eb7d9b8ee60149b21171d51cd575d91b` |

## Boundary and handoff

These checks establish internal byte integrity and frozen-protocol consistency.
They do not authenticate acquisition, reconstruct the historical binaries,
prove source-to-binary correspondence, certify a current checkout or rerun the
workloads. Hash-consistent fabrication cannot be ruled out by this tool.
Nominal-period misses remain retained observations; neither speedup causality,
device underruns, worst-case deadlines nor physical fidelity follows from a
passing archive check. RT-001 and measured fidelity are unchanged.

Next: root can publish the two source files and invoke this read-only CLI from
the evidence audit or CI; native Python CI remains the integrator's verification.
