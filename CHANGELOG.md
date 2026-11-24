# Change log

## Unreleased — experimental VST3 and research foundation

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
