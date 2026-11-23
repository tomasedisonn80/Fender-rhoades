# Numerical contract of the real-time research kernel

This describes the synthetic `ResearchEngine` used by the renderer and optional
VST3 adapter. It is not a calibrated instrument or a measured deadline guarantee.
The authorities are the [public API](../include/rhodes/research_engine.hpp),
[implementation](../src/research_engine.cpp), and existing
[primitive](../tests/cpp/research_engine_test.cpp) and
[boundary/regression](../tests/cpp/review_regression.cpp) tests.
All engine constants are synthetic. Finite-mass hammer contact, physical damper
contact, mechanical coupling, magnetic reaction, and antialiasing are absent
from this audio engine. The separate [offline contact reference](CONTACT_REFERENCE.md)
implements finite-mass hammer/compliant-damper experiments with different
integration, storage and failure contracts; it does not run in the VST3 callback.

## Modal coordinates, frequency, and decay

Each independent mode has displacement $q$ in metres, velocity $v=\dot q$ in
metres per second, and positive modal mass $m$ in kilograms. It obeys

$$\ddot q+2\beta\dot q+\omega_0^2q=0,\qquad \omega_0=2\pi f_0.$$

`frequency_hz` is the **undamped natural frequency** $f_0$, not the oscillation
frequency under damping. `decay_per_s` is $\beta$ in inverse seconds, not
$2\beta$ or a decay time. Only $0\leq\beta<\omega_0$ is implemented, with
$\omega_d=\sqrt{\omega_0^2-\beta^2}$ and damped frequency $f_d=\omega_d/(2\pi)$.
The displacement envelope is proportional to $e^{-\beta t}$, so its 60 dB
amplitude decay time is $T_{60}=\ln(1000)/\beta$; at $\beta=0$ it is infinite.
This envelope relation does not make instantaneous mechanical energy a pure
exponential at every phase.

## Exact discrete transition

For sample interval $h=1/F_s$, set $c=\cos(\omega_dh)$ and
$s=\sin(\omega_dh)/\omega_d$. The stored state advances by

$$\begin{bmatrix}q_{n+1}\\v_{n+1}\end{bmatrix}
=e^{-\beta h}\begin{bmatrix}c+\beta s&s\\-\omega_0^2s&c-\beta s\end{bmatrix}
\begin{bmatrix}q_n\\v_n\end{bmatrix}.$$

For $A=\left[\begin{smallmatrix}0&1\\-\omega_0^2&-2\beta\end{smallmatrix}\right]$,
$(A+\beta I)^2=-\omega_d^2I$ gives this matrix directly from $e^{Ah}$.
It is the analytic constant-coefficient transition, rather than a time-step
approximation to the differential equation. Double rounding still accumulates.
The implementation evaluates $\omega_d$ as
$\sqrt{(\omega_0-\beta)(\omega_0+\beta)}$ and $s=h\,\mathrm{sinc}(\omega_dh)$;
for $|\omega_dh|<10^{-6}$ it uses $\mathrm{sinc}(z)\approx1-z^2/6$.
The first omitted term is $z^4/120$. Critical and overdamped solutions are
outside this API even though the underdamped transition has a finite limit.

## Events, observation time, and energy

A successful sample observes all modes at $t_n$, combines their motion into
pickup sources at $t_n$, and leaves all modes at $t_n+h$. Internally each mode
advances immediately after its old state is read. A strike before processing
therefore affects the first output sample at that same timestamp; there is no
implicit one-sample delay. A direct `ResearchEngine` caller splits processing at
an event boundary. The [EventProcessor wrapper](../include/rhodes/event_processor.hpp)
also accepts a caller-owned span of sample-offset events and performs this
dispatch before each affected sample; it does not retain a queue between calls.

An impulse $J$ in newton-seconds changes $v^+=v^-+J/m$ while $q^+=q^-$.
The engine uses $J_m=m(0.06\,\mathrm{m/s})I\,b_m$, intensity $I\in[0,1]$,
and synthetic couplings $b=(1,0.45,0.2)$. This is prescribed momentum input,
with no hammer mass, contact duration, rebound, or contact-force calculation.
The mechanical energy and its continuous unforced derivative are

$$E=\tfrac12m(v^2+\omega_0^2q^2),\qquad \dot E=-2m\beta v^2\leq0.$$

An impulse changes energy by $\Delta E=Jv^-+J^2/(2m)$, which can be negative
for an impulse opposing sufficiently large existing velocity. Retrigger energy
must include this phase-dependent cross term. Changing only $\beta$ preserves
$q$, $v$, and instantaneous $E$; it changes subsequent loss. Release/pedal use
this switched viscous-loss surrogate. `mechanical_energy_joules()` sums all
219 modal energies; it does not account for electrical energy or reciprocal
power transfer, since the current pickup does not react on the mechanics.

### Bounded event wrapper

`EventProcessor::process` accepts at most **4096 frames and 256 physical-key
events per call**. Offsets must be inside the block and nondecreasing; equal
offsets require strictly increasing producer sequence numbers. Supported events
are note-on, note-off, sustain, all-notes-off and reset. Every count, domain and
ordering check completes before mutation. Invalid input returns `invalid_input`,
zeros the supplied output and preserves mechanics, cursor and fault state. An
empty output accepts only an empty event list. Clearing an oversized rejected
buffer still costs time proportional to its supplied size.

An event-application or sample arithmetic failure retains successful prior
samples, zeros the failed sample and remainder, reports the failing index and
subsystem, and latches `reset_required`. It does not roll back partial mechanics.
Only public `reset()` or successful `prepare()` clears that latch and resets the
64-bit successful-frame cursor. An in-stream reset changes engine mechanics at
its offset but does not restart the cursor or bypass an existing fault latch.
The audio-owner `set_sustain()` port permits a prevalidated parameter flush
without advancing mechanics.

The wrapper uses fixed storage without callback allocation, locking or I/O.
MIDI/channel ownership is handled separately by [NoteTracker](../include/rhodes/note_tracker.hpp).
The [VST3 adapter](specs/HOST_PROTOTYPE_20261123.md) has its own 8192-frame host
bound and preflights the whole host block before issuing one-sample core calls;
that host capacity does not change the wrapper's per-call limits.

## Pickup: sum motion, then differentiate linkage

For each key, $x=\sum_m a_mq_m$ and $\dot x=\sum_m a_mv_m$, with
dimensionless observation weights $a=(1,0.3,0.08)$. The linkage and source EMF are

$$\lambda(x)=a_1x+a_2x^2+a_3x^3,\qquad
e=-\frac{d\lambda}{dt}=-(a_1+2a_2x+3a_3x^2)\dot x.$$

Here the polynomial coefficients $a_1,a_2,a_3$ are distinct from the modal
observation weights; their units are weber-turns per metre, metre squared, and
metre cubed respectively. Linkage already includes winding turns; do not
multiply by turn count again. Default coefficients are synthetic $(0.4,800,400000)$.
The minus sign fixes the implementation's polarity convention. Differentiation
is analytic, not a sampled finite difference. Applying the nonlinearity to
each mode separately would omit cross terms; for the quadratic term alone,
the two-mode discrepancy is $-2a_2(x_1v_2+x_2v_1)$.
There is no measured displacement-domain or extrapolation guarantee.

## Synthetic resistive network

There are 24 series-connected groups: keys 0–3 form one four-branch parallel
group, followed by 23 three-branch groups. Each branch has source $e_i$ and
equal series resistance $R_b$. KCL for a group of $N$ branches gives its
open-circuit source $\bar e=\sum e_i/N$ and resistance $R_b/N$. Consequently

$$V_{\mathrm{oc}}=\sum_{i=0}^{3}e_i/4+\sum_{i=4}^{72}e_i/3,\qquad
R_s=R_b(1/4+23/3),\qquad V_L=V_{\mathrm{oc}}R_L/(R_s+R_L).$$

A silent source remains a resistor; removing its branch changes the circuit.
Defaults are synthetic $R_b=200\,\Omega$ and $R_L=10^6\,\Omega$.
Output samples are loaded voltage in volts. This candidate wiring is unverified
against a specimen and contains no inductance, capacitance, controls, or cable.
For $R_s>R_L$, `frexp`/`scalbn` scaling avoids overflowing a resistance ratio
or prematurely underflowing its reciprocal when the final voltage is representable.

## API acceptance, failure, and resets

| Operation | Current contract |
|---|---|
| Modal `configure` | Finite $32000\leq F_s\leq192000$, $0<f_0<0.45F_s$, $m>0$, and $0\leq\beta<\omega_0$; transition entries must be finite. Success resets state; rejection preserves the prior object. |
| Modal updates | `set_state`, `add_impulse`, `advance`, and `set_decay` reject unconfigured use. State inputs and computed energy must be finite. Rejected operations preserve prior state and coefficients. Accepted `set_decay` preserves state. |
| Modal `reset` | Zeros $q,v$; retains configuration and damping and does not configure an unused object. |
| Pickup/network | Coefficients must be finite; resistances positive and finite with finite computed $R_s$. Failed configuration preserves parameters. Nonfinite inputs/results return `nullopt`. |
| Engine `prepare` | Constructor prepares 48 kHz. Successful preparation at a supported rate rebuilds all modes at rest, releases keys and pedal, and sets the rate. Failure is atomic. |
| Notes and pedal | Keys are 0–72; finite intensity is 0–1. Zero intensity releases. A successful strike adds momentum; release and pedal preserve $q,v$. Invalid notes reject atomically. `all_notes_off` releases keys while respecting sustain. |
| Engine `reset` | Hard reset zeros mechanics and releases keys/pedal, retains sample rate, and restores released damping. |
| `process` | Empty span succeeds without advancing. On a fault, the failed sample and remaining buffer become zero; earlier output remains. Returns false without rolling back partially advanced mechanics. Call `reset` or successful `prepare` for a known recovery state. |
| State queries | Invalid key/mode indices return `nullopt`. Energy getters have no failure channel; the aggregate sum can overflow even if individual accepted energies are finite. |

Finite inputs alone do not guarantee usable arithmetic: squared-state terms,
polynomial intermediates, and source summation can overflow before a mathematically
representable final answer. Cancellation, underflow, subnormals, and loss of tiny
increments remain double-precision limits. Very small natural frequencies can
underflow products used in the transition. No arbitrary-exponent accuracy,
cross-platform bit identity, or denormal timing guarantee is asserted.

## Numerical rate support and independent validation

Accepted rates express the implemented numerical domain. The modal frequency
guard does not band-limit the pickup: cubic conversion generates harmonics and
sum/difference products, potentially above Nyquist. Strikes and damping switches
also introduce broadband changes. Exact oscillator propagation therefore supplies
no aliasing bound. Oversampling, reconstruction filtering, and convergence studies
are future work; higher supported rates alone do not certify sufficient quality.

Existing tests encode scalar closed-form trajectories, energy behavior, preserved
state, pickup derivatives/cross terms, loading, rate endpoints, atomic rejection,
and block partition invariance. Their presence is not a claim that a particular
build passed. Future reference work should compare independent scalar trajectories
at matched physical times (including near-critical damping), quantify accumulated
roundoff, verify impulse energy including $Jv^-$, compare the network to independent
KCL equations, and refine finite-difference linkage checks over multiple step sizes.
Separately compare a filtered high-rate electrical reference after aligned
downsampling; record spectral alias residuals, tolerances, compiler, and precision.
No numerical result by itself establishes measured physical fidelity.
