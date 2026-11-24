# Contiguous quadrature FIR traversal — 24 November 2026, session B

The fixed six-branch FIR now traverses each circular history in two contiguous
descending ranges. It retains one accumulator, the same node and tap order,
the same coefficients, and the same intermediate-overflow behavior. There is
no wrap decision inside each multiply/add. Preparation, fixed storage, filter
history, reported latency, model identity and saved-state format are unchanged.

The source change follows [worker 006's index and compiler review](../agents/reports/20261124b-w1-006-qfir-runtime.md).
The measured source was the `2a8caab` checkout with this header patch; the
[complete compatibility result](qfir-contiguous-compatibility-20261124b.json)
records the exact source and executable SHA-256 values, original baseline hashes,
every case/result hash, environment and execution times.

## Observed verification

The existing five FIR and five quadrature-engine CTests passed after rebuilding
the affected targets. They exercise chronological convolution, impulses,
48-frame delay, invalid-cell rejection, overflow/reset, event partitions,
preparation, fault behavior and guarded allocations.

The complete compatibility replay then matched all **2,097 Q6 records /
2,534,400 output frames** exactly, together with all **24 direct/filtered WAV
fixtures / 44,160 frames**. The replay's source/input/binary stability checks
passed and reported no errors. These are observed Linux/GCC results; native
platform and additional compiler contraction checks are separate work.

```sh
cmake --build build/vst3 --parallel 3 --target rhodes_quadrature_fir_tests rhodes_quadrature_engine_tests rhodes_quadrature_probe rhodes_render
ctest --test-dir build/vst3 -R 'quadrature\.(qfir_|qengine_)' --output-on-failure
python3 tools/verify_audio_compatibility.py --probe build/vst3/rhodes_quadrature_probe --renderer build/vst3/rhodes_render --output build/NEW-qfir-compatibility
```

This replay preserves applicability of the retained independent Q6 comparison;
it does not recompute the continuous reference or establish physical fidelity.
No timing improvement is claimed by this record. A separate paired measurement
is required to assess the change's runtime effect. Direct/filtered convergence,
general contact extrema and hardware/DAW qualification remain separate gates.
