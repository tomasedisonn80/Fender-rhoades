# Worker 002 — sampled/continuous filtered-error diagnostic design

Date: 2026-11-24. Scope: read-only implementation review and in-memory analytic
spot calculations; only this report was written. Root owns state, staging and
commits. No new probe run, complete case comparison, full sweep, benchmark or
acceptance claim is made here.

## Finding

The leading explanation for the historical model-2 failures is the full weight
given to right-continuous samples at instantaneous strike-voltage jumps. The
continuous convolution assigns no mass to an isolated endpoint. The sampled
convolution consequently has a leading half-sample jump contribution, propagated
by the FIR. This is a mathematical difference between the frozen sampled model
and the continuous target, not evidence that its ring buffer is wrong.

The retained [full study](../../validation/filtered-audio-full-20261123.json)
reports A passing all 2,097 cases, B failing 2,085 and passing 12, with both
reference refinements resolved. Its worst B error is
`-0.4473573821492792 V` at frame 816 of `r08-chord-all73-u10`; the RMS error is
`0.014629788655542155 V`. Outside the causal event-window union, this case's
historical maximum is only `1.377271341862496e-6 V`. These remain historical,
source-bound observations, not a new execution against current production.

For the frame-768 strike, exact independent propagation of the preceding
`KeyTrajectory` segment to the event, followed by the ordered velocity impulse,
gave the following in-memory spot calculation:

| Quantity | Value |
| --- | ---: |
| Loaded source immediately before strike | `0.0745591594294645 V` |
| Loaded source immediately after strike | `-0.8774063738389667 V` |
| Voltage jump | `-0.9519655332684311 V` |
| Continuous Q64 kernel DC integral Z | `1.000004092069991` |
| Leading interior endpoint term at frame 768+48 | `0.5*0.94*jump/Z = -0.44742196975414683 V` |
| Historical B error minus that leading term | approximately `+6.4587604868e-5 V` |

This close match identifies a strong mechanism; it does not establish the entire
residual or classify it as alias-only. The remaining terms below must be retained.

The historical 12 passes are exactly attack keys 0–3, intensity 0.1, at rate
indices 0/1/2 (32/44.1/48 kHz, all L=4). At a fresh strike the key's unloaded
voltage jump is `-0.027624*intensity V`. With the loaded network factor
`12000/12019/group_size`, the leading delayed term at intensity 0.1 is
`-8.101689120388716e-5 V` for group size 4 at L=4, but
`-1.0802252160518288e-4 V` for group size 3. The corresponding group-size-4
terms at L=2/L=1 are `-1.6203378240777432e-4`/
`-3.2406756481554865e-4 V`. Thus the leading term also explains the precise
threshold/pass pattern. This remains an analytic explanation, not a substitute
for decomposing all 2,097 records or proving their maximum-error locations.

## Frozen case and retained evidence

Select exactly `next(c for c in build_cases("full") if
c.id == "r08-chord-all73-u10")`. Assert one match, rate `192000.0`, factor 1,
4,096 frames, keys 0–72, intensity 1, 736 ordered event rows, and reset epochs
`((0,1023),(1023,4096))`. The existing case-document SHA-256 is
`579b3844d0fd32ef8fa9706f189b5d6255c0c93b2218e64d235ed4b233cbc4bc`.
Preserve reset-before-strikes at frame 1023 and zero-intensity note-ons at 2303.

Use the existing public functions in `tools/validate_filtered_audio.py`:

```python
case = next(c for c in build_cases("full")
            if c.id == "r08-chord-all73-u10")
before = source_snapshot()
binary_before = file_hash(probe)
create_output(output)  # refuses an existing path; parent must exist
bank = ReferenceBank()
row = execute_case(probe, output, case, bank, object_hash(before))
write_exclusive(output / "case.json", canonical_bytes(row) + b"\n")
```

The driver must additionally freeze its own source hash, fixed plan/case and
event-input hashes, build/source binding, compiler/build configuration,
environment and dependency versions, exact argument vector, and before/after
source and binary hashes. `execute_case` saves stdin, stdout, stderr and the
six float64 arrays: production, source-internal, same-rate, continuous Q16,
Q32 and Q64. It does **not** save its returned case JSON, the overall manifest,
or before/after checks for the caller. Retain those explicitly, plus per-array
little-endian float64 hashes and the new intermediate arrays. A binary hash
alone does not attest that the binary was built from the recorded source.

## Exact telescoping decomposition

Use host-frame time units. Let `h=1/L`, `M=96L`, `K_m=kernel(m*h)`,
`D=h*sum(K_m, m=0..M)`, and `Z=quadrature(64).normalizer`. For each output n,
let `x_e(t)` be the independent loaded source after the most recent reset epoch
start e, and zero before e. Superscripts + and − denote exact right and left
limits. The side at e− is zero even when the preceding epoch was moving.

Define the following arrays at the original output times; do not fit shifts,
gains or onset alignment:

```text
P[n] = existing production_volts[n]
S[n] = existing same_rate_volts[n]
C[n] = existing continuous_q64_volts[n]
N[n] = S[n] * D/Z
T[n] = h/(2Z) * sum over m=0..M-1 of
       [K_m*x_e^-(n-m*h) + K_(m+1)*x_e^+(n-(m+1)*h)]

P-C = (P-S) + (S-N) + (N-T) + (T-C)
```

`N` changes only the discrete kernel's DC normalizer. `T` is the composite
trapezoid rule with each cell's endpoints taken from that cell's interior.
It correctly omits the instantaneous current-frame strike from the continuous
interval ending at n. Interior event nodes use the mean of the two one-sided
values. Keep these named terms and their sum and closure residual:

| Array | Interpretation |
| --- | --- |
| `implementation_error_volts = P-S` | Production versus independent sampled implementation |
| `normalization_error_volts = S-N` | Discrete versus continuous DC normalization |
| `endpoint_event_error_volts = N-T` | Finite support endpoints plus sampled event-jump weighting |
| `cell_integration_error_volts = T-C` | Remaining trapezoid versus resolved continuous-cell integration |
| `decomposition_residual_volts` | `(P-C) - sum(the four preceding error arrays)` |

For explicit attribution, `N-T` has the following algebraic expression (apart
from ordinary floating-point evaluation/summation differences):

```text
h/(2Z) * [
  K_0*(2*x_e^+(n)-x_e^-(n)) + K_M*x_e^+(n-96)
  + sum over m=1..M-1 of K_m*(x_e^+(n-m*h)-x_e^-(n-m*h))
]
```

Retain a separate support-endpoint contribution and each event contribution.
One convenient split uses endpoint terms
`h/(2Z)*(K_0*x_e^-(n)+K_M*x_e^+(n-96))`, the current-time jump
`h/Z*K_0*jump(n)`, and interior jumps `h/(2Z)*K_m*jump(n-m*h)`.
Do not treat the two nonzero Kaiser/sinc support endpoints as zero. Include
reset truncation in this calculation; a new epoch has no old filter history.

For cell-level diagnostics, retain the trapezoid contribution and independent
Q16/Q32/Q64 contribution of each delay cell. At L=1 these are 4096×96 matrices;
their row sums must reconstruct T and the existing continuous references.
Store per-cell errors, the sum and summation residual. Each Q order keeps its
own existing normalizer. This can locate smooth-cell curvature, event-adjacent
derivative changes and cancellation without declaring a spectral alias bound.

## Suggested new offline APIs and checks

These are proposed APIs for a separate diagnostic driver, not existing functions:

- `one_sided_source(case) -> right, left, event_rows`: evaluate the independent
  trajectories on the exact internal grid. At an event, propagate the preceding
  `Segment` using `modal_solution` to obtain the exact left state, then use the
  new segment for the right state. Do not approximate limits with epsilon or
  `nextafter`; at non-events reuse the same evaluated source for both sides.
  Apply each local nonlinear pickup before summing the loaded keys.
- `trapezoid_reference(right, left, factor, epochs, normalizer) -> T`: within
  each reset epoch, convolve `left` with `K[:-1]` and `right` with
  `[0, *K[1:]]`, add, multiply by `h/(2Z)`, truncate and take phase `[::L]`.
- `cell_reference(case, order) -> cells`: use the existing `quadrature(order)`
  nodes/weights/kernel, independent `KeyTrajectory.loaded_volts`, and the same
  epoch masks. Its row sum must agree with `continuous_reference`.
- `decompose_case(...) -> arrays, metrics, event_attribution`: retain all
  intermediates, error metrics inside/outside the unchanged event windows,
  values at frame 816, maxima and their frames, signed means, RMS, and closure.

Required targeted checks for the new driver: a constant source with zero
prehistory (support-endpoint terms), a single step and two adjacent steps
(half-weight interior jump and current endpoint), a reset with simultaneous
strike (old history erased), a smooth analytic source (zero interior jump
term), and row-sum/closure checks. Bookkeeping closure can use the existing
`1e-12 V` absolute scale as a separate selfcheck; it cannot turn a failing B
comparison into a pass. Preserve A's `1e-12 V`, B's `1e-4 V` peak and
`1e-5 V` RMS, and both refinements' `1e-9 V` peak and `1e-10 V` RMS limits.

The existing validator CLI offers only full/pilot suites. Do not document an
imaginary existing `--case` option. Existing commands for the integrator/later
implementation worker, **not run by this worker**, are:

```sh
cmake --build build/vst3 --target rhodes_audio_probe --parallel 2
PYTHONDONTWRITEBYTECODE=1 RHODES_AUDIO_PROBE="$PWD/build/vst3/rhodes_audio_probe" python3 -m unittest discover -s tests/python -p test_filtered_audio_reference.py -v
python3 tools/generate_audio_filter.py --check
```

Execute the separately implemented one-case driver after its targeted tests;
freeze its actual CLI before publishing an invocation. A 36-case pilot excludes
the requested all-73 chord, so it is not a replacement for this experiment.

## Honest fix choices with compatibility preserved

1. Keep direct model 1 and filtered model 2 frozen and explicitly bounded by
   their existing sampled contracts. The model-3 quadrature path is already a
   separate compatible option for the continuous target. Its success does not
   erase legacy model-2 B failures or close direct-path alias acceptance.
2. If another correction is needed, use a separately identified/versioned path
   with preserved model/FUID/state rejection and legacy fixtures. Event-aware
   cell quadrature or analytic jump correction followed by a measured smooth
   residual is mathematically motivated. A jump correction alone is not yet a
   validated complete remedy: the frame-816 spot difference is about 64.6 µV,
   and other cells, cases and RMS limits remain to be checked.
3. Higher internal sampling reduces the leading jump bias only as approximately
   `1/L`; changing L, taps or sampling phase changes model-2 outputs and requires
   a new contract and cost study. Smoothing the strike or replacing it with
   finite-mass contact changes the physical source and also requires a new
   model, independent numerical validation and runtime accounting.

Changing only DC normalization cannot repair a roughly 0.447 V jump error.
The measured spot normalizer corrections `1-D/Z` for L=1/2/4 were
`4.8774311537380655e-6`, `1.7426568409417698e-6` and
`7.122278639082325e-7`; for this case's roughly 1.33 V output the first is only
about 6.5 µV. Fitted alignment, widened gates, removal of onset samples, or
silently changing model-2 saved-state semantics are not compatible fixes.

## Executed checks and provenance

Read `AGENTS.md`, README, project state, roadmap, canonical state, the current
[research-gates review](../../validation/research-gates-20261124.md), filtered
contracts, validator, relevant source/filter APIs and existing tests. Ran
`python3 -B tools/project.py validate`, `sync --check`, and `next --json`;
the final observation passed validation (14 tasks, 26 evidence records), showed
current generated state, and revision 32 with no unclaimed available tasks.
This bounded assignment made no independent claim/state change.

Executed read-only Python snippets importing the validator, extracting the
historical pass set, computing D/Z for L=1/2/4, and propagating exact one-sided
states at the six positive-strike event offsets. Python/NumPy/SciPy versions
were `3.12.14`/`2.3.5`/`1.17.0`. No source bytecode, output directory or probe
artifact was intentionally written. Numerical calculations and historical
observations are distinguished above; no new waveform experiment is claimed.

Source HEAD observed: `8813b2680a49c07c277f0d0e932843fdb356c3ee`.

| File | SHA-256 observed |
| --- | --- |
| `tools/validate_filtered_audio.py` | `0d6e5336cd571e873b22333e9ed8f98d70c4aa443831309c68ad1084171d3ca1` |
| `src/research_engine.cpp` | `f0facfcb2c28f989c79407d2e0571a868b07ec7a37511bc05f7a33efcdeb0703` |
| `include/rhodes/audio_engine.hpp` | `10efac8150c07e69ef41eed415a6e7403b4339179af902f91cd17ef498da7b97` |
| Historical full-study JSON | `68041ad5edef8a31998d8b441819c348b282b222d8255dc0229a494384679c07` |

Next action: implement and test the bounded offline driver, freshly bind/build
the probe, retain the six existing arrays plus the complete telescoping terms,
then assess whether the mechanism warrants a finite follow-up matrix. One case
does not complete RT-001/C03.
