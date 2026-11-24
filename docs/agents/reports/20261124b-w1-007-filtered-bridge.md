# Worker 007 — independent filtered-error bridge

Implemented `tools/filtered_error_bridge.py`; no auditor, production,
CLI, or state changes. Exact preceding-segment propagation
produces one-sided loaded sources. Each pickup is converted before key summation;
reset epochs have zero left history. All producer rows retain physical/effective
jumps, including erased same-time events. Trapezoid, both nonzero support
endpoints, per-event matrices, four telescoping terms, residuals, regional
metrics, and frame-816 values are retained. Numerical imports are lazy.

Read starting documents and worker-002 design. Ran:

```sh
python3 -B tools/project.py validate
python3 -B tools/project.py sync --check
python3 -B tools/project.py next --json
python3 -S -B -c "from tools import filtered_error_bridge; import sys; assert 'numpy' not in sys.modules; print('stdlib import passed')"
```

Passed; state revision 32 had no unclaimed available tasks. Two executed
`python3 -B -` selfcheck sessions covered direct cell sums, constant/adjacent
steps, reset/strike ordering, smooth zero-interior jumps, exact two-key source
agreement, all L=1/2/4, and 12 malformed-input rejections. All passed.

Executed bookkeeping check:

```python
from tools import filtered_error_bridge as b
r = b.reference
c = next(c for c in r.build_cases('full') if c.id == 'r08-chord-all73-u10')
x, left, rows = b.one_sided_source(c)
epochs = r.reset_epochs(c.frames, c.events)
z = r.quadrature(64).normalizer
s = r.sampled_reference(x, 1, epochs)
t = b.trapezoid_reference(x, left, 1, epochs, z)
a, m = b.decompose_arrays(s, s, t, x, left, 1, epochs, z, event_rows=rows)
assert len(rows) == 736 and left[1023] == 0
assert abs(a['endpoint_event_attribution_residual_volts']).max() < 1e-12
```

Maximum attribution residual: `9.838050513133467e-16 V`. This used P=S and C=T;
no production/Q64 comparison, sweep, benchmark, or acceptance claim. Next: driver
and independent tests, then the freshly source-bound fixed-case experiment.
