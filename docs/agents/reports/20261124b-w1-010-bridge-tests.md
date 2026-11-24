# Worker 010 — independent filtered-error bridge tests

Date: 2026-11-24. Added only
`tests/python/test_filtered_error_bridge.py` and this report. Read the required
starting documents, worker-002 equations, worker-007 report, bridge, independent
reference, and existing tests. Root retains state and Git ownership.

## Coverage and result

All **10 focused tests passed** with optional numerical dependencies present.
An actual `-S` run passed both standard-library import/plan tests and cleanly
skipped all eight numerical tests. Python/NumPy/SciPy observed:
`3.12.14` / `2.3.5` / `1.17.0`.

- Explicit per-cell sums check trapezoids and normalized rectangle differences
  for constant, single-step, adjacent internal/host-step, and smooth sources at
  L=1/2/4. Both nonzero support endpoints and current/interior/far event weights
  are checked, including read-only source inputs and zero epoch prehistory.
- Two simultaneous resets with intervening strikes check erasure of both old
  motion/filter history and same-time pre-reset strikes. Metadata retains
  producer order and physical jumps while effective erased jumps remain zero.
- Zero-intensity note-on matches release, adds no voltage jump, and retains its
  original inclusive event window. Exact left propagation is checked against
  independent SciPy matrix exponentials, with guards rejecting perturbed
  scalar propagation times or off-grid source queries.
- Invalid finite values, shapes, factors, normalizers, epoch partitions,
  attribution rows, host vectors/rates, and duck-typed cases are rejected.
- The four named decomposition terms, closure, regional counts, peak location,
  signed mean/RMS, frame 816, metadata and caller immutability are checked.
  Omitting one event leaves its independently predicted contribution in the
  attribution residual while telescoping closure remains small.
- An explicitly synthetic algebra fixture preserves an original resolved B
  failure despite closed bookkeeping. Original A/B/refinement gates and the
  fixed full-case plan hash remain frozen. This fixture is not a new Q64
  integration or production comparison.

## Executed commands

```sh
python3 -B tools/project.py validate
python3 -B tools/project.py sync --check
python3 -B tools/project.py next --json
python3 -B -m unittest discover -s tests/python -p test_filtered_error_bridge.py -v
python3 -S -B -m unittest discover -s tests/python -p test_filtered_error_bridge.py -v
python3 -B -c 'import platform, numpy, scipy; print(platform.python_version(), numpy.__version__, scipy.__version__)'
```

Preflight passed at revision 32 (14 tasks, 26 evidence records; no unclaimed
available task). Numerical tests passed initially and again after strengthening
the four-term and metric assertions; final numerical run: 10 tests, 1.459 s.
The no-site-packages run completed 10 discovered tests, 8 skipped, in 0.153 s.
Only a whitespace separator was added afterward.

No bridge defect was observed. No production/module change, probe execution,
full 4096-frame/73-key quadrature sweep, benchmark, or acceptance promotion was
performed. Next: root runs the separately owned, freshly bound fixed-case
diagnostic and retains its original comparison statuses and residuals.
