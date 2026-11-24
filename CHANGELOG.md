# Change log

## Unreleased — experimental VST3 and research foundation

- Continued [native photo editor](docs/specs/PHOTO_EDITOR_20261124.md) preserves
  the original full photograph and exposes 24 host parameters (eight persistent
  controls and sixteen CC123 commands), with rapid pedal/Vibrato toggles,
  host-shortcut pass-through and fine input. White-key feedback follows visible
  contours while preserving black-key interiors; help remains unclipped at the
  minimum size. The
  [final Linux Direct replay](docs/agents/reports/photo-continuation-20261124/w4-061.md)
  passes 136/136 interaction and visual checks.
- [Callback cancellation](docs/validation/photo-continuation-20261124/CALLBACK_STATE_20261124.md)
  preserves newer host updates, and
  [audition-release handling](docs/agents/reports/photo-continuation-20261124/w2-034.md)
  suppresses stale notes and retains pending recovery on transport failure.
  Deferred native close/reopen and shared-connection child cleanup pass 39/39
  Linux lifecycle cases across all three variants:
  [13 Direct](docs/validation/photo-continuation-20261124/NATIVE_LIFECYCLE_20261124.md)
  and [26 Filtered/Quadrature](docs/agents/reports/photo-continuation-20261124/w4-062.md).
- [Fresh final-bundle PCM replay](docs/agents/reports/photo-continuation-20261124/w4-064.md)
  records 144 captures against 72 retained baseline captures, with all 608,256
  paired stereo frames exact at neutral panel/unity Expression. These Linux
  research results establish no Windows/macOS native editor, named-DAW or
  physical-fidelity claim; no release has been published.
- [Loaded front-panel audio checks](docs/validation/photo-continuation-20261124/final-loaded-audio/README.md)
  pass 96 fresh-instance renders and 36,000 callbacks across the three Linux
  bundles in float32/float64 stereo. Independent spectral probes distinguish
  Bass from Treble; channel-sum/separation checks establish opposed modulation;
  every switch-off ramp sample is checked at its in-block parameter offset.
- Global digital CC11 **Expression** on all three instruments, with independent
  automation, continued mechanics at zero, and an unchanged exact-unity output
  path. The [frozen contract](docs/specs/HOST_EXPRESSION_MONO_20261124.md) retains
  existing control/class/model identities and latencies while adding parameter
  ID 2, bringing that pre-panel stage to nineteen host parameters.
- Adaptive RHST configuration: unity Expression preserves exact 16-byte v1
  bytes; non-unity uses 20-byte v2. Own-model legacy loads restore unity and
  valid v2 unity loads save canonically as v1; strict lengths and transactional
  rejection remain. Output layout, transient commands and ringing state are
  excluded. See the [wire-format table](docs/specs/RUNTIME_AND_ASSETS.md#persistence-and-determinism).
- Default stereo with suspended mono/stereo negotiation, scalar mono output,
  bounded one/two-channel rejection cleanup and omitted inactive-output
  processing. Factory category is now `Instrument|Synth`; the
  [category correction](docs/validation/host-expression-category-correction-20261124.json)
  retains its first failure and nine passing targeted checks.
- Independent state, stream, control/audio, transaction, capacity, concurrency,
  mono and actual loaded-module fixtures, plus a reusable exact baseline replay
  and source-bound native diagnostics. The
  [verification guide](docs/EXPRESSION_VERIFICATION.md) separates the initial
  Linux full run, later focused results and 216-process legacy matrix; native
  feature acceptance and named-DAW verification remain separate gates.
- Batched six-node quadrature observation with adjacent prepared coefficients.
  The [retained Linux/GCC replay](docs/validation/batched-quadrature-20261124.md)
  preserves all 2,097 Q6 outputs and 24 legacy WAVs exactly; model identities,
  48-frame quadrature latency and numerical budgets remain unchanged.
- macOS CoreFoundation linkage and scoped Clang floating-point repairs, with
  unchanged exact continuation assertions. All three variants pass their SDK
  and new `moduleinfotool` metadata validators on Linux, Windows and macOS at
  the [recorded native checkpoint](docs/validation/native-platforms-20261124.md).
- Separate [four-fixture contact peak diagnostic](docs/CONTACT_PEAK_DIAGNOSTIC.md)
  with bounded parsing, independent value references, optional damped timing,
  explicit incomplete results and retained partial inputs. Finite contact stays
  offline; general extrema acceptance and positive preload timing remain open.
- Three retained [72-process callback captures](docs/BENCHMARKING.md), including
  the worse first batch and every timing outlier, plus separate whole-child CPU
  observations. These do not establish a general speedup or device deadlines.
- Read-only [archive verification](tools/verify_vst3_matrix_archive.py) checks
  bounded contents and recomputes the frozen workload/timing summaries without
  extracting or executing archived files. Corrupt inputs retain their rejection
  while later arguments are checked; matching hashes do not authenticate capture.
- [Benchmark](tools/run_vst3_matrix.py) and
  [audio replay](tools/verify_audio_compatibility.py) failure handling now retains
  attempted outputs, validates consumed comparison inputs before execution, and
  prevents timeouts or secondary snapshot errors from claiming completion.
- [48 additional invalid-cell scenarios](docs/agents/reports/20261124-w4-63-invalid-cell.md)
  exercise the wrapper's own NaN/Inf rejection, preserved output/history and
  reset fence. All 84 earlier fault scenarios and exact assertions remain; the
  five local wrapper CTests pass, and a removed-guard mutation fails as intended.
- [Current development-wave validation](docs/agents/DEVELOPMENT_WAVES_20261124.md)
  records actual assignments and immutable report hashes. Campaign progress is
  separate from task acceptance; PHY-002 and RT-001 remain incomplete.

- Additional model-3 **Rhodes Research Quadrature** instrument: six prepared
  fractional observations per host interval, fixed convolution storage and
  48-frame latency. Existing direct and filtered identities and saved states
  remain available; every cross-model recall is rejected.
- Independent [full quadrature study](docs/validation/quadrature-audio-full-20261123.json):
  2,097 cases and 2,534,400 frames pass both the independent Q6 comparison and
  resolved continuous-reference limits, with a maximum latter error of
  8.318901123516298e-12 V. These are synthetic numerical results.
- [Twenty-four legacy renders](docs/validation/quadrature-legacy-compatibility-20261123.json)
  retain their exact sample and complete WAV hashes. New retained tests cover
  fractional observations, event and reset behavior, allocation policy, three
  loaded modules, model-3 host contracts and bounded diagnostic output.
- Windows build/path portability repairs and portable quadrature host fixtures,
  including identical reset timelines under fused arithmetic. Warning policy,
  production DSP and numerical tolerances remain unchanged by the fixture repair.
- A separate [quadrature campaign ledger](docs/agents/QUADRATURE_CAMPAIGN.md),
  deterministic index and strict report/hash validation record this continuation
  without rewriting earlier campaigns or claiming unperformed work.
- Separate filtered VST3 variant with 4x/2x/1x internal processing, reproducible
  Kaiser FIR coefficients and explicit 48-frame latency; direct-model identities
  and state remain available. Cross-model state is rejected transactionally.
- Bounded rejected-output cleanup: unsupported shapes remain untouched; supported
  stereo cleanup visits at most two channels and 8192 frames.
- Independent FIR/wrapper/host tests, model-tag codec/stream regressions and
  variant-identified complete-callback measurements.
- Deterministic contact evidence selection using complete settings, explicit
  duplicate conflict detection and preserved original refinement failures.
- Optional VST3 instrument using the pinned official SDK, with generic gain and
  sustain controls, mono signal on a stereo bus, and float32/float64 processing.
- Bounded sample-offset events, overlapping MIDI note ownership, validated
  versioned configuration state and lock-free recall handoff to the audio owner.
- Retained processor/controller host tests and an SDK-validator CTest target.
- Separate offline finite-mass hammer/compliant-damper reference, reciprocal
  contact, energy/loss/work ledgers, typed failure and whole-call rollback.
- Independent contact fixtures, exact grazing specialization, relaunch snapshots,
  optional SciPy oracle and explicit unfinished refinement/extrema gates.
- Complete event-processor callback benchmark with bounded stress schedules and
  retained raw timing observations; no audio-device deadline guarantee.
- Cached modal transition matrices with matched historical timing and trajectory
  evidence, plus an actual VST3 callback benchmark for both sample formats.
- Floating-point-environment-independent state/control validation and conversion;
  retained host buffer, stream, automation and note-ownership model checks.
- Strict independent-auditor input/action validation and always-active numerical
  self-checks under optimized Python; failed contact convergence remains visible.
- Pinned SDK content verification, explicit plugin symbol visibility and a
  complete optional host-test build target.
- Five-wave development campaign with actual assignments and immutable reports.

- Original C++20 modal, pickup, and passive-network research primitives.
- Deterministic synthetic WAV renderer and analytical regression tests.
- Typed specimen/component/acquisition contracts with explicit unknown parameters.
- Explicit specimen/session evidence binding, held-out observation coverage, and
  opt-in local payload byte verification with immutable result reports.
- Synthetic callback benchmark with recorded scope, timing distributions and
  deadline comparisons.
- Canonical dependency graph, evidence registry, task leases, generated status,
  recovery procedures, and precise continuation prompt for development agents.
- Architecture, measurement, validation, and staged development specifications
  grounded in the supplied research.
- Cross-platform build and Python CI configuration, sanitizer job configuration,
  and documentation/provenance checks; actual results remain tied to each run.
- Traceable implementation and review campaign with actual worker reports.

All model constants remain synthetic. No physical specimen has been measured and
no audio plugin or calibrated instrument release is represented by this entry.
