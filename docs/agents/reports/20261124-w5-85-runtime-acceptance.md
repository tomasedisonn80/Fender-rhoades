# W5-85 — Runtime acceptance and bounded continuation

Evaluator: `/root/w5_85_runtime_acceptance`; 24 November 2026.

**RT-001 remains incomplete.** The independent review agrees with
[W3-58](20261124-w3-58-rt-criteria.md): C01 and C02 pass their recorded
benchmark/reporting scope; C03 does not pass overall. This report changes no
criterion, tolerance or task status.

## Frozen definition

State revision 27 supplies the exact projection below. Sorted-key compact UTF-8
JSON with `ensure_ascii=False` gives SHA-256
`134aae8a07f36b75bc986390376f51ca5fbb8abc0955d57913dda04d1ad4f6b8`.
Dependency PHY-001 is complete.

```json
{
  "task_id": "RT-001",
  "acceptance": [
    "Benchmark representative kernels with hardware, compiler, sample rate, block size, polyphony and stress conditions recorded.",
    "Report maximum observed callback duration and deadline misses, plus optimized-versus-reference error.",
    "Verify allocation/locking policy and timestep/antialiasing convergence; claim only measured performance."
  ],
  "dependencies": ["PHY-001"],
  "required_evidence": ["performance", "numerical"],
  "target": "One documented 73-key Mark I Stage specimen at its specified loaded electrical output; identity and measurements pending."
}
```

## Criterion decisions

| Exact criterion | Evidence and observed coverage | Remaining limitation | Decision |
| --- | --- | --- | --- |
| **RT-001/C01:** Benchmark representative kernels with hardware, compiler, sample rate, block size, polyphony and stress conditions recorded. | Three retained matrices each contain 72 processes, 40,128 callbacks and all 36 same-variant repeat pairs. The frozen plan, machine/build records and raw reports identify Linux x86_64, AMD EPYC 9V74, GCC 14.2, CPU 0, rates, blocks, keys and held/pedal/gain workloads. | Shared scheduling, frequency and competing activity were uncontrolled; future physical-model cost is outside this suite. | Pass within recorded scope |
| **RT-001/C02:** Report maximum observed callback duration and deadline misses, plus optimized-versus-reference error. | Final direct/filtered/Q6 maxima are 19,216.976 / 63,297.240 / 99,882.967 µs; misses are 18 / 21 / 1,716. The independent Q6 study resolves all 2,097 cases; maximum continuous-reference error is 8.318901123516298e-12 V. Batch replay preserves all 2,097 Q6 outputs and 24 legacy WAVs exactly. | Replay did not recompute reference integration. Observed maxima and nominal-period misses are not device underruns or worst-case bounds. | Pass within reporting scope |
| **RT-001/C03:** Verify allocation/locking policy and timestep/antialiasing convergence; claim only measured performance. | Fixed storage, compile-time lock-free mailbox requirements and bounded CAS operations agree with retained calibrated allocation guards. Q6 Q16→Q32→Q64 refinement evidence remains applicable through the recorded replay. | Historical model-2 comparison B retains 2,085 failures/12 passes; direct-path alias acceptance remains unresolved. Guards exclude direct malloc, other threads and arbitrary host getters. No complete convergence/error account covers all retained paths. | Incomplete |

All captures remain visible; pooled misses mix workloads and are not population
miss rates. I independently recomputed these Q6 totals and maxima:

| Capture | Nominal misses | Maximum callback, µs |
| --- | ---: | ---: |
| Original scalar | 2,947 | 91,441.456 |
| First batch | 3,852 | 96,939.447 |
| Final batch | 1,716 | 99,882.967 |

The separate ABBA×4 experiment has eight observations per implementation. Mean
whole-child user CPU is 1.53260575 s before and 1.422067375 s after: 7.21245%
lower in that observation. It includes setup, warmup and serialization, excludes
descheduled time, and does not establish a causal speedup. Actual DAW/device
qualification remains a separate boundary, not an added C01/C02 prerequisite.

The historical filtered study's wrapper, FIR, coefficients, evaluator and frozen
contract still match; ResearchEngine, control/probe/build files have later changes.
Its failures therefore retain their historical source identity. Twenty-four
legacy WAV comparisons do not constitute a new full filtered-reference sweep.
Current allocation-related source hashes match the native manifest; the later
Q6 fault-test expansion is separately covered by W4-70. Neither byte identity
nor a populated evidence record authenticates physical measurements.

## Next bounded experiment — proposed, not executed

First freeze a one-case decomposition for `r08-chord-all73-u10`: 192 kHz, all
73 keys, intensity 1, 4,096 frames and the original ordered script. The CLI has
only full/pilot suites; use its existing Python API in a separate diagnostic
driver. Build the current probe with
`cmake --build build/vst3 --target rhodes_audio_probe --parallel 2`, then select
the exact case using `build_cases("full")` and invoke `execute_case()` with a
new directory from `create_output()` and `ReferenceBank()`.

Retain source/input/binary hashes before and after, exact argv/environment, raw
probe streams, the six float64 arrays and returned case JSON. Compare production
P, independently sampled/FIR output S and continuous Q64 output C through
`P-C = (P-S)+(S-C)`. Decompose S−C with explicitly defined intermediate kernel
normalization, endpoint/event weighting and cell-integration sequences; retain
their sum and residual. Do not label the residual alias-only without a separate
spectral derivation. Preserve zero prehistory, reset epochs, phase L*n,
48-frame delay and all onset samples. Keep A's 1e-12 V peak limit, B's 1e-4 V
peak/1e-5 V RMS limits, and refinement's 1e-9 V peak/1e-10 V RMS limits unchanged.
One diagnostic case informs a subsequent finite plan; it cannot complete C03.

## Executed review and identities

Required `project.py validate`, `sync --check` and `next --json` passed; the final
check observed 14 tasks, 23 evidence records and unvalidated fidelity. Read-only
source/JSON/tar inspection and SHA-256 checks verified the three compressed
archive hashes, recomputed aggregate statistics and checked source applicability.
No archive was extracted, and no build, test, benchmark or numerical study ran.
Only this report was written.

| Examined path | SHA-256 |
| --- | --- |
| `docs/validation/vst3-matrix-20261124.json` | `021648832aa392fc68b1c7235db79c6489da93798a48c68321459985ac57328d` |
| `docs/validation/vst3-matrix-batched-20261124.json` | `264b56c4500a4ff5a80d40cb9c1af03339ddc4472d4d803f5ae9fa34384073f6` |
| `docs/validation/vst3-matrix-batch-final-20261124.json` | `e0c00b865e9a274be69e8d384ba9c31001e9bc27c3958ada00e1c5ffe78b54a9` |
| `docs/validation/q6-cpu-paired-20261124.json` | `e678e3a9e1610e21d33458fd9c945861adb03c3fe490533026d47b752b8511c7` |
| `docs/validation/quadrature-audio-full-20261123.json` | `d1d13dc3468fff4962d3bdc03a61a35a03284bc2ae38835c9783525903c15278` |
| `docs/validation/filtered-audio-full-20261123.json` | `68041ad5edef8a31998d8b441819c348b282b222d8255dc0229a494384679c07` |
| `docs/validation/batched-quadrature-compatibility-20261124.json` | `2ecedc02e1fb11628cfa98017035f0e3e92b7a428e72ed6ec281ce6f1b6b40d4` |
| `docs/validation/aliasing-observations-20261123.json` | `bde596bcb9046a47f523a0d526e61527f9992e3b12861604c4123dacdf0683eb` |
| `docs/agents/reports/20261124-w3-49-allocation-policy.md` | `83ceffd6024cb70e9177417d71dd48a08ec6ab91059b27f0c4114cf9475d901a` |
| `src/research_engine.cpp` | `f0facfcb2c28f989c79407d2e0571a868b07ec7a37511bc05f7a33efcdeb0703` |
| `include/rhodes/quadrature_engine.hpp` | `c625a82b61eb3e721061177654c12ffa6aac0049e026b9633ae53ea2abaa773d` |
| `include/rhodes/instrument_state.hpp` | `c11c9d982887f4228c76c3ee9e10fe5627c2dc0f1053ad531cfd316285a65616` |
| `plugins/vst3/processor.cpp` | `97b449dac4e1eb434af8ad4e279bdf7ba07f4668cb174199980eea0266493cf6` |
| `tools/validate_filtered_audio.py` | `0d6e5336cd571e873b22333e9ed8f98d70c4aa443831309c68ad1084171d3ca1` |
| `docs/specs/FILTERED_TRANSIENT_DIAGNOSTIC_20261123.md` | `9b2103e272bd28a226d43099f178d2cde9c9041b57383d278e756e85d07a57b4` |
