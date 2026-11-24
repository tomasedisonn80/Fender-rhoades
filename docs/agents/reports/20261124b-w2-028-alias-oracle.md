# W2-028 — Independent stationary alias oracle

2026-11-24; actual worker 028, wave 2. Bounded RT-001 implementation assignment.
The module and this report are frozen after the focused checks below. No native
build, native primitive experiment, timing capture, Git operation, canonical-state
mutation, engine identity change, historical-budget change or acceptance promotion
was performed by this worker.

## Scope and source binding

Implemented only [tools/direct_alias_oracle.py](../../../tools/direct_alias_oracle.py)
and this report. The module SHA-256 at freeze is:

```text
2bfed9f0c3e75fce9fed9a866756f9caa4d45728a7150912790b4caf2a71ff6c
```

Read the complete [W2-025 proposal](20261124b-w2-025-alias-diagnostic-design.md),
`AGENTS.md`, `README.md`, `docs/PROJECT_STATE.md`, `docs/ROADMAP.md`,
`project/state.json`, and the relevant public primitive declarations and
implementation. The module does not import the production propagator, existing
symbolic alias audit, NumPy or another third-party dependency.

The frozen cases use `(a1,a2,a3)=(0.4,800,400000)` with coefficient units
Wb-turn/m, Wb-turn/m² and Wb-turn/m³, respectively. Modal mass is 0.001 kg,
decay is zero, and the boundary is one pickup's voltage before harp gain.
These are current synthetic development constants, without specimen meaning.

| Case ID | Fs (Hz) | N | Mode frequencies (Hz) | Exact bins | Amplitudes (m) | Phases (rad) |
|---|---:|---:|---|---|---|---|
| `one_mode_48000` | 48000 | 1024 | 13500 | 288 | 0.0001 | π/7 |
| `one_mode_96000` | 96000 | 2048 | 13500 | 288 | 0.0001 | π/7 |
| `two_modes_48000` | 48000 | 1024 | 7500, 13500 | 160, 288 | 0.0001, 0.00006 | π/7, −π/5 |
| `two_modes_96000` | 96000 | 2048 | 7500, 13500 | 160, 288 | 0.0001, 0.00006 | π/7, −π/5 |

All cases have duration 1024/48000 s and exact bin spacing 46.875 Hz. The IDs
were coordinated directly with the separate worker 029 producer implementation.

## Mathematical implementation

The explicit one/two-mode cosine expansion from W2-025 is encoded as thirteen
flux cosine terms, including the constant. A missing second mode has amplitude
zero. This expansion is independent of the existing audit's symbolic algebra.
Each term `q*cos(2*pi*F*t+alpha)` yields the voltage coefficients

\[
c_F=-i\pi Fq e^{i\alpha},\qquad c_{-F}=c_F^*.
\]

The signed derivative multiplier is the **original** frequency `F=k*Fs/N`.
Terms with exactly equal continuous integer bin keys are added before sampling;
the integer fold is `(k+N//2)%N-N//2`. Folded complex coefficients are summed
before computing amplitudes or powers. Only exact zero coefficients are removed;
no frequency or magnitude tolerance coalesces distinct lines.

The ideal target keeps only original bins satisfying `2*abs(k)<N`. Thus original
Nyquist endpoints are excluded from this target. Both conjugates fold to one
real coefficient at DC or Nyquist, and no further doubling occurs. The original
DC flux term differentiates to zero, while a nonzero original multiple of Fs can
produce sampled DC according to phase. Every synthesized real spectrum must be
conjugate symmetric, with real DC/Nyquist values.

`S` is the full analytically differentiated harmonic polynomial sampled at n/Fs;
`B` is its retained original-frequency spectrum sampled at n/Fs. The independent
`direct_v` path evaluates modal sine/cosine q/v, sums both modes, and computes
`-(a1+2*a2*x+3*a3*x*x)*v` directly. It checks `S` without importing a time stepper.
The returned `alias_v` is samplewise `S-B`; `analytic_alias_v` separately
synthesizes the removed original-frequency lines. With no removed lines, the
full and retained spectra are identical, so both 96 kHz alias arrays are exactly
zero without a threshold or a rounded-to-zero operation.

For supplied primitive samples `P`, `error_arrays` retains numerical `P-S`, alias
`S-B`, total `P-B`, and the floating-point closure residual
`(P-B)-((P-S)+(S-B))`. No gain, phase, timing or magnitude-spectrum fit is used.
Parseval uses normalized signed complex coefficients:

\[
\operatorname{RMS}(S-B)=\sqrt{\sum_k |C_k(S-B)|^2}.
\]

An independent direct DFT evaluates only the known line bins plus DC, Nyquist
and ±1 guard bins. It uses `1/N` normalization and the negative exponential sign;
it does not require a full FFT or a third-party package. This is a coefficient
identity check at the stated bins, not a claim that a full FFT was run.

### Clarification of the proposed phase-cancellation check

No error was found in W2-025's expansion or sign. Its phrase “opposite phases”
has been made precise here, with the integrator's agreement, while leaving that
original report intact. For equal **voltage** amplitudes at `f` and `Fs-f`,

\[
\sin(2\pi(F_s-f)n/F_s+\beta)
=-\sin(2\pi f n/F_s-\beta).
\]

Their sum with `sin(2*pi*f*n/Fs+alpha)` cancels when
`beta=-alpha (mod 2*pi)`. The zero-phase pair therefore cancels. A generic π
phase shift is different; at alpha=0 it reinforces instead. Equal flux cosine
amplitudes would not generally mean equal voltage amplitudes because their
derivative multipliers use different original frequencies. The implemented
line-level cancellation checks use equal voltage coefficients explicitly.

## Importable API handoff

| API | Contract |
|---|---|
| `Mode(frequency_hz, amplitude_m, phase_rad)` | Frozen mode; integer Hz, finite nonnegative amplitude and finite phase. Signed/zero frequencies support algebra checks. |
| `Fixture(case_id, sample_rate_hz, sample_count, modes, coefficients=COEFFICIENTS, mass_kg=.001, decay_per_s=0)` | Frozen fixture; modes and coefficients are immutable tuples. Every mode must have an exact coherent bin. |
| `fixtures()` | Tuple of exactly the four frozen cases in the table's order. |
| `fixture_record(fixture)`, `plan()` | Fresh JSON-compatible descriptions, with units, sample definitions, caps and separately named new budgets. |
| `flux_cosine_terms(fixture)` | Tuple of frozen `CosineTerm(frequency_bin, flux_coefficient_wb_turn, phase_rad)` values before coalescence. |
| `voltage_lines_from_cosines(terms, Fs, N)` | Signed differentiation and exact continuous-bin coalescence. |
| `continuous_lines(fixture)` | `dict[int, complex]` of continuous signed voltage coefficients. |
| `coalesce_lines(pairs)`, `fold_bin(k,N)`, `folded_lines(lines,N)` | Bounded exact integer-key algebra and complex folding. |
| `bandlimited_lines(lines,N)`, `alias_lines(lines,N)` | Original strict-band retained lines and folded removed lines, respectively. |
| `sample_lines(lines,N)` | Real sample tuple; one contribution per signed coefficient, with integer cycle reduction. |
| `analytic_motion(fixture)` | `time_s`, sample-major `q_m` and `v_m_per_s`, summed `x_m` and `v_total_m_per_s`. |
| `analytic_waveform(fixture)` | Independent direct chain-rule voltage sample tuple. |
| `oracle_arrays(fixture)` | Motion arrays plus `direct_v`, `s_v`, `b_v`, `alias_v`, `analytic_alias_v`. |
| `error_arrays(P,S,B)` | `numerical_v`, `alias_v`, `total_v`, `closure_v` tuples. |
| `signal_metrics(samples)` | `sample_count`, `sample_peak_v`, `rms_v`, `mean_v`. |
| `line_rms(lines,N)`, `dft_at_bins(samples,bins)` | Independent Parseval RMS and normalized direct DFT at at most 64 requested bins. |
| `metrics(fixture, primitive_v=None)` | JSON-compatible metrics, named checks, failures and `passed`; omitting P explicitly reports `primitive_compared=false`. |
| `self_check()` | The bounded analytic checks described below; no subprocess or file output. |

The module CLI accepts only `--plan` or `--self-check` and prints JSON to stdout.
It has no file-writing or subprocess API, so it cannot overwrite an arbitrary
output path. Exclusive output creation, producer validation, raw float64/CSV/JSON
retention, source/compiler/binary hashes, timeouts and process/artifact caps belong
to the separate driver. The plan declares four cases, 2048 samples per case,
one worker, 30 seconds, 256 MiB process memory and 16 MiB artifacts; this module
enforces array/algebra size caps, not operating-system process limits.

Generic mathematical inputs are bounded to one/two modes, even N from 2 through
2048, Fs from 1 through 192000 integer Hz, at most 128 line contributions,
64 requested DFT bins, and modal frequencies no farther than three sample-rate
periods. Types, booleans, exact coherence, mutable fixture containers, nonfinite
inputs/intermediates, and incompatible sequence lengths are rejected. These
bounds describe an offline algebra API, not expanded C++ primitive support.

The new bookkeeping tolerance is 1e-12 V; proposed primitive numerical limits
remain 1e-10 V sample peak and 1e-11 V RMS. Alias magnitude has **no acceptance
threshold**. None of these names or values changes any existing historical gate.

## Actual focused verification

Initial read-only state commands all exited successfully at revision 32:

```sh
python3 tools/project.py validate
python3 tools/project.py sync --check
python3 tools/project.py next --json
```

Validation reported 14 tasks, 26 evidence records and fidelity `unvalidated`;
generated state was current, and no task was available for an independent claim.
This worker remained within the integrator's RT-001 assignment.

The final module command was:

```sh
python3 -B tools/direct_alias_oracle.py --self-check
```

It exited 0. All 14 named top-level checks and every fixture's bookkeeping
checks passed: single-mode closed identity; independent direct-versus-expanded
two-mode voltage; zero coefficients; signed negative-frequency equivalence;
conjugate symmetry; generic and zero-phase alias cancellation; DC derivative;
DC/Nyquist folding at phases 0 and π/2; original-frequency derivative magnitude;
Faraday sign; sparse normalized DFT agreement; Parseval; and exact zero 96 kHz
analytic alias. Single-mode identity peak residual was 8.881784197001252e-16 V.
The four endpoint coefficient checks had exactly zero residual.

| Case | Alias sample peak (V) | Alias RMS (V) | Direct polynomial − S peak (V) | Direct DFT coefficient maximum error (V) |
|---|---:|---:|---:|---:|
| `one_mode_48000` | 0.6950707156100173 | 0.4801686202168883 | 2.1094237467877974e-15 | 5.978733960281817e-16 |
| `one_mode_96000` | 0 | 0 | 2.220446049250313e-15 | 5.978733960281817e-16 |
| `two_modes_48000` | 0.27788504292219685 | 0.17506422673477412 | 1.304512053934559e-15 | 3.3306690738754696e-16 |
| `two_modes_96000` | 0 | 0 | 1.5543122344752192e-15 | 3.510833468576701e-16 |

Maximum alias synthesis residual was 6.106226635438361e-16 V. Maximum absolute
gap between sampled alias RMS and Parseval line RMS was 2.7755575615628914e-17 V.
Both 96 kHz cases had empty removed-line dictionaries and all-zero alias arrays.
These are analytic diagnostic outputs; no primitive numerical result is claimed.

Additional `python3 -B - <<'PY'` import checks actually ran and passed:

- Thirty-five rejection cases covering boolean/nonnumeric/nonfinite inputs,
  negative amplitudes, out-of-range rates/frequencies/sizes, odd N, missing or
  too many modes, mutable tuple replacements, incoherence, nonpositive mass,
  nonzero decay, malformed/oversized lines, nonreal DC/Nyquist spectra,
  nonfinite/oversized/mismatched samples and oversized DFT requests.
- Two modes at the same 13500 Hz, with amplitudes 1e-4/6e-5 m and phases
  π/7/−π/5: continuous-bin coalescence versus direct polynomial peak residual
  2.6645352591003757e-15 V.
- Synthetic `P=S+1e-8 V`: exactly the two proposed primitive numerical checks
  failed, while decomposition closure and the independent alias quantity passed.
  This deliberately failing synthetic input is not a native primitive test.
- Mutating a returned plan's mode frequency did not mutate subsequent plans or
  frozen fixtures. JSON round-trip with `allow_nan=False`, exact integer-bin
  coherence, frozen rate/count pairs and equal durations all passed.
- SHA-256 was computed from the complete module bytes and recorded above.

`python3 -B tools/check_docs.py` first identified an incorrectly rooted link in
this new report. The link was corrected before freeze and the final run passed.

Durable independent regression tests are assigned to another worker. This report
records the checks actually performed here; it does not claim those future tests,
native process limits or retained driver artifacts have already been validated.

## Next action and limitations

Driver owner: import the frozen API, validate the worker 029 producer schema and
case settings, retain its modal q/v and P alongside all oracle arrays, and call
`metrics(fixture, P)` without alignment or gain fitting. Keep primitive execution
failure, timeout or artifact/hash failure visible and incomplete. Independent
test owner: reproduce line algebra and adversarial input cases without importing
production DSP into the oracle. Integrator: bind durable tests and the bounded
driver result to this source hash before using them as further RT-001 evidence.

This module covers prescribed stationary undamped motion only. Damping,
onset/retrigger/release, finite-window leakage, finite-kernel response,
whole-engine direct convergence, C03, physical fidelity, audibility and real-time
deadlines remain outside its claim. The original W2-025 report is preserved.
