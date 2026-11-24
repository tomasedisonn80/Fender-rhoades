# Supported-impact extrema diagnostic — 2026-11-24

## Result and acceptance boundary

All **26 declared candidate runs** passed the scoped peak-value, maximizing-time,
pointwise-reference, sample-lower-bound and coordinate-pair comparisons. Six
independently generated full-duration references (R0/R1/R2 for each of two
coordinate-equivalent fixtures) resolved their peak values and times under the
predeclared refinement checks. The driver returned exit 0 for these diagnostics.

**PHY-002 remains incomplete.** Both native and reference searches explicitly
retain uncertified coverage labels. A dense-output search and agreement under
refinement do not prove that every extremum of the exact trajectory was found.
This experiment does not evaluate or waive the original 0.8 decrease rule,
general waveform/energy gates, or the ten earlier refinement failures. It does
not add calibrated instrument measurements or validate a physical Rhodes replica.

## Frozen experiment

The fixtures are `supported_impact` and `supported_impact_scaled`: 20 ms coupled
hammer/modal motion with a linear elastic hammer, zero contact damping, disabled
damper, no external actions and three impacts. Their signed coordinate transforms
must preserve physical output. The depth-one candidate matrix has 13 parameter
slots per fixture, including the original grid and declared refinements.

The independent solver is SciPy Radau (order five) with separate dense-cubic and
physical-RHS derivative searches. R0/R1/R2 cap the accepted step at 5, 2.5 and
1.25 microseconds, use relative tolerances 1e-10, 1e-12 and 1e-13, and refine the
frozen physical absolute-tolerance vector. The search includes branch endpoints,
physical derivative roots, dense derivative roots, recontacts, isolated peaks,
ambiguous clusters and disabled/plateau intervals. It records reconstruction and
physical-versus-dense derivative discrepancies separately. Source rationale and
coverage cautions appear in the [oracle implementation report](../agents/reports/20261124b-w1-008-contact-oracle.md).

The [declared plan](supported-extrema-plan-20261124b.json) retains settings,
budgets, fixtures and execution limits. Candidate-comparison budgets are
`1e-8 + 1e-4*abs(M)` m for compression,
`1e-4 + 2e-4*abs(M)` N for force, and
`1e-7 + 1e-4*abs(t)` seconds for maximizing time. Reference refinement must be
within one tenth of the declared value/time budgets; coordinate equivalence has
its separately declared, tighter physical-scale budgets. These distinct checks
must not be described as the same tolerance.

## Observed numerical results

| Quantity | Observed result |
|---|---:|
| Valid candidate slots | 26 / 26 |
| Coordinate candidate pairs | 13 / 13 passed |
| Full-duration reference solves | 6 / 6 |
| Largest compression difference from R2 | 3.137935874683584e-14 m |
| Largest force difference from R2 | 6.275868713601085e-10 N |
| Largest distance to retained maximizing-time interval | 1.562467931079392e-9 s |
| Unscaled R2 maximum compression | 0.00035691994985386573 m |
| Unscaled R2 maximum force | 7.1383989970773145 N |
| Unscaled R2 maximizing-time bracket | [0.0046025374997157465, 0.00460253750031824] s |

The largest peak is the third impact. The unscaled R2 search resolves local peak
clusters near 0.000621180, 0.002913336 and 0.004602538 s. Disabled damper values
remain exact zeros with a whole-duration maximizing interval; an arbitrary zero
time is not interpreted as a unique physical event.

The unscaled R0/R1/R2 solves accepted 6,850 / 19,100 / 33,866 steps; the scaled
solves accepted 6,972 / 20,432 / 36,428. All stayed within the frozen accepted-step,
RHS, candidate, search and wall-time caps. The midpoint physical-versus-dense
derivative discrepancy is **not monotonic**: for the scaled fixture it is about
6.32e-9, 4.38e-10 and 4.13e-9 m/s. The retained diagnostics expose this observation;
it is not a proof of derivative convergence or global coverage. The declared
peak-value/time checks and coordinate checks nevertheless passed.

## Retained evidence and reproduction

- [Complete comparison report](supported-extrema-initial-20261124b.json): all 26
  candidates, reference-at-candidate-time evaluations, comparisons, input hashes,
  source provenance, unchanged-input check and explicit unevaluated gates.
- [Reference summaries](supported-extrema-references-20261124b.json): all six
  settings, peak candidates, clusters, branches, counters, discrepancies and
  original reference-file/raw-array hashes.
- [Artifact manifest](supported-extrema-manifest-20261124b.json): hashes and sizes
  of inputs, complete original reference records, checkpoints and raw NPZ meshes.

The complete local experiment is retained under
`build/supported-extrema-initial`; the raw NPZ arrays total 86,175,359 bytes and
are not embedded in the committed JSON. They can be regenerated from the frozen
source/settings. The report binds the exact source files and producer executable;
the parent Git revision alone does not identify the then-uncommitted diagnostics.
The reference binary is the preserved original contact build, whose identity is
recorded in the report. Later test-only CMake changes do not alter that binary.

To reproduce, build `rhodes_contact_reference`, run `tools/run_contact_sweep.py`
for the two fixtures with `--extra-step-levels 1 --locate-peak-candidates`, then
pass the 26 numbered JSON files as repeated `--candidate` arguments to
`tools/validate_contact_extrema.py`, with a new `--output-dir` and the corresponding
`--producer-executable`. `--plan` is standard-library only. Reference execution
requires NumPy and SciPy. Existing outputs are refused, failed and partial inputs
are retained, and worker/resource failures cannot be promoted to successful
physics acceptance.
