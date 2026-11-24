# Contact and runtime continuation review — 24 November 2026

Evaluator: `development-root`, with independent criterion reviews by
[W5-84](../agents/reports/20261124-w5-84-contact-acceptance.md) and
[W5-85](../agents/reports/20261124-w5-85-runtime-acceptance.md), and the
[specimen/data audit](../agents/reports/20261124-w5-87-specimen-data-gate.md).

**PHY-002 and RT-001 remain incomplete.** The current campaign adds a bounded
four-fixture contact peak diagnostic, preserves Q6 output while batching its
observations, repairs platform and diagnostic failures, and retains complete
timing observations. Those results do not close every scientific criterion.
Release the two integrator claims with the concrete continuation below after
independent review. Fidelity remains `unvalidated`; SPEC-001 and DATA-001 remain
blocked by missing instrument identity and calibrated measurements.

## Frozen definitions and applicable source

The [frozen definitions](development-task-definitions-20261124.json) reproduce
the exact task IDs, acceptance text, dependencies, required kinds and target.
Both tasks depend on completed PHY-001. The unchanged target is **One documented
73-key Mark I Stage specimen at its specified loaded electrical output; identity
and measurements pending.**

| Task | Definition SHA-256 | Required evidence |
| --- | --- | --- |
| PHY-002 | `e51dfa484a4a2f600fa3b6eaf357a8a2a85bfade777745ceb55fd0af8ba4ee03` | numerical |
| RT-001 | `134aae8a07f36b75bc986390376f51ca5fbb8abc0955d57913dda04d1ad4f6b8` | performance, numerical |

The [source manifest](development-source-manifest-20261124.json) binds 156
reviewed files to `766dd11c73cf9d1dcf6c1960b4333767356f9937`. All 74 native
source/test inputs match the tested `2401706` checkpoint. The only difference
from the earlier retained native manifest is the expanded quadrature fault
fixture. This is a source-applicability check, not another numerical experiment.
Every historical result below retains its own execution, input/model, evaluator
and binary identities. Matching bytes do not authenticate a physical acquisition.

## PHY-002: finite contact

| Criterion and exact text | Evidence and observed coverage | Remaining limitation | Decision |
| --- | --- | --- | --- |
| **PHY-002/C01:** Use finite hammer mass, reciprocal contact forces, separation and recontact with persistent resonator state. | W5-84 maps the current finite-inertia hammer and opposite projected forces to the retained supported-impact/recontact, relaunch, event-order and coordinate-rescaled comparisons in the [branch-split audit](contact-split-depth1-20261123.json). Relaunch preserves resonator coordinates and velocities. | Selected synthetic two-mode, p=1 offline reference; no calibrated action or contact processing in the plug-in. | Pass within the declared reference scope |
| **PHY-002/C02:** Model damper/pedal contact and resting equilibrium; identify or label all assumed contact parameters. | The same source-bound matrix covers prescribed damper-face motion, signed actuator work, preload perturbations and equilibrium. Header and frozen contact contract identify assumed parameters as synthetic. The new peak diagnostic encloses rounded-preload values. | Prescribed face motion is not a full key/pedal assembly. Positive preload timing remains `unresolved_near_equilibrium`; a rounded configuration is not an exact positive plateau. | Pass within the declared reference scope |
| **PHY-002/C03:** Pass energy, timestep convergence, retrigger, release and pedal stress checks with explicit numerical tolerances. | The current solver/header and main independent auditor match the retained 338-candidate branch-split result: zero missing slots, zero independent sampled numerical failures and zero declared depth-1 refinement failures. The [four-fixture replay](contact-peaks-final-replay-20261124.json) separately passes its requested value comparisons and supplemental damped timing. | Ten original-grid refinements still fail in the branch-split result, and general continuous extrema remain incomplete for all 338 audit runs. Four analytic fixtures do not establish the other fixture families or arbitrary continuous maxima. | Incomplete |

The original 26 refinement failures and the earlier depth-1 experiment's five
`event_after`-fixture refinement failures remain preserved as historical results. They must not be
presented as the latest solver's counts. Conversely, zero failures in the
declared refined subset cannot erase the ten original-grid failures or missing
extrema coverage. The frozen 0.8 refinement rule has not changed.

The four-fixture archive used the recorded earlier oracle implementation. The
later [UTF-8 transport result](contact-worker-utf8-20261124.json) separately
binds the current oracle and retained damped-input replay. Hashing an arbitrary
producer file does not execute it or prove who generated a supplied input.
All 86 contact-related Python tests passed in their recorded targeted run;
the [final full Python suite](python-final-20261124.json) is separate current
verification with its exact skip disclosed.

### Next bounded contact experiment

The next integrator should claim PHY-002 and first freeze a continuation for
`supported_impact` and `supported_impact_scaled`, using the existing
[extrema design](../specs/CONTACT_EXTREMA_AUDIT_DESIGN_20261123.md).
Its R0/R1/R2 maximum steps are 5/2.5/1.25 microseconds, with relative tolerances
1e-10/1e-12/1e-13 and the design's exact absolute-error vectors. Implement a
separate independent reference module that retains branch endpoints, stationary
compression/force roots, physical-RHS and dense-polynomial candidates, and any
unresolved coverage. Keep the adopted four-fixture contract unchanged.

After recording the current producer's build and binary identities, this
existing command generates the 26 declared diagnostic candidates:

```sh
python3 tools/run_contact_sweep.py --executable build/vst3/rhodes_contact_reference --fixture supported_impact --fixture supported_impact_scaled --extra-step-levels 1 --locate-peak-candidates --output-dir build/contact-extrema-supported-next
```

Retain all results and require both independent reference refinements within
0.1 of the existing compression budget `1e-8 m + 1e-4*abs(M)` and force budget
`1e-4 N + 2e-4*abs(M)`. Preserve the coordinate-rescaling comparisons and 0.8
convergence rule. This is a proposed experiment, not an implemented extrema CLI
or a new passing result. Coupled motion and recontact are its bounded purpose;
action-side and sinusoidal-damper families remain later work. Contact stays
outside the audio callback until a separately bounded approximation is validated.

## RT-001: cost and numerical approximation

| Criterion and exact text | Evidence and observed coverage | Remaining limitation | Decision |
| --- | --- | --- | --- |
| **RT-001/C01:** Benchmark representative kernels with hardware, compiler, sample rate, block size, polyphony and stress conditions recorded. | Three retained captures each preserve 72 processes, 40,128 callbacks, 36 repeat pairs, the frozen workload plan, AMD EPYC 9V74/Linux/GCC 14.2 metadata, CPU 0 affinity and all rate/block/key/workload fields. See the [final matrix](vst3-matrix-batch-final-20261124.json) and W5-85's independent arithmetic review. | Shared scheduling, frequency and competing activity were uncontrolled. Future physical-model costs are outside the recorded synthetic workloads. | Pass within the recorded benchmark scope |
| **RT-001/C02:** Report maximum observed callback duration and deadline misses, plus optimized-versus-reference error. | Final direct/filtered/Q6 maxima are 19,216.976 / 63,297.240 / 99,882.967 microseconds; nominal-period misses are 18 / 21 / 1,716. The [independent Q6 study](quadrature-audio-full-20261123.json) resolves 2,097 cases, with maximum continuous-reference error 8.318901123516298e-12 V. The [batch replay](batched-quadrature-compatibility-20261124.json) preserves 2,097 Q6 outputs and 24 legacy WAVs exactly. | Replay reuses the earlier reference conclusions; it does not recompute SciPy integration. Observed maxima are not worst-case bounds, and nominal-period misses are not measured audio-device underruns. | Pass within the reporting scope |
| **RT-001/C03:** Verify allocation/locking policy and timestep/antialiasing convergence; claim only measured performance. | Fixed storage, lock-free mailbox requirements and bounded CAS operations agree with the retained calibrated allocation guards. Q6's declared Q16/Q32/Q64 refinement evidence remains applicable through exact output replay. The new 48 invalid-cell scenarios supplement existing fault/history checks and passed native core/sanitizer selectors. | Historical model-2 comparison B retains 2,085 failures and 12 passes; direct-path alias acceptance remains unresolved. Allocation guards exclude direct malloc, other threads and arbitrary host getters. No complete convergence/error account covers all retained paths. | Incomplete |

All three timing captures remain available, including the worse first-batch
observation. Their Q6 miss totals are 2,947 / 3,852 / 1,716 and maxima are
91,441.456 / 96,939.447 / 99,882.967 microseconds. Pooling different workloads
does not produce a population miss rate. The separate ABBA-by-four CPU study
observed eight runs per implementation: mean whole-child user CPU was
1.53260575 seconds before and 1.422067375 after, or 7.21245% lower in that
observation. It includes setup/warmup/report work and is not a causal or portable
speedup claim.

The filtered wrapper, FIR, coefficients, evaluator and frozen contract still
match their historical study; other production/probe/build files have evolved.
Its 2,085 failures remain historical source-bound observations. The 24 legacy
WAV comparisons are useful compatibility evidence, not a current full filtered
reference sweep. Neither a Q6 pass nor a narrow compatibility replay completes
all retained models' convergence gates. Native DAW/device qualification remains
a separate boundary; it is not added as an unstated C01/C02 requirement.

### Next bounded runtime experiment

The next integrator should claim RT-001 and freeze one diagnostic decomposition
for model-2 case `r08-chord-all73-u10`: 192 kHz, all 73 keys, intensity 1,
4,096 frames and the original ordered event script. Build the current probe:

```sh
cmake --build build/vst3 --target rhodes_audio_probe --parallel 2
```

The existing validator CLI offers full/pilot suites, not a single-case option.
Use `build_cases("full")`, `ReferenceBank`, `create_output` and `execute_case`
from `tools/validate_filtered_audio.py` in a separately bounded diagnostic
driver. Retain before/after source/input/binary hashes, exact argument vectors,
environment, raw probe streams, six float64 arrays and returned case JSON.

Start with `P-C = (P-S) + (S-C)`, where P is production output, S the independent
sampled/FIR path and C the continuous Q64 path. Define intermediate kernel
normalization, endpoint/event weighting and cell-integration sequences, retaining
their sum and residual. Do not call the residual alias-only without a separate
spectral derivation. Preserve zero prehistory, reset epochs, phase `L*n`, the
48-frame delay and every onset sample. Keep A's `1e-12 V` peak limit, B's
`1e-4 V` peak/`1e-5 V` RMS limits, and refinement's `1e-9 V` peak/`1e-10 V` RMS
limits unchanged. One case can inform a subsequent finite plan; it cannot
complete C03.

## Physical evidence and integration limits

The data audit found one unidentified profile and one synthetic metadata-only
session. Eighteen component values remain null/unknown, wiring is unverified,
and no calibrated payloads or raw captures exist in the inspected data/profile
inventory. The useful next acquisition is a dated specimen/configuration
passport and calibrated one-key pilot, with synchronized motion and loaded
voltage, uncertainties, raw payload hashes, rights and condition-level holdouts.
The existing ingestion contract also requires a coordinated schema/validator
upgrade before measured datasets can be accepted. Public factory documents
remain attributed background, not substitute measurements.

This review uses retained reports, source/hash inspection and explicit criterion
mapping. It runs no new contact sweep, full audio reference study, benchmark,
DAW or physical acquisition. The full local Python verification is separately
recorded as 584 discovered tests, 583 unskipped passes and one optional production
probe skip. No acceptance wording, numerical budget, historical report,
specimen identity or fidelity status is changed to obtain a passing milestone.
