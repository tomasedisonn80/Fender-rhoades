# Contact and runtime acceptance review — 2026-11-23

Evaluator: `development-root`, with independent criterion reviews by
[A079](../agents/development-20261123/W4-A079.md) and
[A080](../agents/development-20261123/W4-A080.md), historical-data preservation by
[A082](../agents/development-20261123/W5-A082.md), and an independent aliasing
method review by [A084](../agents/development-20261123/W5-A084.md).

**Decision: keep PHY-002 and RT-001 incomplete.** The campaign delivered useful
implementations, repairs and measurements. It did not pass every scientific
acceptance gate. Fidelity remains `unvalidated`; no identified physical specimen
or calibrated measurements have been acquired.

## Frozen definitions and evidence identities

The [frozen definitions](development-task-definitions-20261123.json) contain the
exact acceptance text, target, dependencies and required kinds. Both tasks depend
on completed `PHY-001`. The unchanged target is: **One documented 73-key Mark I
Stage specimen at its specified loaded electrical output; identity and
measurements pending.**

| Task | Definition SHA-256 | Required evidence |
|---|---|---|
| PHY-002 | `e51dfa484a4a2f600fa3b6eaf357a8a2a85bfade777745ceb55fd0af8ba4ee03` | numerical |
| RT-001 | `134aae8a07f36b75bc986390376f51ca5fbb8abc0955d57913dda04d1ad4f6b8` | performance, numerical |

The contact audit is bound to its actual historical evaluator and eight tracked
source inputs at `b181822ce576167ff21b47c82d54125cf8518fda`, with candidate hashes
and generation commands in the [generation manifest](contact-generation-manifest-20261123.json).
The [execution record](contact-audit-execution-20261123.json),
[summary](contact-audit-summary-20261123.json), and
[complete audit](contact-refinement-audit-20261123.json) preserve the failed run.
Later auditor repairs and targeted reruns do not retroactively replace it.
The approximately 105 MB complete candidate aggregate remains in the transient
build directory. The durable generation manifest records its commands and hash,
but does not contain that full payload; reproducing the whole audit after loss
of the build directory requires regenerating the candidates.

[A095](../agents/development-20261123/W5-A095.md) additionally preserves a
[self-contained failure subset](contact-failure-examples-20261123.json): six
complete semantic run objects, 3,306 samples and 24 events for `damped_impact`
and `supported_impact_scaled` at 10, 5 and 2.5 microsecond maximum steps. Its
embedded standard-library verifier reproduces all seven selected failed
refinement decisions in 43,221 checks. Archive SHA-256:
`bfddf45e36ca0e43cd9bfa6b8e07611be7fd351b33d4cf40f88af0d6bfaa8a42`.
This preserves a useful counterexample subset, not the full matrix, original
JSON whitespace, continuous extrema or internal accepted-interval stages.

The [historical runtime archive](runtime-historical-raw-20261123.json) preserves
all sixteen original A038/A041 timing JSON files, both manifests, wrappers,
reports, and the old/new trajectory harness observations as exact UTF-8 payloads.
Its embedded standard-library verifier reproduced 14,000 durations and 64
quarter-window summaries in 1,368 checks. The archive SHA-256 is
`cc821f2911dc668e65279068be4899a8b13929a813520f2c83d9cc4c981f0a05`.
This closes a preservation gap; it is not a new performance experiment.

The [actual VST3 callback data](vst3-callback-benchmark-20261123.json) and
[aliasing observations](aliasing-observations-20261123.json) retain complete
procedures, inputs and source/build identities. A080 checked their twelve and
five respective source inputs against `301558122aa8e61100dc6857555789e8c5b887a9`.
Immutable agent reports identify any later source evolution and the narrower
checks performed on it. Hash identity supports reproducibility, not physical
authenticity or universal numerical accuracy.

## PHY-002 criterion decisions

| Criterion and exact text | Supporting evidence and observed coverage | Remaining limitation | Decision |
|---|---|---|---|
| **PHY-002/C01:** Use finite hammer mass, reciprocal contact forces, separation and recontact with persistent resonator state. | A079 reviewed the finite-inertia hammer equations, opposite projected contact forces, persistent resonator state, and natural repeated contact/separation in the retained `supported_impact` trajectory. A070 independently checked relaunch snapshots, preserved coordinates and kinetic work. | This is the selected two-mode, p=1 synthetic offline reference. It is not the audio engine or a calibrated action. | Pass within the frozen reference scope |
| **PHY-002/C02:** Model damper/pedal contact and resting equilibrium; identify or label all assumed contact parameters. | A079 traced the compliant prescribed damper face, consistent motion and signed work, assembled preload equilibrium, both perturbation signs and moving-face fixtures. Headers and the frozen contact contract label parameters synthetic. | Prescribed face motion does not model a complete mechanical key/pedal assembly. No historical felt law or specimen parameters were identified. | Pass within the frozen reference scope |
| **PHY-002/C03:** Pass energy, timestep convergence, retrigger, release and pedal stress checks with explicit numerical tolerances. | The 208-run audit has zero sampled-oracle comparison failures and 544 matching ordered events. The frozen budgets remain unchanged. A056 inspected the failed refinements; A070 added seven targeted analytic/Radau action comparisons and checked all 208 action/input records. | **26 refinement checks fail**: 20 waveform/energy and six event-time checks. All 208 runs lack independently located continuous-time force/compression extrema. Targeted follow-ups do not close these gates. | Fail; extrema acceptance incomplete |

The measured maximum sampled cumulative energy residual in the retained audit is
`1.32167e-12 J`; being small does not waive the separately required refinement
and extrema checks. The six event-time failures have overlapping uncertainty
brackets. That diagnosis is not permission to redefine or remove the checks.

Next work: instrument accepted-interval errors and branch crossings for
`damped_impact` and `supported_impact_scaled`; evaluate safeguarded event splitting
against the unchanged contact acceptance contract. Add independent continuous
extrema location, then rerun the complete frozen matrix. Keep offline contact
outside the audio callback until a separately bounded approximation is validated.

## RT-001 criterion decisions

| Criterion and exact text | Supporting evidence and observed coverage | Remaining limitation | Decision |
|---|---|---|---|
| **RT-001/C01:** Benchmark representative kernels with hardware, compiler, sample rate, block size, polyphony and stress conditions recorded. | A038/A041 record complete EventProcessor-call workloads; A069 records actual RhodesProcessor callbacks at 48/192 kHz, 64 frames, 73 keys, three workloads and both sample formats. Compiler, visible machine and workload boundaries are retained. | Shared Linux container scheduling and power state were uncontrolled; no DAW, audio-device or wake-up measurement. | Pass within declared synthetic scope |
| **RT-001/C02:** Report maximum observed callback duration and deadline misses, plus optimized-versus-reference error. | A080 recomputed the timing arithmetic. A069 observed 3,600 successful callbacks, two durations above the nominal block period, and a maximum of 1873.831 microseconds. A041's paired cache observations retain identical output/state digests for four finite trajectories. | The independent operating-domain numerical-error budget remains incomplete. Same-equation cache equivalence and selected traces do not establish it. Observed maxima are not worst-case guarantees. | Incomplete |
| **RT-001/C03:** Verify allocation/locking policy and timestep/antialiasing convergence; claim only measured performance. | A024's calibrated C++ allocation interception observed zero attempts in 89 actual host calls. Fixed storage, lock-free single-attempt mailbox operations, A049 mailbox TSAN and A081 actual-processor concurrency checks support their stated boundaries. A052/A068/A084 supply symbolic and quantitative alias diagnostics. | Foreign host callbacks and uninstrumented SDK archives are outside the complete proof. Full timestep/antialiasing convergence and a justified domain-wide bound remain unestablished. | Incomplete |

The historical cache comparison reduced matched 48 kHz pedal-workload median
time from 1400.686 to 229.495 microseconds and 192 kHz median time from 1367.170
to 229.063 microseconds. The newer VST3 benchmark includes additional ownership,
automation, state and host-interface work; its timings are a different experiment.
Neither can be promoted to a portable speedup or deadline promise.

A084 independently replayed 315 alias-method checks without rendering new audio.
For the selected MIDI 100/48 kHz early window, the residual is -136.515637 dB
relative to reference RMS; a predicted folded component explains 97.3027% of its
squared norm. Direct aligned high-rate decimation aliases the same carriers,
filter transitions affect onset, and float32 WAV export limits later residuals.
These observations establish neither audibility nor absence of aliasing.

Next work: predeclare justified reference-error and aliasing budgets over
supported rates, keys and event sequences, including onset/release and filter
uncertainty. Compare the runtime with an independent reference and retain all
inputs. Evaluate an antialiasing method against those budgets, then measure its
complete callback cost under controlled conditions and in actual hosts.

## Execution and decision boundaries

Numerical observations used Linux x86_64, GCC 14.2, CMake 4.4.5 and Python 3.12.14;
the independent contact and spectral work used NumPy 2.3.5/SciPy 1.17.0. The
recorded contact command took 114.216 seconds and exited 1. The later reviews
recounted existing data; they did not repeat that full sweep. Their exact scripts,
commands, tolerances, source hashes and unrun conditions are in the linked reports
and durable artifacts.

The integrator registers this decision as documentary evidence, retains the
failed/inconclusive numerical evidence, and releases both task claims with the
next experiments above. No acceptance text, historical report, numerical budget,
specimen identity or fidelity status is changed to obtain completion.
