# Experimental VST3 acceptance — 24 November 2026

Evaluator: `development-root`, with independent criterion reviews by
[W5-82](../agents/reports/20261124-w5-82-host-events-state.md) and
[W5-83](../agents/reports/20261124-w5-83-host-platform-contract.md).

**HOST-001 passes its four unchanged criteria within the documented synthetic
engineering-prototype domain.** The current review covers all three optional
VST3 variants and the actual recorded Linux, Windows and macOS native tests.
It does not complete the separate physical-model or runtime-convergence tasks,
qualify a commercial DAW/audio device, or publish a product release.

## Frozen definition and source binding

The [frozen definition](development-task-definitions-20261124.json) retains the
exact criteria below, dependency `PHY-001`, required kinds `numerical` and
`documentary`, and unchanged target: **One documented 73-key Mark I Stage
specimen at its specified loaded electrical output; identity and measurements
pending.** PHY-001 is complete. The prescribed canonical projection hashes to
`b5e43a5c4bfccd246d49bf60783b320a1629741489023757d01cb50207e48336`.

The [reviewed-source manifest](development-source-manifest-20261124.json)
records 156 file identities at
`766dd11c73cf9d1dcf6c1960b4333767356f9937`: current native inputs, Python
tool/test sources and relevant documentation. All 74 native source/test inputs
match the actual tested code checkpoint
`240170673983f9992e4657a2d96c2172ece1ac30`. The only change among the earlier
`015767f` native manifest's inputs is the expanded quadrature fault-test fixture.
The old 288-case local integration run remains attributed to its original
snapshot; it is not relabelled as a run of that later fixture.

Current hosted execution is retained in the [native record](native-final-20261124.json)
and its indexed archive, with exact run/job IDs, decoded log hashes, commands,
compiler/platform identities, SDK pins, source hashes and explicit skips.
The [final local Python record](python-final-20261124.json) separately records
the exact command at observed HEAD `420c4c7`: 543 tracked inputs match that
commit, while the then-untracked Wave 5 ledger is separately bound by its
before/after hashes. All 544 captured inputs stayed unchanged during the run.
Later task/evidence/ledger updates are integration metadata, not retroactive
claims about tested inputs.
No review includes its own future decision or hash in its source identity.

## Criterion-by-criterion decision

| Criterion and exact text | Supporting evidence and observed coverage | Remaining limitation | Decision |
| --- | --- | --- | --- |
| **HOST-001/C01:** Bounded sample-offset event processing preserves producer order and physical key state; invalid blocks reject without mutation. | W5-82 maps `BasicEventProcessor`, the staged note tracker, ordered parameter/note translation and current host tests to retained scalar, partition, equal-offset, ownership, automation and invalid-atomicity selectors. Current native suites retain those passing selectors for all three variants. The later Q6 invalid-cell fixture adds 48 scenarios covering NaN/Inf at every one of six nodes and four offsets; direct and fenced checks retain successful prefixes, zero failed suffixes and preserve 129-frame continuation/FIR history. W4-63's retained mutation check detects removal of the finite-cell guard; W4-70 supplies subsequent native confirmation. | Rejection preserves musical/configuration state, not scratch storage or permitted output cleanup. An accepted arithmetic fault preserves its successful prefix and latches reset-required. Accessible host buffers and the documented single audio owner are required. | Pass within the declared host domain |
| **HOST-001/C02:** An optional VST3 instrument builds against an explicitly identified public SDK and passes the available SDK validator with bounded audio processing and documented output scaling. | Current native run 45340923158 builds all three modules against the pinned public SDK and passes three SDK plus three module-metadata validator CTests on each OS. Actual-processor scalar and allocation selectors pass. The implementation uses fixed event/parameter/history storage, explicit loop bounds and bounded single-attempt mailbox operations. Documented output is `(32 * normalized_gain) * synthetic_harp_volts`, converted to both output channels. Optional-SDK isolation, dependency notices and build boundaries are reviewed in W4-72/W4-76 and W5-83. | Bounds apply to plug-in-owned processing and supported declarations; arbitrary foreign getters may block, and pointers cannot be proven accessible by C++. Allocation interception does not cover direct malloc, unrelated threads or all external library behavior. These are not callback-deadline guarantees. | Pass within the declared prototype domain |
| **HOST-001/C03:** Host configuration round-trips through a versioned validated state format; MIDI range, sustain, overlapping notes, reset and unsupported host conditions are explicit and tested. | W5-82 maps exact RHST v1 wire bytes, model tags 1/2/3, all six ordered foreign-model rejection directions, fragmented/failed streams, atomic recall, ownership and concurrency selectors to the current source and native logs. MIDI 28–100 across 16 channels, anonymous/identified overlap, zero-velocity release, binary sustain, lifecycle reset, parameter flush and unsupported setup/input conditions have explicit tests. Q6 recall resets convolution history and applies only after accepted preflight. | State is configuration plus reset, not a ringing-mechanics checkpoint. One audio owner and synchronized lifecycle transitions are required. Pitch bend, note expression and half-pedal behavior are not advertised. Exhaustive concurrency schedules and arbitrary host implementations are untested. | Pass within the documented configuration/MIDI scope |
| **HOST-001/C04:** Document the actual tested platform and synthetic sound boundary without claiming measured fidelity or an untested release. | README, VST3, development/dependency/numerical guidance and current native records identify the actual tested environments, synthetic impulse/switched-loss/polynomial/resistive model, separate offline finite contact and incomplete scientific gates. The proposed DAW guide explicitly requires a fresh host process and actual loaded-path/build identity; it records no executed DAW session. Current source/byte provenance and public-source limitations are reviewed by W5-83 and the Wave 4 documentation audits. | No instrument specimen, calibrated fidelity, native DAW/device qualification, minimum Linux distribution, installer, signing/notarization, custom editor or product release is established. An owner-selected project-wide software license remains outstanding. | Pass within the documentary criterion |

## Executed checks and exact scope

The native evidence uses [Foundation run 45340923223](https://github.com/baabi-boo/Fender-rhoades/actions/runs/45340923223)
and [Experimental VST3 run 45340923158](https://github.com/baabi-boo/Fender-rhoades/actions/runs/45340923158),
both at `2401706`. All ten jobs succeeded. CTest selectors and their embedded
scenario counts are different units; the 48 added fault scenarios are contained
inside the existing `qengine_faults` selector, not 48 additional CTest cases.

| Execution | Passed CTest selectors | Explicit skips | Total registered |
| --- | ---: | ---: | ---: |
| Linux core, GNU 15.2 | 150 | 0 | 150 |
| Windows core, MSVC 19.52 | 147 | 0 | 147 |
| macOS ARM core, AppleClang 21 | 141 | 6 | 147 |
| Linux Address/UndefinedBehavior sanitizers | 150 | 0 | 150 |
| Linux VST3 | 138 | 0 | 138 |
| Windows VST3 | 134 | 0 | 134 |
| macOS ARM VST3 | 125 | 9 | 134 |

The six macOS core and nine VST3 skips are explicitly unsupported x86 MXCSR
FTZ/DAZ modes, with names retained in the native logs. None of the five Q6 engine
selectors is skipped. Linux-only module-coexistence checks are not represented
as Windows/macOS executions. Each platform's three complete experimental bundles
passed the archive checks and uploaded successfully. Downloadable Actions
artifacts have the configured seven-day retention; retained textual evidence
does not itself preserve those remote binary artifacts forever.

Native Python 3.11 discovers 584 tests per OS; the logs retain 20 Linux, 23
Windows and 21 macOS skips. The separate local Python 3.12.14 run, with NumPy
2.3.5 and SciPy 1.17.0, reports **583 unskipped passes and one skip**, exit 0,
in 122.505 seconds of unittest time. That one test requires
`RHODES_AUDIO_PROBE` for three small production-binary filtered comparisons;
it was unset. All 544 declared inputs remained unchanged. This is not another
full audio-reference or timing study.

The native workflows record the exact configure/build/test argument vectors.
The optional instrument uses Release builds with testing enabled, the pinned
SDK, and VSTGUI, examples and automatic plug-in links disabled. macOS uses
Unix Makefiles rather than the SDK's Xcode-specific fast-math/contraction flags.
The Clang-only floating-point guards passed the recorded native tests; explicit
`-ffp-contract=fast` or arbitrary alternate build flags are outside that result.
The available current compiler/runner versions are observations, not minimum
supported operating-system promises. SDK-off remains the default core build.

## Bounds, numerical identity and interpretation

The core event API permits at most 4,096 frames and 256 events; the host adapter
has a separate 8,192-frame bound, at most two parameter queues with 8,192 raw
points each, and 256 raw notes. Supported rejected-output cleanup visits at most
one output bus, two channels and 8,192 frames per channel. Unsupported dimensions
leave output declarations untouched. State publication uses bounded CAS attempts
with no audio retry loop; UI recall coalesces to the latest unconsumed valid
configuration. A lifecycle reset or valid recall installed at an accepted audio
boundary clears the fault latch and resets the required history.

Event/state tests use exact status, byte, ownership, cursor, mechanical-state and
continuation comparisons. Scalar-output tests compare both channels against the
documented conversion. The new Q6 fault fixture preserves the earlier 84 scenarios
and adds 48, with 12 explicit injection calibrations. Its local temporary mutant
compiled and then failed with `wrapper advanced after failure`; production source
was not replaced by that mutant. No tolerance was loosened to obtain those passes.

The separate [batch compatibility result](batched-quadrature-compatibility-20261124.json)
retains all 2,097 Q6 cases and 24 direct/filtered WAV identities in the recorded
Linux/GCC run. Its previously computed independent-reference conclusions remain
separate from byte replay and native host validation. Output compatibility does
not establish measured Rhodes fidelity or complete RT-001 for every retained
audio model.

## Integration decision

Register this criterion review and its frozen definition/source records as
documentary evidence, together with the current native numerical record. After
independent final review, complete HOST-001 with its existing integrator claim
and those evidence IDs. All required clauses pass without changing their text
or adding unrelated release conditions. The registry validates declared metadata
and hashes; the integrator remains responsible for criterion relevance.

Keep PHY-002 and RT-001 incomplete with the concrete next experiments in the
[current research-gate review](research-gates-20261124.md). Keep SPEC-001 and
DATA-001 blocked and fidelity `unvalidated`. No release is published by this
acceptance decision.
