# W1-011 — bounded supported-contact extrema driver

Date: 24 November 2026. Worker: `w1_011_contact_driver`.

## Outcome and boundary

Implemented [validate_contact_extrema.py](../../../tools/validate_contact_extrema.py)
for the frozen two-fixture, 26-slot depth-one experiment. The driver consumes the
[W1-009 schema](20261124b-w1-009-contact-schema.md) and W1-008 independent
reference API. It schedules six independent subprocesses, retains every input
and worker outcome, and separates candidate value, supplemental time, reference
refinement, coordinate scaling, provenance, and coverage findings.

**No full six-reference/26-candidate numerical experiment was executed by this
worker.** That run is reserved for the integrator after source freeze. The
actual verification here consists of bounded input/failure checks, mocked
comparison/orchestration checks, a short deliberately stalled worker, and
existing parser/comparison regressions. None establishes physical fidelity or
PHY-002 completion.

Only the new driver and this report were authored. The original four-fixture
tools, solver, fixture definitions, acceptance budgets, canonical task state,
and historical reports were not changed. No Git mutation or additional agent
was initiated by this worker.

## Invocation and fixed declaration

The read-only declaration requires only Python's standard library:

```sh
python3 -S tools/validate_contact_extrema.py --plan
```

An experiment uses one repeated `--candidate` per retained native JSON and a
new output directory:

```sh
python3 tools/validate_contact_extrema.py \
  --candidate PATH_TO_FIRST_NATIVE_RUN.json \
  --candidate PATH_TO_SECOND_NATIVE_RUN.json \
  --output-dir NEW_EVIDENCE_DIRECTORY \
  --producer-executable build/baseline/rhodes_contact_reference
```

The example shows argument shape only; a complete experiment requires all 26
declared slots. `--producer-executable` is optional. It is hashed before and
after the experiment, never executed, and explicitly does not authenticate the
candidate production history or establish a source-to-binary build linkage.

The plan fixes the matrix to `contact-refinement-20261123-extra-1`, with
settings SHA-256
`802f018edbad9396df0af0099cd1d0bf6b0d0a9621b0a5ae95688a283d31e17b`.
It contains 13 complete settings tuples each for `supported_impact` and
`supported_impact_scaled`, and independent R0/R1/R2 definitions for each fixture.
The standalone canonical plan, including its terminal newline, had SHA-256
`066de3bcd887da3930af448bf492acd3d29514244db8786b6782bb2a5c460fd8`.
An execution's plan additionally contains its candidate and producer paths and
therefore has its own retained hash.

## Input and worker failure behavior

- Input limits are 32 MiB per file, 128 MiB read across the collection, and
  64 files. Reads never consume an extra sentinel byte beyond those allowances.
  A complete stable regular file receives a whole-file raw hash. Oversized or
  changing files retain an explicitly identified bounded prefix; that prefix's
  hash is not presented as a complete input hash.
- Invalid UTF-8/JSON/schema records retain their original bytes before parsing.
  Every supplied input retains a diagnostic row, including files beyond the
  count limit or collection allowance. Failed producer records remain failed,
  with their failure code and partial trajectory available in the raw artifact.
- Input and output paths refuse symlinks and non-directory ancestors. Opened
  files must match regular-file metadata before and after reading. The reader
  uses nonblocking/no-follow flags where supported, so a FIFO cannot block it.
  Output uses a new exclusive directory and exclusive artifact creation;
  existing output files are never replaced by the driver.
- With at least one usable successful producer carrying peaks, the driver
  starts exactly six fresh independent reference workers using `--fixture`,
  `--level`, `--output`, and `--raw`. Candidate data, candidate event times, and
  producer executables are not supplied to these workers. A wholly unusable
  collection retains six explicit unexecuted reference rows instead of spending
  numerical work on an impossible comparison.
- Each worker has a parent wall deadline of 300 seconds, covering imports,
  integration, and ordinary finalization. A stalled worker is killed, including
  its process group on POSIX. Its stdout, stderr, final files if present, and
  W1-008 `.partial.json`/`.partial.npz` checkpoints remain available. The parent
  loads only the declared final `reference.json`; a partial checkpoint cannot
  become a completed reference by filename selection.
- The worker source itself must have a stable regular-file identity before
  execution. Nonzero exit, timeout, missing final JSON, incorrect fixture/level
  or settings, incomplete duration, and raw path/hash mismatch all prevent a
  completed-reference result. Remaining fixtures and levels still get their own
  outcome rows.

## Numerical comparisons and interpretation

The driver calls W1-008 `compare_refinement` on exactly R0/R1/R2 for each fixture.
It preserves that API's successive value differences, peak/branch classification
checks, root-cluster findings, unresolved ledger counts, and separately reported
supplemental timing refinement. It does not replace the reference comparison
with a range across three numerical answers or describe that range as an ODE
enclosure.

Each supplied successful candidate with peaks receives the following separate
checks:

| Check | Rule and retained evidence |
| --- | --- |
| Global peak value | Compare with the resolved R2 peak using `1e-8 + 1e-4*abs(M)` metres or `1e-4 + 2e-4*abs(M)` newtons |
| Value at reported time | Evaluate independent R0/R1/R2 retained trajectories at the candidate's time on both dense sides; retain the observations, branch, physical derivative, dense derivative, both pointwise refinement differences, and candidate-value error |
| Sample lower bound | Claimed maximum must cover both the largest exported observation and the separate native sampled maximum within the unchanged value budget; negative exported force is also retained as a failure |
| Supplemental peak time | Distance to all independently retained maximizing intervals with the declared `1e-7 + 1e-4*abs(t)` seconds budget; disabled/plateau intervals use membership; multiple matching intervals remain ambiguous |
| Candidate coordinate pair | All 13 unscaled/scaled setting pairs compare physical peak values with existing `SCALE_BUDGETS`; unambiguous maximizing times use the existing structural time budget |
| Reference coordinate pair | Independent unscaled/scaled references are compared at all three levels, with readiness and time pairing retained separately |

Reference evaluations are cached by fixture, level, exact reported time, and
side. A cache entry retains a failed evaluation as an error, and an evaluation
missing finite physical quantities cannot erase an already observed global or
sample-lower-bound failure. Candidate brackets remain schema evidence; they do
not constrain the independent maximizing time or root search.

`value_status`, `time_status`, `reference_refinement_status`, and `coverage`
remain separately visible. Coverage is always
`uncertified_independent_search`; both acceptance fields remain `incomplete`.
A known numerical failure gives `status: fail` even with incomplete coverage.
An otherwise successful scoped diagnostic still has overall `status:
incomplete`, while its narrower `diagnostic_status` can be `pass`.

Exit 0 means only that all required scoped numerical diagnostics passed; exit 1
records an input, numerical, or provenance failure; exit 2 records incomplete
scoped diagnostics; exit 3 records an operational artifact failure. Missing
slots, duplicate slots, failed producers, unavailable peaks, or failed reference
execution cannot yield a passing scoped experiment. Operational errors remain
visible even when they take exit-code precedence over a retained numerical
failure.

The [general convergence/energy gates](../../validation/research-gates-20261124.md)
are explicitly `not_evaluated` by this tool. The original 0.8 decrease rule is
unchanged and is neither evaluated nor waived. The plan/report preserve a link
to the [historical split result](../../validation/contact-split-depth1-20261123.json).

## Retained provenance and artifacts

Each run writes `plan.json`, `inputs.json`, `provenance-before.json`,
`provenance-after.json`, `reference-executions.json`,
`reference-refinement.json`, `report.json`, and `manifest.json`. Numbered raw
candidate bytes are under `raw/`; each fixture/level owns its own directory
under `references/`, including the worker's execution record and raw output.

The plan hashes exact fixture definitions and independently evaluated stiffness
and damping, the model declaration, and all reference settings. Source hashes
are captured before and after execution for the driver, schema, reference,
underlying independent equations, producer source/header/application, build
declaration, and relevant specification/scope files. A changed or unavailable
source blocks a passing diagnostic. Python/platform and installed NumPy/SciPy
versions, Git HEAD/working-tree observations, optional producer identity, worker
commands/exit status/elapsed time, and raw/reference paths and byte hashes are
retained. W1-008 artifacts additionally hold array identities and actual work
counts. The manifest hashes every preceding artifact and explicitly excludes
its own recursive hash.

## Actual verification

Startup commands all exited 0:

```sh
python3 tools/project.py validate
python3 tools/project.py sync --check
python3 tools/project.py next --json
```

They reported 14 tasks, 26 evidence records, fidelity `unvalidated`, current
generated state, and no unclaimed available work at state revision 32. This was
an assigned bounded implementation under the integrator's existing task claim.

An AST/plan check launched `python3 -S tools/validate_contact_extrema.py --plan`,
verified all 26 exact slots and six reference declarations, checked the frozen
matrix hash, and confirmed that neither NumPy nor SciPy had been imported.
All declared provenance source paths existed.

A temporary `python3 -S` here-document ran **18 unittest methods**, all passing
in the runner's reported 0.799 seconds. It used one already retained baseline
JSON as a schema-valid input and mocked all numerical reference generation and
evaluation. It did not execute the producer. The test methods covered:

1. Standard-library-only plan and fixed counts/hash.
2. Unchanged value budgets just below and above both thresholds.
3. A scoped comparison pass retaining incomplete coverage/acceptance.
4. A known global value failure surviving incomplete coverage.
5. A sample-lower-bound failure surviving absent references.
6. A malformed evaluation failing to mask a known global value failure.
7. Ambiguous maximizing-time pairing and disabled-interval membership.
8. Failed native producer status remaining failed.
9. Bounded byte prefixes, symlinks, and nonblocking FIFO rejection.
10. Invalid raw byte retention, artifact hashes, unexecuted reference rows,
    and refusal to overwrite the same output directory.
11. Collection/per-file allowance exhaustion using tightened test constants.
12. Every diagnostic retained when 65 files exceed the 64-file cap.
13. A real 0.15-second parent deadline killing a deliberately sleeping helper,
    with its flushed stdout and partial JSON preserved.
14. Six mocked reference calls with one input still reporting an incomplete
    matrix and exit 2.
15. Duplicate native slots remaining a failure despite mocked resolved values.
16. Optional declared producer hashing with `executed: false`.
17. An injected short raw write preserving its partial artifact and reporting
    exit 3 without refunding consumed input bytes.
18. A coordinate peak-value failure surviving ambiguous coordinate time pairing.

After adding the worker-source identity check, a separate focused check again
terminated a regular sleeping worker at the tightened 0.15-second deadline and
verified its partial artifact. It also refused a symlink worker before execution.
These are tests of the parent deadline mechanism, not a claim that an actual
300-second numerical timeout was exercised.

Existing regression commands also exited 0:

```sh
python3 -S -m unittest discover -s tests/python -p test_contact_peak_schema.py -v
python3 -S -m unittest discover -s tests/python -p test_contact_peak_comparison.py -v
```

Results were **22 schema tests** and **15 comparison tests** passed. No full
Python suite, C++ build/CTest, real Radau trajectory, or acceptance experiment
was run by this worker. The final driver had no trailing whitespace in a direct
line audit. `python3 tools/check_docs.py` also exited 0 with documentation
integrity checks passed after this report was written. Permanent new test files were outside this worker's assigned paths;
the integrator can assign that follow-up separately.

## Frozen implementation identities and next step

Observed final checkout HEAD:
`8adf435a0589b685bbc9f671d41a813808bdf016`.
The driver was uncommitted when this observation was taken.

| File | SHA-256 |
| --- | --- |
| `tools/validate_contact_extrema.py` | `466d6298eb966bd7b35ba778db551b6a260eef8a27f0ee408a1b173ec8a864cb` |
| `tools/contact_supported_schema.py` | `42d821bf535d77e7fad29da48c25dc9ac1bf2cba2fb68e85efe92afa4a82c7c7` |
| `tools/contact_extrema_reference.py` | `35b1f822cef0da3036faaf8dffcfd37e9091ca3df5098650d3d306fac1e7908b` |

The integrator can now run the exact retained 26 candidate files through this
frozen driver, inspect all six reference ledgers and the separate failure
statuses, and register the actual numerical outcome in a new report. General
contact convergence, energy criteria, coverage review, and physical validation
remain separate work.
