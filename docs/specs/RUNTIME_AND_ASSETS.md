# Runtime, host events and prepared assets

**Status: a future integration contract.** The current fixed-storage engine has
call-boundary controls, an arbitrary caller-owned span and a sample-rate-only
`prepare`. It has no host event list, asset loader, state serializer or plugin.
This specification consolidates setup reviews A031–A039; those reports remain
available through [the campaign index](../agents/CAMPAIGN.md).

## Current verified boundary

Static inspection finds fixed 73-key/three-mode storage and no core allocation,
locking, I/O or explicit throws. This is source evidence, not an allocator trap,
platform-library audit, or host deadline guarantee. The benchmark times only
`process()`. Note events recompute three modal transitions; pedal, reset and
all-notes-off can recompute 219; setup also calculates key frequencies. Those
control costs must be included in future full-callback measurements.

One execution owner mutates each engine. Construction, asset parsing and
preparation occur outside audio processing. No concurrent UI/control mutation is
supported by the current unsynchronized state. Diagnostics use bounded status and
counters; logging and presentation occur outside the callback.

## Bounded event adapter

Before adding a host, freeze maximum frames/events, all solver and coupling caps,
and the supported parameter domain. An initial engineering proposal is up to
4096 frames and 256 ordered events per adapter call; this is not implemented or
a measured deadline guarantee. Larger host buffers require a specified bounded
segmentation policy. Any revised limits need updated tests and timing evidence.

Events have integer offsets `0 <= offset < N` and an explicit producer sequence.
Preserve `(offset, sequence)` order; do not sort equal-time releases/retriggers by
event type. Apply events before observing the sample at that offset. Offset `N`
belongs to the following block; an empty block accepts no events and advances
nothing. Keep an overflow-checked absolute frame cursor and a bounded future-event
queue outside the physical core.

Reject invalid order, late events, offsets or event overflow before mutating
mechanics. Return a distinct input-failure status and zero rejected output; never
silently truncate releases. Arithmetic faults differ: the current core may have
advanced part of the state and zeros the failed sample/remainder. A host must
stop using that trajectory and follow explicit reset/recovery before resuming.

An independent event fixture spans 130 frames at 48 kHz: key 17 on at 0; off then
on at 63; pedal-down then release at 64; pedal-up at 128. Compare an independent
scalar trajectory and final states for partitions `[130]`, `[64,64,2]` and single
frames. Reverse the tie at 63 to prove ordering is observable. This is a proposed
fixture, not an already supported event API.

## Musical release and overlapping inputs

The current key has one held boolean. Zero-intensity note-on releases it;
all-notes-off clears held keys while respecting sustain; hard reset clears all
mechanics, held flags and pedal state. A future host must name its overlapping
note/channel policy explicitly and test it. Multiple input sources must still
drive one physical state per key. A controller-message mapping must not assume
that an internal hard reset preserves external controller state.

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

The future boundary needs four explicit types: immutable `PreparedModel`,
exclusive per-instance `EngineState`, bounded `Event` data, and a fixed-size
`ProcessResult`. Declare all vector dimensions and 73-pickup indexing. The result
reports status, first failing sample, subsystem and whether reset is required;
the host formats it later. Runtime depends on this prepared-data interface, not
offline solvers, file parsers or a host SDK. These are future types, not current
API exports.

Reject a candidate whose pickup-projection dimension mismatches its modal state
without replacing the valid model. A future fault-injection fixture at sample
17 of a 64-sample block must retain earlier output, zero samples 17–63 and return
bounded reset-required diagnostics without callback logging or allocation. This
consolidates [A040](../agents/reviews/A040.md) into this single boundary document.

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

Session recall restores configuration with a declared reset/equilibrium policy.
A ringing-state checkpoint additionally needs all dynamic mechanics, contact,
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
and device underruns separately. A future stress fixture can use all 73 keys at
192 kHz/64 frames with the full declared event and solver budgets. Include cap
exhaustion and one-event overflow. Observed maxima and zero observed misses are
finite observations, not worst-case guarantees or CPU-utilization percentages.
