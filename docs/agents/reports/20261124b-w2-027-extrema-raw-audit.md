# W2-027 — retained contact-extrema resource and provenance audit

Date: 24 November 2026. Worker: `w2_027_extrema_raw_audit` (027/100).
Assigned change: this report only; tiny repros stayed outside the repository.
The integrator retains Git, state, evidence registration, and product changes.

## Scope and actual checks

Read `AGENTS.md`, the README, roadmap, current state, relevant implementations,
and W1-016/W1-018 coverage. Startup `project.py validate`, `sync --check`, and
`next --json` exited 0: revision 32, 14 tasks, 26 evidence records, no unclaimed
available task, fidelity unvalidated.

Executed once, exit 0:

```sh
PYTHONDONTWRITEBYTECODE=1 python3 /workspace/scratch/cc294adc3e2f/w2_027_raw_repro.py
```

The constructed constant-state reference used 1,120 array bytes and a 2,369-byte
NPZ. It is protocol test data, not an integrated trajectory. No real worker,
full reference, full suite, build, or timing experiment ran. The fake worker was
one mocked `Popen`; the large allocation and FIFO read were intercepted.

## Findings and trust boundary

**1. Medium for externally supplied retained records: loading has no independent
resource/schema boundary.** `_load_arrays` reads the whole path, then materializes
all ZIP arrays before checking their manifest. A **270-byte** archive containing
only a 128-byte NPY header declaring `(2**40,)` reached an attempted
`np.ndarray(1099511627776, dtype=float64)`. A mock raised before allocation;
zero large allocations or decompression bombs were executed. The declared
manifest still described the original small arrays, demonstrating that the
post-load check is too late to bound allocation.

The guarded core reproducer is:

```python
header = io.BytesIO()
np.lib.format.write_array_header_1_0(header, {
    "descr": "<f8", "fortran_order": False, "shape": (2**40,)})
with zipfile.ZipFile(path, "w") as z:
    z.writestr("dense_coefficients.npy", header.getvalue())
record = {"raw": {"path": str(path), "arrays": {},
                  "sha256": hashlib.sha256(path.read_bytes()).hexdigest()}}
with patch.object(np, "ndarray", side_effect=RuntimeError("intercepted allocation")):
    reference._load_arrays(record)  # Observed: intercepted allocation.
```

Separately, a matching manifest admitted an extra array, candidate-time/state
counts 2/0, and dense shape `(1,13,5)`; evaluation still succeeded. A symlink to
the small archive succeeded. A real FIFO path reached the patched `read_bytes`
without a regular-file check; the FIFO was never opened.

The current CLI creates private worker directories and generates these arrays
itself under accepted-step/candidate limits. It does not accept external
reference JSON. These findings therefore identify a hardening prerequisite for
serialized replay/import, not a demonstrated candidate-controlled exploit.

**2. Low-to-medium provenance gap: exact bytes can change after verification.**
Replacing only the ZIP comment between `read_bytes` and `np.load` produced a
successful 20 N evaluation with unchanged array hashes, although the archive's
final SHA-256 differed from its declared SHA-256. Thus numerical content stayed
consistent while the exact archive identity no longer matched.

The driver's private evaluation cache also assumes immutable artifacts. After
one successful point comparison, appending one byte made direct evaluation fail
its hash check; repeating the same cached point still returned `pass`, with zero
evaluator calls. Cache keys contain fixture/level/time/side, without a reference
fingerprint. Source-before/after checks do not revalidate expected raw digests.
This is a local mutation/provenance issue; no whole-experiment false pass was
claimed or tested.

**3. Low, private worker protocol:** one mocked successful worker with schema
999, an unrelated source-hash map, and missing peaks was marked `completed` by
`run_reference_worker`. Fixture/settings/raw identity and duration checks still
ran. Downstream missing-record checks prevent treating this observation as an
acceptance bypass; completed execution is currently weaker than validated
reference structure. Existing incomplete-worker tests already cover nonzero,
partial, and timeout fail-closed behavior and were not repeated.

## Controls, identity, and handoff

Actual tiny checks confirmed object NPZ rejection with `allow_pickle=False`,
six invalid time/side controls rejected before raw loading, and direct changed-
byte rejection. `_load_arrays` reloads arrays; it has no hidden mutable array
cache. W1 manifest-corruption tests were reviewed, not rerun.

Source hashes were identical before and after:

| File | SHA-256 |
| --- | --- |
| `tools/contact_extrema_reference.py` | `35b1f822cef0da3036faaf8dffcfd37e9091ca3df5098650d3d306fac1e7908b` |
| `tools/validate_contact_extrema.py` | `466d6298eb966bd7b35ba778db551b6a260eef8a27f0ee408a1b173ec8a864cb` |
| Scratch repro | `b6763c0a27e46952ec608daca05c72a68eae1cec7ddcf6dde99586a3b8034100` |

Proposed focused fix: load a bounded stable regular-file snapshot; preflight
ZIP/NPY names, numeric dtypes, checked byte products, and completeness-aware
shapes/counts before allocation; parse exactly the hashed bytes. Bind cache
identity and final artifact digests to the original reference. Validate worker
schema/source-map/required fields before marking its record completed. Add the
three guarded boundary regressions above; preserve partial-reference evaluation
and all existing scientific budgets. No source edits were made here.
