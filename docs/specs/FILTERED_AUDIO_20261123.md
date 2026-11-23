# Filtered synthetic audio — frozen implementation contract

Integrator decision, 23 November 2026, before implementation. This extends
RT-001 and HOST-001 engineering work without changing their scientific gates.
The underlying parameters remain synthetic; no instrument measurements or
audibility improvement are claimed. The original `ResearchEngine`, VST3 class
identities and model-1 saved-state interpretation remain available.

## Signal path and timing

Add `AudioEngine` with the same event-facing operations as `ResearchEngine`.
It owns an unchanged research core at an internal rate and a fixed-storage
causal FIR. Evaluate the nonlinear pickup at that internal rate, sum the full
loaded harp, then filter and decimate. All 73 electrical branches remain in the
network. Filtering host-rate aliased samples is not this implementation.

| Host rate (Hz) | Factor L | Internal rate | FIR taps |
|---|---:|---|---:|
| 32000 through 48000 | 4 | L times host rate | 385 |
| Above 48000 through 96000 | 2 | L times host rate | 193 |
| Above 96000 through 192000 | 1 | Host rate | 97 |

Coefficients use `N=96L+1`, Kaiser beta 9, cutoff `0.47 * host_rate`, normalized
to unit DC gain. The exact mathematical recipe and independently measured
candidate arrays are in [worker 005](../agents/continuation-20261123/w1-005.md).
Serialized coefficients must be reproducible from a documented generator;
numerical comparison to the independent recipe allows absolute error `5e-15`.

All factors have exactly **48 host frames of linear-phase delay**. At host
frame n, apply events once before internal index `L*n`, produce the filtered
output at that index, then consume the remaining L-1 substeps. This phase is
required: returning the final substep shifts the delay fractionally. Mechanical
diagnostics after a call refer to the undelayed physical state at its end time.

Prepare validates first and publishes atomically. Invalid prepare preserves
mechanics, host rate and filter history. Explicit reset clears mechanics,
history and phase. Retrigger, release and sustain preserve history and mechanical
state. Partitioning a stream does not change output. An arithmetic fault follows
the existing reset-required contract; no hidden clipping or retry loop is added.
Processing allocates no memory, performs no I/O or locking, and uses fixed bounds.

## Independent numerical gates

Freeze these limits before testing the implementation:

- FIR passband: magnitude within +/-0.05 dB through `0.446 * host_rate`.
- FIR stopband: magnitude at most -80 dB from `0.5 * host_rate` to internal
  Nyquist. At L=1 this interval contains only the Nyquist endpoint.
- Compare coefficients and dense-grid response independently of the C++ ring
  implementation. A frequency grid is an observation; do not call it proof of
  every continuous frequency unless a separate bound establishes that claim.
- Impulse/convolution tests check exact phase and 48-frame group delay. A
  separate direct convolution oracle checks ring wrap, startup and long tails.
- Compare wrapper output with an independent high-rate core stream plus direct
  FIR convolution, with absolute error <=1e-12 V for normalized strikes in the
  retained synthetic test cases. Do not fit away gain, phase or onset time.
- Verify all rate boundaries, rejected preparation, reset, sustained/released
  notes, retriggers and partitioning. Compare q/v at the same physical time
  separately from delayed electrical output.
- Measure complete callback cost and allocations. A passing filter response
  does not bound internal-rate aliases, every transient, or real-time deadlines.

Preserve failing observations. Do not widen these gates to make an implementation
pass. Broader RT-001 approximation/convergence and hardware timing obligations
remain separate from this bounded feature.

## VST3 compatibility

Build a second optional bundle, `RhodesResearchFiltered`, with a distinct fixed
processor/controller FUID pair and bundle identifier. Its headless host controls
remain gain and sustain. The original `RhodesResearch` bundle retains direct
model-1 rendering and zero reported latency. The filtered bundle reports 48 frames
and uses model tag 2 in the existing 16-byte RHST format.

Extend codec/stream APIs with an explicit model argument defaulting to 1.
Do not macro-vary shared inline codec definitions or default arguments. Each
variant passes its fixed expected model; cross-model recall is rejected
transactionally by processor and controller. No live quality switch, dynamic
latency change, or reinterpretation of model-1 state is introduced.

The variant selection definition is private and consistent across every source
in that executable/module. Shared core/SDK binaries do not inherit it. Keep
hidden implementation visibility and distinct factory names. Test both SDK
validators, state roundtrips/cross-rejection, simultaneous module loading,
latency reporting, filtered event timing and reset/flush behavior.

## Public basis and scope

The physical-model direction is consistent with Pfeifle's public
[DAFx-2017 research](https://www.dafx.de/paper-archive/details/W0D0fMDJBUxiacAi_398wg).
That paper's measurements are not measurements of this implementation or a
selected specimen. The filter recipe uses documented
[window-method FIR construction](https://docs.scipy.org/doc/scipy/reference/generated/scipy.signal.firwin.html).
The separate alternative of antiderivative/convolution methods is discussed in
[public DAFx work](https://dafx.de/paper-archive/2016/dafxpapers/20-DAFx-16_paper_41-PN.pdf).
These are references to public knowledge, not copied source, audio or artwork.
