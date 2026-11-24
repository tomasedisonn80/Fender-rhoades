# W1-016 — independent supported-contact extrema regressions

Date: 24 November 2026. Worker: `w1_016_extrema_tests` (016/100, wave 1/5).
Assignment: add only `tests/python/test_contact_extrema_reference.py` and this
report. The root owns Git, claims, canonical state, evidence registration and the
six-reference production experiment. No subagent was spawned.

## Source binding and delivered scope

The tested implementation is the frozen module documented by
[W1-008](20261124b-w1-008-contact-oracle.md):

| File | SHA-256 |
|---|---|
| `tools/contact_extrema_reference.py` | `35b1f822cef0da3036faaf8dffcfd37e9091ca3df5098650d3d306fac1e7908b` |
| `tests/python/test_contact_extrema_reference.py` | `48e319d7b5c5555aa3433648d19738ef011f1d03d882ca85a1f8e88f330d4c96` |

The implementation hash was checked before and after the tests. No source file,
CMake file, older four-fixture test, acceptance document or state was edited.
The new test file contains 32 tests across six classes:

- **Standard-library boundary:** numerical imports are explicitly blocked in a
  `python -S` subprocess while importing the reference and executing the driver's
  read-only `--plan`. The declaration retains six references, 26 candidate slots,
  unchanged settings, uncertified coverage and incomplete PHY-002 status.
  Unsupported or modified fixtures, unknown levels, invalid/raised caps and an
  existing raw destination are rejected before numerical backend entry.
- **Public cubic reconstruction:** a callable exposing no private dense-output
  fields reconstructs an independently written three-component cubic. The suite
  checks known coefficients and extra evaluation points, including an interval
  only seven floating-point ULPs wide whose normalized thirds are inexact.
  Noncubic, scalar, nonfinite and unrepresentable sample inputs fail explicitly.
- **Roots and bisection:** exact dyadic factors supply two close roots hidden from
  the quarter-point sign scan. Independent expected roots remain in disjoint
  brackets no wider than `1e-12 s`. Even and triple roots preserve unresolved
  conditioning/tangency records; a sign bracket for an odd repeated root does not
  erase that ledger. Exact polynomial plateaus, tiny nonzero constants and
  endpoint zeros retain distinct labels. Bisection checks independent square-root
  residuals, endpoint roots, the iteration cap, nonfinite values, failed sign
  brackets, unrepresentable midpoints and charging before function evaluation.
- **Retained evaluation:** test-authored cubics have physical velocity zero at
  `0.002 s` and dense-position derivative zero at `0.003 s`. The suite checks both
  derivative values, force derivatives, disabled damper identity despite positive
  geometric gap, exact left/right endpoint selection and no extrapolation. The
  signed `(2, -3)` coordinate transformation preserves contact, energy, pickup and
  acceleration observables. Independent manifests detect inline mutations, changed
  NPZ bytes, changed values, reshaped arrays, altered dtypes and missing arrays,
  including when the outer file hash is recomputed.
- **Refinement decisions:** synthetic records exercise both successive differences
  immediately below, exactly at and one floating-point step above `0.1*B(M_R2)`
  for compression and force. Zero R2 values make the boundary exact without
  subtractive rounding. Additional tests check nonzero R2 budget evaluation,
  independent time status, missing/ambiguous clusters, branch/classification
  changes, unresolved/incomplete inputs and source/fixture/settings drift.
- **Live enforcement and retention:** four real generator calls tighten one cap
  to one: accepted steps, RHS evaluations, search evaluations or candidates.
  They all return explicit `BudgetExceeded` and retain initial/partial arrays.
  The accepted-step case also verifies final and checkpoint file hashes, embedded
  manifests, a two-row mesh, one dense segment and inconclusive hammer values.
  Scaled-fixture capped calls retain the declared unscaled atol vector.

Expected coefficients, roots, physical formulas, settings and byte hashes are
derived in the tests; the implementation's polynomial, serialization and budget
helpers are not used as expected-value oracles. Constructed retained cubics and
comparison records are labeled test inputs, not ODE trajectories.

## Actual checks

Startup checks all exited 0: `python3 tools/project.py validate`,
`python3 tools/project.py sync --check`, and `python3 tools/project.py next --json`.
The state had 14 tasks, 26 evidence records, unvalidated fidelity and no unclaimed
available task. No competing claim was made.

| Command | Observed result |
|---|---|
| `python3 -m unittest discover -s tests/python -p test_contact_extrema_reference.py -v` | Exit 0; 32 passed in 0.693 s |
| `python3 -S -m unittest discover -s tests/python -p test_contact_extrema_reference.py -v` | Exit 0; 17 passed and 15 optional numerical tests skipped in 0.121 s |
| `python3 -m py_compile tests/python/test_contact_extrema_reference.py` | Exit 0 |
| `git diff --no-index --check /dev/null tests/python/test_contact_extrema_reference.py` | Exit 1 for the new-file difference; no whitespace diagnostics |
| `python3 tools/check_docs.py` | Exit 1; an unrelated concurrent report link was missing, as identified below |

A standard-library subprocess wrapper inspected `git diff --no-index --check`
for both assigned new files and exited 0 with no whitespace diagnostics. The
documentation check reported only
`docs/validation/silent-observation-20261124b.md:37`, whose relative link to
`../agents/reports/20261124b-w1-014-silent-tests.md` did not yet exist. That report
belongs to another worker; neither file was changed here. This global check is
not recorded as passed.

Observed environment: Linux; Python 3.12.14, NumPy 2.3.5 and SciPy 1.17.0.
Optional NumPy-only checks and NumPy/SciPy integration checks have separate skip
guards. Standard-library tests still run without either dependency.

## Findings, limitations and handoff

No implementation defect was exposed by this assigned coverage. The tests verify
API behavior, independent algebraic expectations, refinement decisions and bounded
failure retention. They do not certify continuous-domain extrema, solver accuracy,
physical fidelity, native-platform behavior or completion of PHY-002.

No complete R0, R1 or R2 trajectory, six-reference experiment, full Python suite,
CMake build or CTest run was performed by this worker. The real solver calls each
accepted at most one step. The root's production experiment remains separate.

Next: integrate this test file into the ordinary discovery suite, retain the
source-bound production results and review the remaining independent-refinement,
coverage, energy and candidate-comparison gates without changing their budgets.
