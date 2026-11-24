# W2-025 — Direct primitive alias diagnostic design

2026-11-24; bounded RT-001 research assignment. **Recommendation: implement a four-case stationary amplitude/phase diagnostic next.** This report specifies proposed work; no experiment, implementation, model change, or C03 promotion occurred.

## Existing coverage and remaining gap

Reviewed `src/research_engine.cpp`, its header, both `*AUDIO*` specifications, `tools/audit_aliasing.py`, its Python tests, `research_engine_test.cpp`, both audio validators/tests, historical `transient-campaign/w1_a01.md`, and the [filtered decomposition](../../validation/filtered-decomposition-20261124b.md).

Existing tests check single-mode algebra, mixed monomials, zero-phase cosine folding, damped derivatives, and primitive propagation. The audit explicitly excludes amplitude/error measurement. Q6 compares a finite-kernel convolution; the filtered decomposition separates implementation, normalization, endpoints/events, and integration. None supplies phase-coalesced stationary alias amplitudes versus an ideal bandlimited target. Extend that gap without rerunning either transient matrix.

## Exact oracle, derived here

Let $x=A\cos\theta+B\cos\psi$, $\theta=2\pi f_1t+\phi_1$, $\psi=2\pi f_2t+\phi_2$, and $v=\dot x$. Expand $\Phi=a_1x+a_2x^2+a_3x^3$ into these cosine terms:

| Argument | Flux cosine coefficient $q$ |
|---|---|
| constant | $a_2(A^2+B^2)/2$ |
| $\theta$ | $a_1A+3a_3A^3/4+3a_3AB^2/2$ |
| $\psi$ | $a_1B+3a_3B^3/4+3a_3A^2B/2$ |
| $2\theta,2\psi$ | $a_2A^2/2,a_2B^2/2$ |
| $\theta+\psi,\theta-\psi$ | $a_2AB$, each |
| $3\theta,3\psi$ | $a_3A^3/4,a_3B^3/4$ |
| $2\theta+\psi,2\theta-\psi$ | $3a_3A^2B/4$, each |
| $\theta+2\psi,\theta-2\psi$ | $3a_3AB^2/4$, each |

For every argument $2\pi Ft+\alpha$, voltage contributes $+2\pi Fq\sin(2\pi Ft+\alpha)$; retain signed $F$. The constant differentiates to zero; $B=0$ gives the single-mode expansion. Here $A,B$ are metres, $a_p$ is Wb-turn/m$^p$, $q$ is Wb-turn, and voltage is volts; turns are already included.

Create conjugate complex voltage coefficients

\[
c_F=-i\pi Fq e^{i\alpha},\qquad c_{-F}=c_F^*.
\]

Add coincident continuous frequencies first. Fold signed frequencies using

\[
r(F)=F-F_s\lfloor F/F_s+1/2\rfloor\in[-F_s/2,F_s/2).
\]

Sum complex coefficients at each folded location **before** magnitudes/powers; never replace the derivative multiplier $F$ by $r(F)$. Use exact integer bin keys, not frequency tolerances. Both conjugates coalesce at DC/Nyquist, without doubling again. Sampling identities and phase reversal follow [Delgutte §1.1.2, equations 1.8–1.10](https://ocw.mit.edu/courses/hst-582j-biomedical-signal-and-image-processing-spring-2007/c8dc8096a9d75f8f30b4b97354b48437_ch1_adc.pdf).

## Proposed minimal plan

Use current synthetic coefficients $(a_1,a_2,a_3)=(0.4,800,400000)$, zero decay, and mass $0.001$ kg. No specimen interpretation is intended. Motion is periodic for all time; initialization selects phase, not a strike.

| Motion | Frequencies (Hz) | Amplitudes (m) | Phases (rad) |
|---|---|---|---|
| one mode | 13500 | $10^{-4}$ | $\pi/7$ |
| two modes | 7500, 13500 | $10^{-4},6\times10^{-5}$ | $\pi/7,-\pi/5$ |

Run each at $F_s=48000,N=1024$ and $96000,N=2048$: identical duration, coherent integer bins, no Nyquist terms. At 48 kHz, 40500 Hz folds to −7500 Hz and 34500 Hz to −13500 Hz, colliding with existing components. At 96 kHz all generated frequencies fit below Nyquist. Coherent records avoid DFT-bin leakage; see [Rowell §5, equations 22–26 and cases (a)/(b)](https://ocw.mit.edu/courses/2-161-signal-processing-continuous-and-discrete-fall-2008/80ec6fb9bd027f30adcdc16577e8ab16_samplingdft.pdf).

A separate C++ harness should configure public `ModalOscillator`/`PolynomialPickup` primitives, initialize $q_j=A_j\cos\phi_j,v_j=-2\pi f_jA_j\sin\phi_j$, sum motion before pickup, and observe before advancing. Boundary: one pickup's volts, without harp gain. Existing `ResearchEngine` parameters/identities remain untouched.

Let S be exact polynomial samples, P primitive output, and B samples retaining only original $|F|<F_s/2$ terms. Record $P-B=(P-S)+(S-B)$: numerical error plus stationary alias contribution. For normalized DFT $C_k=N^{-1}\sum_n y_ne^{-2\pi ikn/N}$, alias RMS is $\sqrt{\sum_k|C_k(S-B)|^2}$; report sample peak separately. This follows [Julius O. Smith, Fourier Theorems for the DFT, “Rayleigh Energy Theorem (Parseval’s Theorem)”](https://dsprelated.com/freebooks/mdft/Fourier_Theorems_DFT.html) with the stated 1/N scaling. Never subtract magnitude spectra or fit gain/time alignment.

## Verification and handoff

Independently compare the table against direct analytic $x,v,-\Phi'(x)v$, then normalized FFT coefficients against the folded table. Proposed bookkeeping tolerance: $10^{-12}$ V; primitive numerical limits: $10^{-10}$ V peak/$10^{-11}$ V RMS. These are new diagnostic limits, not altered existing gates; alias magnitude has no acceptability threshold.

Test zero coefficients; negative frequencies; conjugate symmetry; DC/Nyquist phases 0 and π/2; exact cancellation of equal-amplitude $f$ and $F_s-f$ sines with opposite phases; wrong derivative sign; and Parseval closure. Require the 96-kHz analytic alias contribution to be zero. Do not import the production propagator or symbolic audit into the independent oracle.

Retain exclusive-output plan JSON, compiler/binary/source hashes before/after, signed line CSV, raw float64 arrays (modal q/v, P, S, B, both errors), and metrics JSON including failures. Cap execution at four cases, 2048 samples/case, one worker, 30 seconds, 256 MiB process memory, 16 MiB artifacts. Timeouts remain incomplete.

This supports only prescribed stationary primitive behavior. Damping, onset/retrigger/release, finite-window leakage, and finite-kernel response remain separate; their residuals cannot be relabeled alias-only. Whole-engine direct convergence, C03, audibility, physical fidelity, and realtime deadlines remain open.

Actual checks: `python3 tools/project.py validate`, `sync --check`, and `next --json` succeeded (revision 32; no available unclaimed tasks). Documentation integrity also passed via python3 tools/check_docs.py. No DSP tests or numerical experiments ran. Next integrator: freeze this plan and assign separate harness/oracle owners; proposed files are apps/alias_primitive_probe.cpp, tools/validate_direct_alias_toy.py, and independent Python/C++ tests.
