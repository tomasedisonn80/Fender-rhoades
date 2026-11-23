# Quadrature audio: implementation and verification contract

Frozen before the C++ implementation, 23 November 2026. This is an additive
RT-001/HOST-001 experiment using the existing synthetic mechanical/pickup/harp
model. Existing direct and sampled-filtered instruments keep their identities,
state semantics and output algorithms. Model 3 is a separately identified
quadrature instrument, not a calibrated Rhodes or a replacement for model 2.

## Algorithm

Use six-point Gauss–Legendre quadrature on every host-frame interval. Nodes
`a[j]` lie strictly inside `(0,1)` and weights `w[j]` sum to one. At the start of
frame `n`, emit the convolution of already completed cells. Observe the same
synthetic source at `n+a[j]`, store the six voltages for the next output, and
advance the authoritative mechanical state once to boundary `n+1`.

For the existing causal Kaiser/sinc kernel `K(s)`, supported on `[0,96]`, prepare
six 96-tap branches:

`c[j][k] = w[j] * K(k+1-a[j]) / sum(j,k,w[j]*K(k+1-a[j]))`.

The most recent completed cell is age zero. At frame `n`, its samples are
`x(n-1+a[j])`, making the kernel argument `1-a[j]`. This indexing retains the
kernel's **48 host frames** of group delay; it introduces no extra output frame.
Node/tap mirror symmetry is retained explicitly. This integrates interior
points instead of assigning full sample weight to the strike endpoint.

Every event occurs at a host-frame boundary in existing producer order. Retrigger,
release, sustain and all-notes-off preserve all modal and convolution history.
Hard reset clears mechanics, histories and ring position. A reset and strike at
the same offset follow their supplied order. State observation is independent
of block partitioning and never resets the motion of an already moving key.

## Bounded implementation

`ResearchEngine` remains the sole owner of held/sustain and modal state. Add a
rate-bound, fixed-storage fractional observation plan prepared for six offsets
and both existing damping regimes. Reuse the same synthetic constants, pickup
and loaded harp. Evaluate observations without mutation, allocation, locking,
I/O, or per-sample transcendental functions. Preserve the existing sample-rate
validation and the original process/event arithmetic.

The wrapper owns a prepared plan and fixed six-by-96 history. Preparation must
validate all candidates before publication; rejected rate/preparation leaves
mechanics, histories and metadata unchanged. Empty processing is a no-op.
Failure zeros the failed output sample and remaining buffer and requires the
existing explicit-reset recovery; never publish a partial node observation.
Reset must reuse the prepared plan rather than recompute it.

One core boundary advance per frame keeps mechanical state comparable with the
direct engine at exactly the same physical time. Optional future optimizations
must preserve that contract and require new timing/error observations.

## Numeric gates

The earlier exploratory continuous-reference limits are retained unchanged:

- Independent Q16→Q32 and Q32→Q64 refinement: maximum `1e-9 V`, RMS `1e-10 V`.
- C++ Q6 output versus independently evaluated Q6 source/convolution: maximum
  absolute error `1e-12 V`, with no fitted gain, phase, onset or time shift.
- Resolved Q64 continuous comparison: maximum `1e-4 V`, RMS `1e-5 V`.
- Generated coefficients versus an independent numerical recipe: absolute
  error at most `5e-15`; node/weight error at most `2e-15`.
- The combined coefficient sum must differ from one by at most `5e-15`.

Retain the existing 2,097-case / 2,534,400-frame plan, nine exact rates,
intensities, event scripts, reset epochs and all failures. The original model-2
reference and historical reports remain intact. The Q6 integrand is not a
polynomial; polynomial exactness alone cannot establish the waveform bound.
Pilot results guide implementation but do not establish full-matrix coverage.

Test branch convolution against an ordinary independent sum, every rate
boundary, event ordering, reset, empty/partitioned buffers, invalid preparation,
finite-output guards, and persistence of boundary modal state. Compare both old
models against their retained behavior. Measure allocation policy and callback
cost separately; these numerical gates are not audibility or realtime guarantees.

## Host and saved-state compatibility

Add `RhodesResearchQuadrature` with a fresh processor/controller FUID pair and
bundle identifier. It reports 48 latency frames and state model tag 3 inside
the unchanged 16-byte RHST v1 envelope. Default codec/stream arguments remain
model 1. All six ordered cross-model recalls must fail without changing state,
pending valid recall, note ownership, mechanics, controls or histories.

Keep monitoring gain and sustain control identities/ranges. Variant definitions
are private to each target; shared core/SDK code never sees them. Reject mutually
exclusive variant macros. Benchmark/report metadata must name quadrature and
its node count rather than describe nonuniform nodes as uniform oversampling.

Build/validate all three native modules, verify simultaneous loading, resource
notices and complete bundle archives. Actual platform CI results and real DAW
testing remain distinct. Large test fixtures should be heap-owned outside the
audio allocation probes to avoid overflowing native Windows thread stacks.

## Public basis

The [SciPy Gauss–Legendre documentation](https://docs.scipy.org/doc/scipy/reference/generated/scipy.special.roots_legendre.html)
describes nodes, weights and polynomial exactness. The original generator uses
standard Legendre recurrences and the existing original Decimal kernel math;
no third-party source is copied. The independent audit uses separate SciPy
quadrature. The [Pfeifle DAFx paper](https://www.dafx.de/paper-archive/2017/papers/DAFx17_paper_79.pdf)
supports the physical-model research direction, not this implementation's
synthetic parameters or a measured fidelity claim. See the worker reports
under `docs/agents/transient-campaign/` for the preceding exploratory evidence.
