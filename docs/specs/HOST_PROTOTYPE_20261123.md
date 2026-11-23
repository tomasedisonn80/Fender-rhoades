# Experimental VST3 instrument contract

This is host engineering for the synthetic `ResearchEngine`, independent of
measured physical-model acceptance. The contact reference is offline and is not
silently substituted into the plug-in's real-time audio path. SDK provenance is
recorded in [DEPENDENCIES.md](../DEPENDENCIES.md).

## Audio and parameters

Provide one MIDI event input, no audio input, and one stereo output carrying the
same synthetic mono signal on both channels. Support 32-bit and 64-bit samples,
32–192 kHz, and up to 8192 frames per host call. Reject unsupported setup before
replacing a working configuration. Advertise zero latency; no measured CPU or
audio-device deadline guarantee is implied. No custom editor is required: expose
ordinary host controls.

Parameter ID `0` is normalized monitoring gain, finite in `[0,1]`, default `0.5`.
Digital output is `32 * normalized_gain * synthetic_harp_volts`. The gain is an
explicit monitoring conversion, not a calibrated amplifier. Parameter ID `1` is
the common sustain pedal, down at normalized values at least `0.5`. The public
controller maps MIDI CC64 to this parameter on all sixteen channels. There is no
pitch bend, note expression, custom temperament, physical half-pedal, effects or
extra historical circuit emulation in this version.

Quantize each valid gain point to binary32. Interpolate monitoring-gain automation
linearly from the previous block's final value at local sample `-1` to the first
point, then between points; keep the final point constant afterward. Duplicate
offsets within one queue use the last point, without division by zero. A point at
offset zero is applied directly to sample zero. Sustain points act as
steps at their offsets. Across separate host parameter and note streams, apply
parameter changes first at an equal offset, then notes in original list order.
This is a declared tie policy because the SDK supplies no shared producer order.
The physical event API separately preserves its explicit `(offset, sequence)`
ordering. Parameter-only zero-frame flushes accept offset-zero points only, use
the last point in each queue, and change configuration without advancing
mechanics; note events in such a flush are rejected.

## MIDI ownership and bounds

Adopt [A010's ownership contract](../agents/development-20261123/W1-A010.md):
MIDI 28–100, one persistent state per key, sixteen merged channels, 256 fixed
holders, exact identified-note ownership, and distinct anonymous holders for
the `-1` sentinel. Retrigger retains modal state. Only the last owner's release
releases a key. Valid pitches outside the instrument range are ignored. Ordinary
release never performs a hard reset.

The first adapter maps CC64 only. Channel CC123 and CC120 mappings are not
advertised; lifecycle reset and successful state recall clear ownership and
mechanics. The note-tracking helper may support channel release independently
for future adapters. No unsupported MIDI event type is reinterpreted as a note.

Preflight all bounded host input before mutating audio state: at most 256 input
events, two parameter queues, and at most 8192 points per queue. Validate every
read, count, domain, offset, queue identity and ordering. Parameter points may be
dense while mechanical events are dispatched in bounded groups. An individual
sample may contain at most 256 combined physical note/pedal events. Reject any
translated overflow without truncating releases. All fixed arrays are owned by
the instance or by bounded stack storage; no callback allocation, locking, I/O,
serialization or logging.

The physical event limit is per `EventProcessor::process` call; the host adapter
may issue one such call per sample after preflighting the entire host block.
Dense host automation does not need to fit a single 256-event array for all
8192 samples. MIDI-to-key translation retains a separate 256-event cap.

The initial 4096-frame host proposal was expanded after the unmodified pinned
SDK validator requested 8192-frame setup and processing. The fixed host arrays
and parameter-point caps expand accordingly; physical event invocations keep
their original 4096-frame maximum and use one-sample dispatch here. This is a
host-compatibility change, not a change to contact tolerances or fidelity gates.

Invalid host input returns failure and silence, preserving active configuration,
ownership and mechanics. An arithmetic failure preserves successful prior
samples, silences the failed sample and remainder, and latches reset-required.
The adapter must check finite output conversion rather than silently emitting
NaN/Inf. Oversized rejected buffers require clearing the supplied output and
therefore have clearing cost proportional to that invalid buffer size; accepted
processing bounds do not imply a bound for arbitrary malformed caller storage.

## State and threading

Adopt the exact sixteen-byte v1 encoding and bounded stream-transfer checks in
[A011](../agents/development-20261123/W1-A011.md). Model ID `1` means the synthetic
ResearchEngine; it cannot load future identified assets under the same semantics.
Gain is quantized to binary32, sustain to exact zero/one, and negative zero is
canonicalized. Invalid, short, extended or incompatible chunks preserve state.

Recall is configuration plus explicit reset, not a ringing-state checkpoint.
Clear note ownership, mechanical state, fault/cursor state and interpolation
history, then install the saved gain and sustain. Old unknown note identities
are harmless; a host reusing an identical identity creates an ambiguity that
this first adapter cannot distinguish from a new matching release.

Host UI-thread state callbacks may run during audio processing. Use A011's
lock-free packed mailbox and single-attempt compare/exchange protocol. Only the
audio owner changes mechanics. UI recalls coalesce to the latest unconsumed
configuration; concurrent automation must not overwrite a newer pending recall.
Saving reads one coherent snapshot without resetting audio. No unbounded retry
loop is permitted in the callback.

Validate host syntax and buffers first. Consume a recall token before ownership
preflight so the staged holder table reflects the requested reset. If ownership
or translated-event validation rejects, restore that pending token with one
compare/exchange only when no newer UI write exists; mutate no mechanics. Apply
the actual recall only after all preflight succeeds. This preserves rejected
blocks and concurrent UI requests without an audio retry loop.

## Required checks and reported boundary

Verify the independent scalar event trajectory, block partitions, ordered ties,
whole-block rejection, injected sample/event faults, note overlap/capacity,
portable codec, stream short transfers, lifecycle/recall, dense automation and
allocation policy. Build the actual VST3 and run the official pinned SDK
validator. Add a small real processor/controller host harness for behavior the
generic validator does not inspect.

Record actual compiler/platform and binary identity. A source configuration for
another platform is not a tested build. Preserve the SDK notices in the bundle.
The first instrument remains a synthetic, uncalibrated engineering prototype
with the current impulse excitation, switched-loss release and nonlinear
aliasing limits. Physical acquisition and the broader RT-001 gate stay separate.
