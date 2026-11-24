# Filtered transient error decomposition — 24 November 2026, session B

The frozen `r08-chord-all73-u10` diagnostic now reproduces and explains the
previous model-2 failure without changing its source, onset, output scale,
event ordering, reset epochs, 48-frame delay or numerical limits. This is a
synthetic loaded-harp voltage experiment, not a measurement of an instrument or
an isolated spectral aliasing bound.

The implemented tools are `filtered_error_bridge.py` and
`decompose_filtered_audio.py`. The [predeclared plan](filtered-decomposition-plan-20261124b.json)
fixes 192 kHz, 73 keys, intensity 1, 4,096 frames and 736 ordered events.
The [manifest](filtered-decomposition-manifest-20261124b.json),
[original case result](filtered-decomposition-case-20261124b.json) and
[complete decomposition](filtered-decomposition-initial-20261124b.json) retain
the source, binary, input and array identities and all comparison metrics.
The binary was built with the [initial verified source](continuation-b-baseline-20261124.json).

## What the experiment found

Production agrees with the independently sampled/FIR implementation to
`1.0325074129013956e-14 V` peak, passing comparison A's `1e-12 V` limit.
Both Q16/Q32 and Q32/Q64 continuous-reference differences are at most
`4.440892098500626e-16 V`, satisfying the unchanged refinement limits.
Comparison B still **fails**, with `0.4473573821492792 V` peak at frame 816
and `0.014629788655542155 V` RMS. The diagnostic correctly exits **1** while
recording `diagnostic_complete=true`; a completed explanation does not turn
the original failure into a pass.

Define P as production, S as the independent sampled/FIR output, N as the same
sampled numerator with continuous DC normalization, T as event-aware one-sided
composite trapezoids, and C as the Q64 continuous result. The exact decomposition
is `P-C=(P-S)+(S-N)+(N-T)+(T-C)`.

| Component at frame 816 | Signed error (V) |
| --- | ---: |
| Production minus independent sampled implementation, P−S | `+6.661338147750939e-16` |
| Discrete versus continuous normalization, S−N | `-4.140269789787432e-6` |
| Endpoint and ordered event weighting, N−T | `-0.44742148975005236` |
| Cell integration, T−C | `+6.824787056225468e-5` |
| Total, P−C | `-0.4473573821492792` |

The maximum telescoping residual is `3.469446951953614e-18 V`; explicit
endpoint/event attribution closes within `1.2119211884042969e-15 V`.
Independent reconstruction of the internal source matches exactly. The driver
checks these separate identities against a fixed `1e-12 V` bookkeeping limit.

Endpoint weighting explains the largest sample, but **jump correction alone
is insufficient**: over the complete record, the cell-integration term reaches
`0.08344302942892937 V` at frame 815, with `0.0039993910697735426 V` RMS.
The normalization term peaks at only `6.471358042547948e-6 V`.
These observations support retaining the separately identified quadrature path
for the continuous target. They do not authorize silently changing model 2.

## Reproduce and interpret

The optional numerical dependencies are NumPy and SciPy, used offline only.
The plan/help paths require only Python's standard library. The driver creates
an exclusive output directory and executes the reference work in a child with
a default 600-second deadline. Its probe has the original 120-second timeout.
On POSIX systems the parent kills the worker's process group if its deadline
expires; partial evidence stays visible. An existing output path is refused.

```sh
python3 tools/decompose_filtered_audio.py --plan
python3 tools/decompose_filtered_audio.py --probe build/vst3/rhodes_audio_probe --output-dir build/NEW-filtered-decomposition
```

Raw probe streams, all six original arrays, intermediate arrays and per-event
contribution arrays remain in the recorded output directory. Committed JSON
records retain their hashes; rerunning the fixed command reconstructs them.
The run checked source and binary hashes before and after execution. Source
hashes identify inputs; the separately retained build record supplies the
observed compilation context.

RT-001 remains incomplete. This one-case diagnostic does not replace the full
historical matrix, resolve the direct path's aliasing question, validate future
physical models, or qualify DAWs and audio-device deadlines. The existing
model-1/model-2 identities and saved-state semantics are preserved.
