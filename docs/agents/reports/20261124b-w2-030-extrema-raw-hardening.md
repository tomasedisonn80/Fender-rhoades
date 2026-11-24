# W2-030 — retained contact-extrema import and provenance hardening

Date: 24 November 2026. Worker: `w2_030_extrema_raw_hardening` (030/100).
Integrator owns Git, CMake, canonical state, evidence registration, and existing
test-fixture updates. This worker changed only the two assigned tools and this
report. Worker 031 owns the new adversarial test module and its report.

## Scope and result

Implemented the focused findings in
`20261124b-w2-027-extrema-raw-audit.md`, after reading its guarded scratch
reproducer. This is serialized replay/import hardening and reliable artifact
provenance. It is not evidence of a candidate-controlled whole-experiment
exploit, a numerical revalidation, or completion of PHY-002/RT-001.

`contact_extrema_reference.py` now checks the fixed manifest, file identity,
ZIP directory, and every NPY header before NumPy allocates array storage. It
loads exactly the bounded byte snapshot whose SHA-256 was verified. Leaf and
ancestor symlinks, FIFOs, and other nonregular inputs are refused; size and
before/after handle/path signatures detect ordinary mutation/replacement.
All eleven arrays must have their existing names, canonical numeric dtypes,
compatible row counts, and checked byte products. Payload hashes remain
mandatory. Inline constructed-cubic records receive the same bounded manifest
checks and bounded shape/type traversal before conversion.

The driver validates schema/version/kind, both recorded source identities,
the frozen fixture definition and hash, exact level settings, declared limits
and counters, completion/duration/failure fields, all four peak structures,
required diagnostic lists, and retained NPZ headers/payload identities before
marking a worker completed. Invalid and partial bytes remain on disk and in
the artifact inventory. A structurally valid capped record remains available
for inspection while its execution remains inconclusive. An unresolved search
ledger is preserved and is not silently cleared by protocol validation.

Cached point evaluations now include a hash of their relevant reference
metadata and a current regular-file signature. A changed file or reference
identity cannot reuse the prior successful cache entry. Cache hits only check
metadata; cache misses retain the complete evaluator validation. The existing
final streaming artifact inventory is reused to compare actual digests with
the original candidate, JSON, raw-reference, and worker-artifact identities.
Changes/missing bytes make provenance fail, preserve the expected digest, and
report the observed digest/status. An earlier worker inventory cannot replace
the already-validated raw/JSON digest. Worker-local inventory drift also makes
that execution inconclusive.

No numerical algorithm, level setting, work budget, tolerance, refinement rule,
historical artifact, or 26-candidate experiment was changed or rerun.

## API and explicit storage limits

Existing `_load_arrays` and `evaluate_reference` signatures are unchanged.
New standard-library-only helpers are:

- `raw_storage_limits(limits=None)`: returns `array_bytes`,
  `uncompressed_bytes`, and `compressed_bytes` ceilings.
- `inspect_retained_raw(reference)`: validates a final-format NPZ, including
  streamed per-array payload hashes, without NumPy or ndarray allocation. It
  returns `sha256`, `bytes`, `signature`, `array_bytes`, and
  `uncompressed_bytes`.

Let `S` be the accepted-step cap and `C` the candidate cap. The existing arrays
use at most `112*(S+1) + 640*S + 128*C` data bytes. Each NPY header, including
magic/version/length, is bounded to 4,096 bytes. ZIP central metadata is bounded
to 65,536 bytes and exactly eleven members; the compressed-file ceiling adds
conservative DEFLATE expansion and metadata allowance.

| Ceiling | Default bytes |
| --- | ---: |
| Array payloads | 163,200,112 |
| Total uncompressed NPY members | 163,245,168 |
| Compressed snapshot | 165,922,640 |

Tightened existing work limits also tighten these ceilings. The loader accepts
NPY versions 1/2/3 with canonical C-order `<f8`, `<i8`, and `<u8` storage and ZIP
stored/DEFLATE compression. Production archives already use this representation;
the six historical final NPZs passed unchanged. Arbitrary dtype, Fortran-order,
extra-member, oversized-header, or ZIP64 end-directory imports are not the
declared final format. No new dependency was introduced.

For mesh rows `M`, dense rows `D`, and candidate rows `K`, the shapes are fixed:
mesh times/state `(M,)`/`(M,13)`; dense times `(D,)`, coefficients `(D,13,4)`,
residuals/limits `(D,13)`; candidate times/segment IDs/origin bits `(K,)` and
states `(K,13)`. Complete records require nonempty dense/candidate rows and
`M=D+1`. Partial records permit all-empty arrays or `M=D+1`/`M=D+2`, preserving
one accepted endpoint interrupted before dense reconstruction. Declared
accepted-step/candidate counters must equal the retained counts. Public
constructed-cubic records may still omit optional counters/limits and receive
the default storage ceilings. Evaluation checks finite data, mesh ordering,
the dense prefix, candidate identifiers, and the declared final time.

Periodic checkpoint NPZs already have a distinct twelfth metadata member and
a separate JSON format. They are preserved as evidence, not accepted as a
final-reference NPZ or silently promoted. No new restriction rejects the
generated partial final records checked below.

## Actual validation

Startup commands, all exit 0:

```sh
python3 tools/project.py validate
python3 tools/project.py sync --check
python3 tools/project.py next --json
```

Observed revision 32, fourteen tasks, twenty-six evidence records, no available
unclaimed task, and fidelity `unvalidated`.

Targeted commands:

```sh
PYTHONDONTWRITEBYTECODE=1 python3 -m unittest discover -s tests/python -p test_contact_extrema_reference.py -v
PYTHONDONTWRITEBYTECODE=1 python3 -S tools/validate_contact_extrema.py --plan
```

Reference tests: **32 passed**. The initial run had three expected-error-message
regex failures because rejection moved ahead of allocation; the legacy
`per-array hash/shape/dtype mismatch` phrase was retained and the focused rerun
passed. No existing reference-test fixture was changed. The plan command exited
0 under `-S`; it did not import numerical backends or run workers.

Worker 031 then executed its final independent tests against the frozen source
hashes below (reported directly to this worker):

```sh
PYTHONDONTWRITEBYTECODE=1 python3 -m unittest discover -s tests/python -p test_contact_extrema_raw.py -v
PYTHONDONTWRITEBYTECODE=1 python3 -S -m unittest discover -s tests/python -p test_contact_extrema_raw.py -v
```

Results: **37/37 passed** normally; under `-S`, **33 passed and four explicit
NumPy skips**. These include guarded huge-header/member-size cases, regular-file
and bounded-read checks, snapshot replacement, exact-byte loading, worker
schema/source/peak corruption, cached identity changes, and final changed,
missing, or inventory-time-mutated raw/JSON/candidate artifacts. Its test-file
SHA-256 was `cda6314a9237f3df73930d83ec6b072eb3ec7fd6610e07799a5e1fead0b6bf2f`.
Its earlier inventory-mutation harness expected completed executions; that
expectation was corrected to require the stronger early inconclusive result.

Five tiny capped generated R0 references and a guarded SciPy import failure
passed `inspect_retained_raw`, `_load_arrays`, and the worker structure validator:

| Tightened cap or guard | Mesh rows | Dense rows | Candidate rows |
| --- | ---: | ---: | ---: |
| `accepted_steps=0` | 1 | 0 | 1 |
| `accepted_steps=2` | 3 | 2 | 4 |
| `rhs_evaluations=0` | 1 | 0 | 1 |
| `search_evaluations=2` | 2 | 0 | 1 |
| `candidates=0` | 1 | 0 | 0 |
| Guarded SciPy import failure after NumPy import | 0 | 0 | 0 |

All remained `inconclusive_reference`; every capped call used a tightened
10-second cooperative wall cap. Existing retained dense coverage was evaluated
where present. The `M=2,D=0` case specifically verifies the unmatched endpoint
rule. The inline validation command used precisely the controls above and
`generate_reference('supported_impact', 'R0', raw_path=<new temporary NPZ>)`;
the import guard raised `ImportError` only for `scipy`.

The following exact read-only command loaded the six historical final records;
it ran no solver or experiment:

```sh
PYTHONDONTWRITEBYTECODE=1 python3 - <<'PY'
import hashlib, json, pathlib, sys
sys.path.insert(0, 'tools')
import contact_extrema_reference as r
records = sorted(pathlib.Path('build/supported-extrema-initial/references').glob('*/reference.json'))
assert len(records) == 6, len(records)
for path in records:
 ref = json.loads(path.read_text())
 arrays = r._load_arrays(ref)
 print(json.dumps({'reference': str(path), 'status': ref['status'], 'mesh': len(arrays['mesh_t_s']), 'dense': len(arrays['dense_t0_s']), 'candidates': len(arrays['candidate_time_s']), 'raw_sha256': ref['raw']['sha256'], 'raw_bytes': pathlib.Path(ref['raw']['path']).stat().st_size}, sort_keys=True))
 del arrays
print('six historical retained references loaded; zero solves')
PY
```

All six passed. Dense row counts were 6,850/19,100/33,866 for the base fixture
and 6,972/20,432/36,428 for the scaled fixture; corresponding candidate counts
were 9,788/22,961/38,105 and 9,961/24,275/41,145. This checks backward-compatible
loading and recorded byte identity, not their original numerical conclusions.

## Handoff and frozen source identity

The integrator was given the exact stricter fields needed by the existing
synthetic driver fixture: real eleven-array tiny NPZ/manifest, both temporary
source files and their hashes, schema/kind/frozen fixture definition, full
limits/counters, completion fields, existing raw representation metadata, and
the required peak/status/candidate fields. Existing driver tests intentionally
used placeholder bytes and omitted fields; this worker did not edit those
tests. The integrator subsequently migrated those fixtures using worker 031's
independent standard-library tiny NPZ helper and reported **all 34 existing
driver tests passed**. Integration still owns the combined targeted suite and
normal project gates; it need not repeat the full reference solves or experiment.

| File | SHA-256 |
| --- | --- |
| `tools/contact_extrema_reference.py` | `39bed1d73f4482455c9268846987aa91a4200188b0eb83ff0efbc5759a71263e` |
| `tools/validate_contact_extrema.py` | `65e38326fd3f07566ca37d84513b34a0be24bf916f8d5f4ff541fb2225325dfe` |
| Reviewed W2-027 scratch reproducer | `b6763c0a27e46952ec608daca05c72a68eae1cec7ddcf6dde99586a3b8034100` |

No Git command, state mutation, CMake change, full test suite, full six-reference
solve, or current 26-run experiment was performed by this worker.
