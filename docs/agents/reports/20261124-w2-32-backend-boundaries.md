# Wave 2 assignment 32 — Optional timing backend boundaries

Date: 2026-11-24. Worker: `w2_32_backend_boundaries`, delegated by
`w2_contact_implementation`. Starting checkout HEAD:
`cfd8ef452c2a053bf41e636668498d13ccf37dc0`.

## Result

The focused boundary checks pass after the oracle owner corrected two related
transport-validation failures. Malformed earlier refinement records no longer
claim resolved timing. Unrepresentable JSON numbers no longer escape timing
validation or prevent strict report serialization. Resolved scalar peak values
and their root-refinement evidence remain intact when optional timing fails.

Owned changes are this report and
`tests/python/test_contact_peak_backend_boundaries.py`. Oracle source changes
were made by its assigned owner, `w2_23_oracles`, after the concrete reproduction
was sent to the parent. This worker made no production, old-auditor, Git, or
canonical-state changes.

## Findings and correction

The initial oracle checked only the last record of each successful timing
refinement list. A controlled success payload with a null or negative first
compression refinement was accepted as `resolved_under_declared_refinement`.
An earlier JSON `1e999` became infinity, was also accepted, and caused
`json.dumps(result, allow_nan=False)` to raise `ValueError`.

The parent identified the closely related final `value_s = 10**400` case:
`math.isfinite` can raise `OverflowError`, which escaped the local timing
handler and reached the public handler that clears all peak values. Static
review confirmed this route; the numeric regression matrix was extended before
the final run. That additional case was not separately rerun against the old
source.

The repaired source validates every success refinement and rejects
unrepresentable JSON floats and integers locally. The final regressions confirm
inconclusive timing, empty time intervals, unchanged resolved hammer values and
scalar root evidence, and JSON-safe returned reports for each injected fault.

## Verification

Read the existing `test_contact_peak_oracles.py` first. Its tests already cover
real timeout termination, the evaluation cap, nonfinite/convergence failures,
missing SciPy through `-S`, malformed final timing, and times outside the fixture
duration. Those cases were not duplicated in this worker's test file.

The new file contains four tests:

- Process-launch `OSError` preserves values and a diagnostic reason.
- Real short subprocesses cover nonzero exit with Unicode stderr, garbled JSON
  stdout, and undecodable stderr; each preserves values and reports timing as
  inconclusive. The nonzero case retains its exit code and readable stderr.
- Null and negative earlier refinement records cannot claim resolved timing.
- Earlier `1e999` and final `10**400` numbers preserve values and strict JSON
  serialization.

Command, run from the repository root:

```sh
python3 -m unittest discover -s tests/python -p test_contact_peak_backend_boundaries.py -v
```

Initial run against the initial oracle: **4 tests, exit 1**, with 6 failing
status subtests and 1 strict-serialization error. The two ordinary process
failure tests passed. Final run after the owner fix and numeric-matrix extension:
**4 tests, all passed, exit 0, 0.304 seconds**. No whole suite was run by this
worker.

Static review also confirmed that the parent's active 10-second subprocess cap
encloses worker startup and SciPy import; timeout handling records termination.
The worker declares the frozen three quadrature tolerance pairs, 128 series
terms, a 128-subinterval limit, and a shared 10,000-evaluation cap. Worker failure
records retain prior refinements and reasons; successful records retain counts,
settings, convergence fields, estimates, and successive differences. Missing
SciPy maps to `not_available`. These observations are code review, not additional
independent numerical or timeout experiments by this worker.

## SHA-256 identities

| Artifact | SHA-256 |
| --- | --- |
| Initial `tools/contact_peak_oracles.py` | `eb9d848a41ff3d101a5b0d86bd49e1d039000103dd5b923295bfaef332bb5c68` |
| Final tested `tools/contact_peak_oracles.py` | `74030851ce9511cd50c6952a11b43bfdf5e8da5d7fd10428c009be0210060789` |
| Final `tests/python/test_contact_peak_backend_boundaries.py` | `5868ec0c2428c2105beeec80b116efb4b3b0c4f4a664801c7ee65e50039d1166` |
| Existing `tests/python/test_contact_peak_oracles.py` | `9de8d7907b7a68fccf77788d9f04c93d4059d343bb4e6b1bacdc1b26bc2be1e6` |
| Frozen `docs/specs/CONTACT_ANALYTIC_PEAK_SLICE_20261123.md` | `56e5caefaebc03596b2ed566d50578b2be2e4dc54e37bf7fdce31dab31243f03` |

## Limits and handoff

This is local, offline diagnostic evidence for the four frozen synthetic
fixtures. Artificial worker payloads exercise failure handling; they are not
observed SciPy numerical failures. Error estimates and refinement agreement are
not rigorous certificates. This audit does not establish physical calibration,
platform coverage, or continuous-extrema acceptance. PHY-002 and its
continuous-extrema gate remain **incomplete**; frozen budgets and the old auditor
remain unchanged.

Next action: the integrator can include the owner repair and these focused
regressions in the diagnostic slice, preserving the above source identities and
existing acceptance limitations.
