# Runtime, host events and prepared assets

**Status: implemented synthetic host foundation, with open runtime and prepared
asset requirements.** The current fixed-storage engine is wrapped by bounded
sample-offset events, MIDI ownership, versioned configuration state and an
optional VST3 instrument. A generalized prepared-asset loader and ringing-state
checkpoint are not implemented. The [host contract](HOST_PROTOTYPE_20261123.md)
defines the implemented adapter; [VST3.md](../VST3.md) records build instructions
and actual verification. The separate [contact reference](../CONTACT_REFERENCE.md)
remains offline, with its own [frozen acceptance](CONTACT_ACCEPTANCE_20261123.md).

This specification began with setup reviews A031–A039; their historical
observations remain in [the setup campaign](../agents/CAMPAIGN.md). The sections
below distinguish implemented behavior from future requirements without changing
RT-001's numerical-error, convergence or measured-performance gates.

## Current verified boundary

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
up to **8192 host frames**, 256 raw note events, two parameter queues and 8192
points per queue; it preflights the entire host block before issuing bounded
one-sample physical dispatches. At most 256 combined physical note/pedal events
may occur at one sample. These are storage/processing limits, not measured
deadline guarantees. Any revised limits or future solver/coupling caps need
updated tests and timing evidence; details remain in the host contract.

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
mechanics, held flags and pedal state. The VST3 adapter maps CC64, not CC120 or
CC123. Its lifecycle reset and successful state recall clear ownership/mechanics
and reinstall the preserved or recalled configuration. Do not infer an external
controller-message mapping from an internal helper operation.

Test pedal lift with one physically held and one released key. Check all modal
coordinates across events. Reset during held/pedal activity and compare the next
strike/release against a fresh engine, detecting stale control state. Opposite
phase impulses must account for `J*v_before+J^2/(2*m)`; a fixed per-strike energy
increment is incorrect. Finite-hammer relaunch follows the separate contact spec.

## Live parameters and structural changes

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

The current host's sixteen-byte version-1/model-1 state restores monitoring gain
and sustain with an explicit mechanical/ownership reset. Its validated numeric
encoding and bounded stream handling are specified in the host contract;
successful recall is not a ringing-state checkpoint. A future checkpoint
additionally needs all dynamic mechanics, contact,
electrical, thermal and scheduler states, event position, coordinate normalization,
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
