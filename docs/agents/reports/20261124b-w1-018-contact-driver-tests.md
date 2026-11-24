# W1-018 — durable contact-extrema driver tests

Date: 24 November 2026. Worker: `w1_018_contact_driver_tests`.

## Scope and result

Added **34 standard-library tests** in
[test_contact_extrema_driver.py](../../../tests/python/test_contact_extrema_driver.py)
for [validate_contact_extrema.py](../../../tools/validate_contact_extrema.py).
Only that test file and this report were authored. Product sources, numerical
budgets, canonical state, other contributors' tests, and historical reports
were not edited. No Git command was used.

The tests exercise actual temporary-file reading, exclusive output creation,
byte retention, hashes, and manifests. Numerical worker launches, trajectory
evaluation, and Git observations are mocked. Inputs are independently assembled
two-observation schema examples; their values are synthetic protocol data, not
integrated trajectories or physical evidence.

Coverage includes:

- Read-only `--help`/`--plan` under `python3 -S`, with NumPy/SciPy imports forbidden;
  frozen settings, 26 slots, six references, resource caps, and incomplete acceptance.
- Existing-output refusal, symlink leaves/ancestors, FIFO rejection before open,
  exact read allowances without sentinel bytes, changing input identity, and read errors.
- Per-file/collection budgets, all 65 input diagnostic rows with only 64 reads,
  strict malformed-input byte retention, failed producers versus missing peaks,
  and partial raw writes without refunding consumed bytes.
- Six distinct subprocess commands, no producer/candidate arguments, independent
  output paths, deadline validation, bounded waits, process-group termination,
  and preserved stdout/stderr/final/checkpoint artifacts.
- Rejected fixture, level, settings, coverage, raw-path/hash, and duration identities;
  missing final files and nonzero workers cannot resolve reference refinement.
- Source/producer mutation or absence, incoherent reference sources, known value
  failures surviving unresolved references, and coordinate failures surviving
  ambiguous time pairing.

A final aggregation test injects completed upstream collection/scaling gates
with **one** candidate to verify that a scoped pass still reports overall
`incomplete`, uncertified coverage, and both PHY-002 acceptance fields incomplete.
This does not simulate or execute the complete 26-run numerical experiment.

## Actual verification

Startup commands exited 0:

```sh
python3 tools/project.py validate
python3 tools/project.py sync --check
python3 tools/project.py next --json
```

They reported 14 tasks, 26 evidence records, fidelity `unvalidated`, current
generated state, revision 32, and no unclaimed available work.

Final focused commands both exited 0 with **34 tests passed**, no skips:

```sh
PYTHONDONTWRITEBYTECODE=1 python3 -m unittest discover -s tests/python -p test_contact_extrema_driver.py -v
PYTHONDONTWRITEBYTECODE=1 python3 -S -m unittest discover -s tests/python -p test_contact_extrema_driver.py -v
```

Reported runtimes were 0.390 s and 0.417 s respectively. An earlier authoring
run exposed an off-matrix event tolerance in the synthetic test fixture; it was
corrected to the declared 2.5 ns slot. No product correctness defect was found.
A direct line audit found zero trailing-whitespace lines.
`python3 tools/check_docs.py` also exited 0 with documentation integrity checks
passed after the report was added.

## Identity, limits, and handoff

SHA-256 observations from `sha256sum`:

| File | SHA-256 |
| --- | --- |
| `tools/validate_contact_extrema.py` | `466d6298eb966bd7b35ba778db551b6a260eef8a27f0ee408a1b173ec8a864cb` |
| `tests/python/test_contact_extrema_driver.py` | `ef87003a484426ff3cea27d2fa886ef48e0138fd94d3746d0717babdcb7fcc34` |

The existing `build/supported-extrema-initial/report.json` was studied read-only;
its hash was `d4111e0f1840fa64b622305fb181af56f6baa5baa3f8fd14874708e572f36646`.
No full reference, native producer, C++ build, full Python suite, or numerical
acceptance experiment ran here. Timeout tests use mocked processes and clock
progression; they do not claim a real 300-second timeout. Next: integrator review
and registration of this bounded regression evidence; PHY-002 remains incomplete.
