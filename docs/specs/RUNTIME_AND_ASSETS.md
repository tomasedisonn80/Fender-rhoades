# Runtime, host events and prepared assets

**Status: implemented synthetic host foundation, with open runtime and prepared
asset requirements.** The current fixed-storage engine is wrapped by bounded
sample-offset events, MIDI ownership, versioned configuration state and three
optional VST3 instruments. A generalized prepared-asset loader and ringing-state
checkpoint are not implemented. The [original host contract](HOST_PROTOTYPE_20261123.md),
[MIDI continuation](HOST_MIDI_20261124.md) and current
[Expression/mono contract](HOST_EXPRESSION_MONO_20261124.md) define the adapter's
successive behavior. [VST3.md](../VST3.md) records build instructions and actual
verification; the [Expression verification guide](../EXPRESSION_VERIFICATION.md)
maps current fixtures and remaining acceptance work. The separate
[contact reference](../CONTACT_REFERENCE.md)
remains offline, with its own [frozen acceptance](CONTACT_ACCEPTANCE_20261123.md).

This specification began with setup reviews A031–A039; their historical
observations remain in [the setup campaign](../agents/CAMPAIGN.md). The sections
below distinguish implemented behavior from future requirements without changing
RT-001's numerical-error, convergence or measured-performance gates.

## Current implementation and evidence boundary

The synthetic audio engine retains 73 physical keys with three modes each.
Preparation warms both free/damped transition matrices; ordinary note, pedal and
reset operations select those prepared coefficients. [A041](../agents/development-20261123/W3-A041.md)
records the implementation and matched observations. An arbitrary third decay
requested through the modal primitive can still recalculate coefficients; the
two-entry cache is not a general prepared-asset system.

Plugin-owned processing uses fixed storage, without asset parsing, logging,
I/O or locks. [A024](../agents/development-20261123/W2-A024.md) observed zero C++
allocation attempts in 89 actual processor calls using calibrated interception.
This finite observation and source inspection do not prove every library or
foreign host getter allocation-free. Three [benchmark boundaries](../BENCHMARKING.md)
are now available: raw engine processing, complete `EventProcessor` calls, and
actual `RhodesProcessor` callbacks. Their costs must not be conflated.

One audio owner mutates mechanics and note ownership. Construction and preparation
occur outside processing. Direct concurrent mutation of `ResearchEngine` remains
unsupported; UI state callbacks instead exchange coherent configuration through
the lock-free mailbox. [A049](../agents/development-20261123/W3-A049.md) tests that
mailbox, and [A081](../agents/development-20261123/W5-A081.md) exercises concurrent
state callbacks against the actual processor. Lifecycle operations are outside
that concurrent-use contract.

The underlying engine's process call still returns Boolean success, while
`EventProcessor` returns bounded `ProcessResult` status, completed frames,
failing sample/event, subsystem and reset requirement. Retained
[event tests](../../tests/cpp/event_processor_test.cpp), reported by
[A015](../agents/development-20261123/W1-A015.md), inject sample and event failures
through a compile-time substitute engine and verify prefix preservation, suffix
silence and explicit recovery. This exercises adapter semantics without adding
a production fault switch or claiming exhaustive physical arithmetic coverage.

The optional instruments retain separate class identities and saved-state model
numbers. Their engines, electrical constants and digital monitoring controls
remain synthetic; the host extensions do not calibrate a physical instrument.

| Instrument | State model | Audio path | Reported latency |
| --- | ---: | --- | ---: |
| Rhodes Research Prototype | 1 | Direct synthetic source | 0 frames |
| Rhodes Research Filtered | 2 | Sampled internal-rate source and fixed FIR | 48 frames |
| Rhodes Research Quadrature | 3 | Six prepared observations per host interval and fixed convolution | 48 frames |

All three have no audio inputs and one output bus, stereo by default, with
suspended negotiation of standard mono or stereo. Mono emits the existing
scalar signal; stereo duplicates it. Negotiation updates bus name/count
coherently and unsupported requests retain the last accepted arrangement.
Layout does not alter model identity, latency, tail reporting or saved
instrument configuration. The current factory category is `Instrument|Synth`.
The [recorded metadata correction](../validation/host-expression-category-correction-20261124.json)
removes the obsolete stereo-only classification; it is a separate Linux
observation, not native-DAW acceptance.

## Current RT-001 handoff

The synthetic full-call harnesses now include their declared event/control work
and report observed maxima and nominal block-period misses. Retain the raw data,
source/build identity, workload and numerical-reference tolerances with each
result. The process-only benchmark remains a separately named boundary. Neither
callable harness measures host wake-up lateness, device underruns or released
plugin behavior. The [historical raw archive](../validation/runtime-historical-raw-20261123.json)
preserves the A038/A041 observations; the
[actual VST3 matrix](../validation/vst3-callback-benchmark-20261123.json) is a
separate experiment.

RT-001 still requires both performance and numerical evidence, including the
allocation/locking policy and timestep/antialiasing convergence in its acceptance
criteria. Timing JSON or a smoke test alone cannot complete it. The integrator
must record exactly which synthetic workload and approximations passed. The
separate HOST-001 prototype task does not waive runtime or physical-model gates;
native host/device and release claims require their own actual evidence.

## Bounded event adapter

The implemented physical `EventProcessor` accepts at most **4096 frames and 256
ordered events per call**, with whole-block preflight. The VST3 adapter accepts
up to **8192 host frames**, **256 input events** including validated pressure,
**19 distinct parameter queues**, **8192 raw points each for gain, Expression
and sustain**, and **256 raw CC123 points in total** across sixteen channel
commands. Duplicate offsets still consume their raw-point budget. It preflights
the entire host block before issuing bounded one-sample physical dispatches.
Translation stores at most **585 physical note actions per block**, with a
separate **256-action limit per sample including sustain**. These are
storage/processing limits, not measured deadline guarantees. Any revised limits
or future solver/coupling caps need
updated tests and timing evidence; details remain in the current host contracts.

Events have integer offsets `0 <= offset < N` and an explicit producer sequence.
Preserve `(offset, sequence)` order; do not sort equal-time releases/retriggers by
event type. Apply events before observing the sample at that offset. Offset `N`
belongs to the following block; an empty block accepts no events and advances
nothing. The adapter maintains an overflow-checked absolute frame cursor; it has
no future-event queue. Callers retain events belonging to later blocks. Any
future queue must have an explicit bound and remain outside physical mechanics.

Reject invalid order, late events, offsets or event overflow before mutating
mechanics. Return a distinct input-failure status and zero rejected output; never
silently truncate releases. Arithmetic faults differ: the current core may have
advanced part of the state and zeros the failed sample/remainder. A host must
stop using that trajectory and follow explicit reset/recovery before resuming.

The retained `scalar_partitions` fixture spans 130 frames at 48 kHz: key 17 on
at 0; off then on at 63; pedal-down then release at 64; pedal-up at 128. It compares
an independent direct-call schedule and final states for partitions `[130]`,
`[64,64,2]` and single frames. `equal_offset_order` reverses the tie at 63 and
verifies the observable difference. These checks establish the stated event
semantics within the synthetic engine.

## Musical release and overlapping inputs

Each physical key retains one held flag and one mechanical state. `NoteTracker`
translates up to 256 fixed holders across sixteen MIDI channels into that state
for pitches 28–100. Identified notes use channel/note ID; the `-1` sentinel has
distinct anonymous holders. Every accepted positive on strikes without resetting
motion, and only removal of the last owner emits physical release. Zero-intensity
note-on follows note-off semantics; valid pitches outside the instrument range
are ignored. The [independent note model](../agents/development-20261123/W4-A074.md)
tests ownership, capacity, recall and randomized sequences.

Core all-notes-off clears held flags while respecting sustain; hard reset clears
mechanics, held flags and pedal state. The VST3 adapter maps CC64 to shared
sustain, CC11 to global Expression, and CC123 to sixteen channel-specific release
commands. CC120 remains unmapped. Each accepted CC123 point removes that
channel's holders while retaining other holders, sustain, motion and filter
tails. At an equal offset, sustain changes precede ascending-channel releases,
then note events keep their input-list order. Pressure is a validated no-op;
it consumes event capacity without changing synthesis or ownership. Lifecycle
reset and successful recall clear ownership/mechanics and reinstall the preserved
or recalled configuration. These mappings are explicitly defined host behavior.

Test pedal lift with one physically held and one released key. Check all modal
coordinates across events. Reset during held/pedal activity and compare the next
strike/release against a fresh engine, detecting stale control state. Opposite
phase impulses must account for `J*v_before+J^2/(2*m)`; a fixed per-strike energy
increment is incorrect. Finite-hammer relaunch follows the separate contact spec.

## Live parameters and structural changes

The current persistent configuration contains Monitoring Gain (ID 0, normalized
`0..1`, default `0.5`), Sustain (ID 1, default off), and Expression (ID 2,
normalized/displayed factor `0..1x`, default `1x`). The sixteen CC123 parameters
at IDs `0x1000..0x100f` are transient commands. CC11 on any input channel addresses
the same global Expression control; the adapter does not reconstruct 14-bit
CC11/CC43 pairs. Expression multiplies digital output independently of Monitoring
Gain and does not change velocity, mechanical parameters or historical circuitry.

Gain and Expression interpolate independently from the audio owner's current
canonical live value at offset `-1`, after any recall selected for the block.
A mailbox snapshot may contain a newer UI recall and is not that anchor. Distinct
points are canonical binary32 anchors; the last point at an equal offset wins,
and the last value is held. Output uses binary64 interpolation; published
configuration uses canonical binary32 values. Partition tests must deliver the
same curve, including needed boundary anchors. Whole-block queue/event/ownership
preflight precedes mutation; rejection preserves a pending valid recall as well
as live configuration, mechanics and ownership.

The output calculation first preserves and checks
`monitored = (32 * gain) * volts`, then applies Expression before host-format
conversion. Exact unity returns `monitored` directly through the legacy arithmetic
path. Zero Expression continues mechanics, filtering, ownership and tails; it
does not hide engine or nonfinite-monitoring faults. A finite double attenuated
into the float32 range is valid, while failed final conversion still requires
reset. These checks also run when a valid inactive output bus is omitted. A
supplied inactive bus needs its negotiated-width pointer array but permits null
individual sample pointers; an active bus may not be omitted. The
[buffer rules](../VST3.md#output-layouts-and-buffer-handling) bound declared-width
rejection cleanup to one bus, one/two channels and 8192 frames, with null-safe
sample handling.

A zero-frame VST3 flush accepts only offset-zero parameter points and no input
events. It applies selected recall, then final gain/Expression/sustain and
staged channel releases, without advancing audio time. It can omit buffers or
supply a zero-channel or negotiated-width bus, and never accesses sample
pointers. This parameter-flush behavior is distinct from the physical
`EventProcessor`'s empty-block event rule.

Whitelist any live controls and specify their physical units, ramps, timing,
validity and input-work treatment. A smooth coordinate trajectory does not itself
make changes to mass, stiffness, magnetic geometry or circuit storage passive.
Moving a pickup can add explicit parameter-motion terms to the linkage derivative.

For now, structural/rate/normalization changes require deactivation, a successfully
validated replacement, and explicit reset. Failure retains the previous engine.
Future seamless replacement requires a state mapping, event boundary, old-asset
lifetime/reclamation rule and measured error/work bounds. Do not interpolate
transition matrices and assume the result is physically consistent.

## Compiled asset identity

A generalized asset boundary still needs an immutable `PreparedModel` and an
exclusive per-instance `EngineState` with declared vector dimensions and
73-pickup indexing. Bounded `EngineEvent` and `ProcessResult` types now exist;
their current synthetic contract is not an identified-model asset ABI. The
physical core must continue to depend on prepared data, not offline solvers,
file parsers or host SDK headers. Parsing and semantic validation stay outside
the callback.

The future loader must reject a candidate whose pickup-projection dimension
mismatches its modal state without replacing the valid model. The existing
`sample_fault_reset` fixture injects failure at sample 17 of a 64-sample block,
retains earlier output, zeros samples 17–63 and reports bounded reset-required
diagnostics. Reuse that recovery discipline when introducing prepared assets;
it does not validate a loader that is not yet implemented. These requirements
retain the purpose of [A040](../agents/reviews/A040.md).

An asset manifest records schema/model/ABI version, ID and revision, payload
encoding/size/hash, specimen/configuration hashes, netlist/output identity, source
records and rights, toolchain/compiler commit, solver settings and fit seed.
Record coordinates, SI units, transform Jacobians, modal normalization, coupling
and observation conventions, and whether turns are already included in linkage.

Domain metadata includes supported cells, holes, geometry/current/temperature
ranges and explicit failure behavior. Reduction evidence identifies reference
and configuration hashes, whole-condition holdouts, per-observable error budgets,
achieved worst errors and derivative/reciprocity results. Separate reduction,
reference numerical and acquisition errors.

Export with fixed ordering, encoding, numeric precision and byte order. Exclude
volatile timestamps from content identity. Verify actual payload bytes and all
semantic compatibility before atomically publishing immutable prepared state.
A changed payload byte and an intact asset for a different load must both reject
replacement without disturbing the active state. No callback parsing or fallback
substitution is permitted.

## Persistence and determinism

All three models use the same explicitly encoded **RHST configuration** with
their own model number. The [codec](../../include/rhodes/instrument_state.hpp)
appends `expression = 1.0F` after gain and sustain in `InstrumentState`, preserving
the meaning of existing two-field aggregate initializers. Its object layout is
not the wire format. Integer fields and binary32 bit patterns are little-endian:

| Byte offsets | Width | Version 1, exactly 16 bytes | Version 2, exactly 20 bytes |
| --- | ---: | --- | --- |
| 0–3 | 4 bytes | ASCII `RHST` | ASCII `RHST` |
| 4–5 | 2 bytes | Unsigned version `1` (`01 00`) | Unsigned version `2` (`02 00`) |
| 6–7 | 2 bytes | Unsigned model `1`, `2` or `3`; must equal the requested model | Same model rule |
| 8–11 | 4 bytes | Canonical normalized binary32 Monitoring Gain | Same gain field and offset |
| 12–15 | 4 bytes | Binary32 Sustain: canonical `+0` or `1` | Same sustain field and offset |
| 16–19 | 4 bytes | Absent; decode supplies Expression `1` | Canonical normalized binary32 Expression |

Gain and Expression accept nonnegative finite binary32 encodings from `+0`
through `1`, including positive subnormals, plus the `-0` encoding. In integer
terms, accepted bits are `0x00000000..0x3f800000` or `0x80000000`; decode,
publication and encoding canonicalize either signed zero to `0x00000000`.
Negative nonzero, above-unity, infinity and NaN encodings reject. Sustain decode
accepts only `0x00000000`, `0x80000000` and `0x3f800000`, mapping both zeros to
false; writes use `+0` or `1`. Bitwise validation preserves the wire domain under
host rounding and FTZ/DAZ modes instead of losing subnormals to arithmetic.

The explicit v1 encoder **rejects non-unity Expression**. The host writer chooses
v1 at canonical Expression `1` (`0x3f800000`) and preserves the exact legacy
bytes; it chooses v2 for every other valid value. Own-model v1 loads with
Expression `1`. A valid v2 packet with Expression `1` is accepted and subsequently
saves as v1. Older v1-only builds cannot read v2. Both versions reject incorrect
magic, version, model, control bits or length; all foreign-model recall directions
reject. A 20-byte v1 packet and a 16-byte v2 packet are invalid even though both
lengths exist in the codec.

The [stream adapter](../../plugins/vst3/state_stream.cpp) requires the remaining
extent from the current cursor to be exactly 16 or 20 bytes, then checks that it
matches the decoded version. It accepts successful positive partial transfers,
bounded by the selected packet's byte count; zero, negative, oversized or failed
transfers reject. There is **no stream truncation**: writing a 16-byte v1 packet
over a 20-byte v2 buffer can leave four stale bytes, so the caller must provide
a fresh or correctly truncated output extent. A subsequent read rejects that
stale 20-byte v1 extent. Stream I/O stays outside the audio callback.

Processor saving reads one coherent configuration snapshot from the lock-free
64-bit mailbox: gain occupies bits `0..31`, sustain bit `32`, pending-recall bit `33`,
and the valid thirty-bit Expression encoding bits `34..63`. Bounded single
compare/exchange attempts let newer UI recalls take precedence without retry
spins, locks or allocation. Invalid recall preserves an already pending valid
recall; rejected ownership preflight restores a selected recall only if a newer
UI write has not replaced it. The audio owner applies accepted recall at a
processing boundary and resets mechanical, note-ownership and filter state.

Configuration excludes the host's output layout, transient CC123 commands,
note ownership and all ringing/dynamic state. Successful recall is not a
ringing-state checkpoint. A future checkpoint additionally needs all dynamic
mechanics, contact, electrical, thermal and scheduler states, event position,
coordinate normalization,
sample rate and model identity. Do not serialize a C++ object memory layout.
Use explicit numeric encoding and independently versioned configuration/state
formats; validate before replacing a live instance.

Bitwise partition equivalence within the same build does not prove cross-platform
bit identity. Record compiler flags and floating-point environment; specify
tolerances for supported cross-build comparisons. The proposed continuation test
checkpoints after 4097 successful samples at 48 kHz, then compares 96000 further
samples/states with uninterrupted execution across block sizes 1, 64 and 257.

## Long tails and performance evidence

Zero computed energy can coexist with nonzero state after squaring underflows.
Do not use that value alone as a sleep predicate. Keep a gradual-underflow
reference and record fast-math, contraction, rounding and flush modes. Any
runtime flush/sleep approximation needs an output-error bound, phase-preserving
wake behavior and renewed analysis when coupling changes. Restore any caller
floating-point environment altered by an adapter.

Measure uninterrupted tails, not just repeated attacks and exact-zero silence.
Measure complete callback/event handling, wake-up lateness, completion deadlines
and device underruns separately. The retained benchmark matrices already exercise
all 73 keys at 192 kHz/64 frames with their declared synthetic event workloads.
Longer uninterrupted tails, future solver/coupling budgets and device behavior
still require their own experiments. Continue to include cap exhaustion and
one-event overflow when limits evolve. Observed maxima and zero observed misses
are finite observations, not worst-case guarantees or CPU-utilization percentages.
