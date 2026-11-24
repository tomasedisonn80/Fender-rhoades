# W3-51 — Independent retained timing audit

**Agent:** `/root/w3_51_timing_statistics` · **Assignment:** 51, wave 3  
**Result:** no material numerical or attribution error found.

## Observed checks

Read the operating contract, current guidance and RT-001 definition at revision
27. `python3 tools/project.py validate`, `sync --check` and `next --json` passed.
The integrator owns the existing task claim and all publication/state changes.

An independent standard-library `python3 - <<'PY'` audit read the four JSON/tar
pairs below without filesystem extraction, archived-code execution, or importing
the new project verifier. All archive byte counts, archive hashes, indexed member
sets and member hashes matched. Members were unique, flat, nonsparse regular
files: 298 per matrix archive and 34 in the CPU archive.

For every one of the **216 matrix processes**, I recomputed observation count,
serial sorted binary64 mean, maximum, nearest-rank p50/p95/p99, nominal period
and strict `duration > period` miss count from archived raw callbacks. All matched
both native and outer summaries exactly. Each capture contains **40,128 callbacks
and 3,846,144 frames**; each variant contributes 24 processes, 13,376 callbacks
and 1,282,048 frames. The selected p03/p06/p09 Q6 values also match the prose.

| Capture | Direct misses | Filtered misses | Q6 misses | Maximum Q6 callback, µs |
| --- | ---: | ---: | ---: | ---: |
| Original | 33 | 62 | 2,947 | 91,441.456 |
| First batch | 50 | 149 | 3,852 | 96,939.447 |
| Final batch | 18 | 21 | 1,716 | 99,882.967 |

The CPU archive's plan/result objects match the outer JSON. All 16 labels follow
`before, after, after, before` repeated four times. Every record reports exit 0,
CPU affinity `[0]`, identical native integrity, and the declared 4,096 measured
64-frame callbacks at 192 kHz, 73 held keys, float64 and 32 warmup callbacks.
Both executable digests match their corresponding matrix inventories.

| Whole-child user CPU | Observations | Mean, seconds | Median, seconds |
| --- | ---: | ---: | ---: |
| Before | 8 | 1.53260575 | 1.4865385 |
| After | 8 | 1.422067375 | 1.391038 |

Exact calculation expressions executed in the inline audit included:

```python
deadline = w['block_frames'] * 1e6 / w['sample_rate_hz']
misses = sum(v > deadline for v in raw)
p50 = sorted(raw)[math.ceil(len(raw) * 50 / 100) - 1]
mean = statistics.mean(values[label])
median = statistics.median(values[label])
lower = 100 * (1 - stats['after']['mean_s'] / stats['before']['mean_s'])
```

The last expression gives **7.212446840943931%**, supporting the rounded 7.2%
observation. An initial inventory substring filter also selected object files;
its assertion failed. The corrected exact executable-basename filter passed.
All nine examined input hashes remained unchanged.

## Interpretation and handoff

The prose correctly preserves the worse first batch and final 99.883 ms outlier.
Mixed-workload counts are not a population miss rate. Whole-child user CPU
includes preparation, warmup and reporting; it is a different boundary from
steady-clock callback duration. Eight observations per implementation on one
uncontrolled shared host do not establish causal or portable speedup, worst-case
time, device underruns, or statistical generality. Order balancing does not
remove frequency/cache/competing-load effects. Hash consistency does not
authenticate acquisition. No new benchmark, build, test suite or physical
measurement ran. Root may retain the existing limited claims; RT-001 remains
incomplete and physical fidelity unvalidated.

## Examined source identities

Paths below are under `docs/validation/`.

| File | SHA-256 |
| --- | --- |
| `batched-quadrature-20261124.md` | `9a22e4fc3a2cb6373b04f67b8998a4edac5dd1a1d95484ce9cab0b65a170619c` |
| `vst3-matrix-20261124.json` | `021648832aa392fc68b1c7235db79c6489da93798a48c68321459985ac57328d` |
| `vst3-matrix-20261124.tar.gz` | `366c6935c98f143c1fa3d6d6014a83058391d73391fb6bf0047629650783fad4` |
| `vst3-matrix-batched-20261124.json` | `264b56c4500a4ff5a80d40cb9c1af03339ddc4472d4d803f5ae9fa34384073f6` |
| `vst3-matrix-batched-20261124.tar.gz` | `e5bcef9892ba465130592b72021daa503784acb433d5cb06ee325fa29ca67c6a` |
| `vst3-matrix-batch-final-20261124.json` | `e0c00b865e9a274be69e8d384ba9c31001e9bc27c3958ada00e1c5ffe78b54a9` |
| `vst3-matrix-batch-final-20261124.tar.gz` | `1c4871b671512a5ae497e41248d07ea4eb7d9b8ee60149b21171d51cd575d91b` |
| `q6-cpu-paired-20261124.json` | `e678e3a9e1610e21d33458fd9c945861adb03c3fe490533026d47b752b8511c7` |
| `q6-cpu-paired-20261124.tar.gz` | `dab4f216022db4bfceee5bcf6fd39b4ae328eba0fe9aec7cecba6bccd2ce46a4` |
