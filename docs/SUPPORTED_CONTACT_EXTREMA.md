# Supported-impact contact extrema

[`validate_contact_extrema.py`](../tools/validate_contact_extrema.py) compares
native peak candidates with independent numerical references for exactly
`supported_impact` and `supported_impact_scaled`. These are synthetic 20 ms
hammer/modal fixtures with an elastic hammer, zero contact damping, a disabled
damper and no external actions. The scaled fixture changes modal coordinates
while preserving physical observables.

The declared experiment has **26 native inputs: 13 settings per fixture**, plus
**six independent references: R0/R1/R2 for each fixture**. It checks peak values,
maximizing-time intervals, reference values at candidate times, sampled lower
bounds and coordinate equivalence. **PHY-002 remains incomplete after a passing
diagnostic.** Both searches retain uncertified coverage; agreement under
refinement does not prove that every continuous-time extremum was found.
The [four-fixture analytic diagnostic](CONTACT_PEAK_DIAGNOSTIC.md) and
[general contact audit](CONTACT_REFERENCE.md#generate-and-independently-audit-a-sweep)
have separate scopes.

## Inspect the plan, then generate native inputs

Run commands from the repository root with Python 3.11 or later. Help and the
driver's plan use only the standard library and perform no numerical work:

```sh
python3 -S -B tools/validate_contact_extrema.py --help
python3 -S -B tools/validate_contact_extrema.py --plan
python3 -S -B tools/contact_extrema_reference.py --help
```

Build `rhodes_contact_reference` using [Development](DEVELOPMENT.md), then use
its actual executable path below. A single-configuration build commonly places
it at `build/rhodes_contact_reference`; a Visual Studio build commonly uses
`build/Release/rhodes_contact_reference.exe`.

```sh
python3 tools/run_contact_sweep.py \
  --executable build/rhodes_contact_reference \
  --output-dir build/supported-inputs-001 \
  --fixture supported_impact \
  --fixture supported_impact_scaled \
  --extra-step-levels 1 \
  --locate-peak-candidates
```

Adding `--plan` to that sweep prints its declaration without executing the
producer or creating files. `--extra-step-levels 1` selects the required 13
distinct settings per fixture; `--locate-peak-candidates` requests the peak
diagnostics omitted by default. The generator captures stdout as bytes, retains
stderr and exit codes, and produces numbered one-run JSON files plus its own
manifest and aggregate. Inspect its result before comparison. Generation alone
does not establish numerical acceptance.

## Run the independent comparison

Reference execution requires the optional
[NumPy/SciPy audit dependencies](DEPENDENCIES.md#optional-independent-python-audit).
The C++ producer and the plan do not require them. The driver reads native JSON;
it never launches the native executable. Its optional `--producer-executable`
records a declared binary identity before and after comparison, without
attesting that the binary produced supplied files.

Pass the **26 numbered files individually**, excluding `manifest.json` and the
multi-run `candidates.json`. This Python snippet avoids shell glob expansion and
works with the corresponding native executable path on each platform:

```python
from pathlib import Path
import subprocess
import sys

producer = Path("build/rhodes_contact_reference")  # Set your actual build path.
inputs = sorted(Path("build/supported-inputs-001").glob(
    "[0-9][0-9][0-9][0-9]_*.json"))
if len(inputs) != 26:
    raise SystemExit("Expected the 26 numbered native inputs; inspect generation.")
command = [sys.executable, "tools/validate_contact_extrema.py",
           "--output-dir", "build/supported-extrema-001",
           "--producer-executable", str(producer)]
for path in inputs:
    command.extend(["--candidate", str(path)])
raise SystemExit(subprocess.call(command))
```

The output directory must be new, its parent must already exist, and path
ancestors must be real directories. Each invocation schedules fresh references;
the CLI does not import or resume a previous experiment. With usable candidate
input, it runs six serial workers. A wholly unusable collection records all six
as unexecuted. Candidate failures remain visible alongside any available
comparisons.

Each worker uses independent Radau integration and retained dense cubics, with
separate physical-RHS and dense-derivative searches. The full absolute-tolerance
vectors and fixture definitions are in `--plan`:

| Level | Maximum step | Relative tolerance | Absolute-vector multiplier |
| --- | ---: | ---: | ---: |
| R0 | 5 µs | `1e-10` | 10 |
| R1 | 2.5 µs | `1e-12` | 1 |
| R2 | 1.25 µs | `1e-13` | 0.1 |

Default per-worker work caps are 200,000 accepted steps, 5,000,000 RHS
evaluations, 1,000,000 search evaluations and 100,000 candidates. Both the
cooperative wall cap and parent-enforced subprocess deadline are 300 seconds
**per worker**. The parent can terminate a stalled import or solver call and
retains available partial artifacts. These are offline limits, not audio
callback guarantees.

## Read values, timing and coverage separately

For an independent maximum `M`, the value budgets are:

| Check | Budget or rule |
| --- | --- |
| Compression comparison | `B(M) = 1e-8 + 1e-4*abs(M)` metres |
| Force comparison | `B(M) = 1e-4 + 2e-4*abs(M)` newtons |
| Reference values, R0→R1 and R1→R2 | Each difference must be at most `0.1*B(M_R2)` |
| Supplemental timing | `1e-7 + 1e-4*abs(t)` seconds |
| Successive isolated-reference timing | Each interval-centre difference uses the **full** supplemental budget at the finer time |

Timing also requires compatible classifications and unambiguous physical peak
cluster pairing. Disabled damper zeros have a whole-duration maximizing
interval; they do not identify a unique timestamp. Coordinate comparisons have
separate tighter physical-scale budgets, retained in the plan.

The [refinement clarification](validation/supported-extrema-clarification-20261124b.md)
corrects an earlier narrative timing statement. The field
`both_reference_refinement_fractions: 0.1` refers to the **two successive value
comparisons**, not a one-tenth timing requirement. No scientific budget changed.

| Exit | Driver meaning |
| --- | --- |
| 0 | All scoped diagnostics passed; `diagnostic_status=pass`, while overall `status=incomplete` and `phy002_acceptance=incomplete` remain. |
| 1 | Input, numerical or provenance failure. |
| 2 | Incomplete diagnostics, such as missing slots or inconclusive references. Invalid command-line usage also returns 2. |
| 3 | Operational artifact failure, such as output creation, retention or inspection failure. |

Operational failures take precedence, and a definite failure survives other
incomplete comparisons. Read `report.json` and `manifest.json`, including the
separate value, time, reference-execution, refinement and provenance fields.
The original 0.8 decrease rule, general energy/convergence checks, historical
failures and instrument calibration remain separate gates.

## Retained evidence and bounded raw files

The driver retains the plan, numbered input bytes and records, commands,
stdout/stderr, worker execution records, reference JSON/NPZ files and available
checkpoints, comparisons, source identities, final report and artifact manifest.
Inputs are limited to 64 files, 32 MiB per file and 128 MiB collectively.
Oversized or interrupted reads retain an explicitly incomplete prefix; that
prefix's hash is not the identity of the complete input.

Worker completion requires the frozen schema, fixture, settings, source map,
limits/counters, peak records and retained-array identities. Sources and an
optional producer identity are checked before/after. Final artifact hashes are
compared with their original retained identities; missing or changed bytes make
provenance fail. Cached point evaluations are bound to reference metadata and
current file identity.

Final-format NPZ files have exactly eleven named arrays. For `M` mesh rows,
`D` dense segments and `K` candidate rows, their shapes are:

| Array names | Shape |
| --- | --- |
| `mesh_t_s`, `mesh_y` | `(M,)`, `(M,13)` |
| `dense_t0_s`, `dense_t1_s` | `(D,)` each |
| `dense_coefficients` | `(D,13,4)` |
| `reconstruction_residual`, `reconstruction_limit` | `(D,13)` each |
| `candidate_time_s`, `candidate_y` | `(K,)`, `(K,13)` |
| `candidate_segment_id`, `candidate_origin_bits` | `(K,)` each |

The first nine arrays use little-endian binary64; segment IDs use signed
64-bit integers and origin bits use unsigned 64-bit integers. The loader checks
names, dimensions, dtypes, byte products, ZIP metadata and NPY headers before
array allocation. It rejects symlinks and special files, verifies one bounded
stable regular-file snapshot, and passes those exact hashed bytes to NumPy.
Per-array payload hashes remain required.

Default ceilings are **163,200,112 array bytes**, **163,245,168 total
uncompressed NPY bytes**, and **165,922,640 compressed snapshot bytes**. Headers
are limited to 4,096 bytes each and ZIP central metadata to 65,536 bytes. These
are representation/storage ceilings, not a peak-memory or whole-experiment
disk-space guarantee. Tightened work caps also tighten storage ceilings.

Complete records require nonempty dense/candidate data and `M=D+1`. Partial
records may retain empty arrays or one accepted endpoint without its dense
segment. They remain inspectable and inconclusive; evaluation cannot extrapolate
beyond the retained dense prefix. Periodic checkpoints have a separate metadata
format and are never promoted to completed references. The
[hardening report](agents/reports/20261124b-w2-030-extrema-raw-hardening.md) and
[independent tests](agents/reports/20261124b-w2-031-extrema-raw-tests.md) give the
exact representation and failure cases.

## Reference API and recorded results

For developer inspection, `contact_extrema_reference.py` exposes
`generate_reference`, `evaluate_reference(..., side="left"|"right")`,
`compare_refinement([r0, r1, r2])`, `inspect_retained_raw` and
`raw_storage_limits`. The last two use only the standard library. A standalone
worker uses `--fixture`, `--level`, `--output`, optional `--raw`, and optional
`--limits-json` to **tighten** caps. Its output paths are exclusive; the NPZ
defaults to the JSON path with a `.npz` suffix. Its cooperative timeout needs an
owning subprocess deadline for stalled imports/native calls; the experiment
driver supplies that deadline. A worker's `completed` result still needs
cross-level refinement and says nothing about certified coverage.

The [initial comparison record](validation/supported-extrema-initial-20261124b.json)
retains the observed 26 passing scoped comparisons. Its
[reference summaries](validation/supported-extrema-references-20261124b.json) and
[artifact manifest](validation/supported-extrema-manifest-20261124b.json) bind the
six references and raw files; the NPZ data are not embedded in those committed
summaries. Read the narrative with the timing clarification linked above.

The separate [native replay](validation/supported-native-replay-20261124b.json)
and its [report](agents/reports/20261124b-w2-026-supported-native-replay.md)
reproduced all 26 candidate outputs byte for byte after the grazing change.
It binds those selected new-source outputs to the initial comparisons without
claiming another six-reference solve. Later retained-file hardening preserved
the numerical algorithms and loaded the six historical final NPZs unchanged;
it did not repeat the experiment or establish physical fidelity.
