# Expression and mono output continuation

Status: reviewed implementation contract. Baseline:
`6c91dab00855b67a559e92edb79ae49e567be469`. This is an engineering extension
of the three existing synthetic instruments, not a physical calibration.

## User behavior and compatibility

Add a global digital **Expression** multiplier, normalized 0..1 and initially
1, independent of Monitoring Gain. Map MIDI CC11 on each of the sixteen input
channels to this same control. This instrument has shared mechanics and one
shared output; expression is intentionally global, not a multitimbral channel
mixer. The host supplies normalized parameter values. No raw CC43 pairing or
14-bit reconstruction is performed by the plug-in.

Reserve parameter ID 2 and retain gain/sustain IDs 0/1, the sixteen CC123 IDs,
all six class identities, model numbers 1/2/3 and latencies 0/48/48. Expose
Expression as an automatable continuous control with factor range 0..1 and
unit `x`. Its text conversion uses the existing strict bounded ASCII decimal
parser and canonical binary32 round trips. Accept surrounding ASCII whitespace,
the existing signed decimal/exponent grammar and an optional lowercase `x`
suffix; enforce the parsed finite binary64 domain 0..1 before canonicalization.
Commands retain their binary64 controls and do not accept a unit suffix.

Expression changes the digital output only. Zero expression continues note
ownership, mechanical evolution, filtering and release tails. It does not mute
by clearing state, alter velocity or claim an authentic historical volume
circuit. Preserve the old calculation
`monitored = (32 * gain) * volts`; check it for nonfinite values before applying
expression. At exact unity return the monitored value directly, retaining the
legacy arithmetic path and stereo audio bytes.
Engine failure or nonfinite monitored output always faults, including at zero
expression. Apply expression before the final host-format conversion: reducing
a finite double into the float output range is valid, not a hidden fault. Check
the final conversion even when inactive output buffers are omitted.

## Automation and transactions

Use fixed instance-owned expression point storage with the existing 8192-point
per-control bound. Permit nineteen distinct parameter queues. CC123's duplicate
tracking must start after the three persistent parameter IDs. Retain the 256
command/event budgets and existing physical-action expansion limit.

Expression follows the same explicit interpolation convention as Monitoring
Gain: the audio owner's current canonical live value, after any selected recall,
is the offset -1 anchor. Do not use the mailbox snapshot, which can already hold
a newer UI recall. Distinct points
are canonical binary32 anchors, equal offsets keep the final point, and values
after the last point are held. Gain and expression interpolate independently.
Output uses the double interpolation values; saved configuration uses their
canonical binary32 values. Tests of block partitioning must deliver equivalent
piecewise-linear curves, including necessary boundary anchors.

Validate every queue, point, event and ownership operation before live state
mutation. Invalid expression, a duplicate queue or a late malformed point must
preserve mechanics, ownership, pending recall and published configuration.
Accepted arithmetic faults retain the existing reset-required policy. A
zero-frame flush accepts offset-zero parameters, applies recall then the final
gain/expression/sustain values and releases, and advances no audio time.

## Saved configuration and publication

Append `expression = 1.0F` to `InstrumentState` so existing two-field aggregate
initializers retain their meaning. Validate and canonicalize expression with
the same bitwise normalized-binary32 rules as gain, including signed zero and
subnormal values under host floating-point modes.

Preserve the existing 16-byte RHST version-1 codec and its exact bytes for unity
expression. Its encoder must reject non-unity expression rather than silently
discarding it. Add an explicit 20-byte version-2 encoding: the first sixteen
bytes have the same fields and offsets as v1 except the version is 2; bytes
16..19 contain little-endian canonical binary32 expression. The decoder accepts
only the exact length for each version and the requested model. A v1 decode
sets expression to 1. The host writer selects v1 at canonical unity, v2 otherwise;
a valid v2 unity input may be saved canonically as v1.

Keep one lock-free 64-bit mailbox word. Retain gain bits 0..31, sustain bit 32
and pending bit 33. Expression's valid nonnegative binary32 encoding fits in
thirty bits and occupies bits 34..63. The compare/exchange protocol remains
bounded, with no spin, lock, allocation or audio-thread stream access. Exercise
whole-configuration coherence, newer UI recall precedence, aborted preflight,
signed-zero canonicalization and all binary32 edge regions.

## Mono and stereo output

The single audio output defaults to stereo and may negotiate either standard
mono or stereo with no audio inputs. Change the SDK bus arrangement and its
reported name/channel count coherently. Reject unsupported layouts without
changing the previous arrangement. Negotiate in the host's suspended state;
this feature does not introduce a live audio-thread layout switch.

Mono emits exactly the existing scalar left-channel signal. Stereo continues
to duplicate it; do not sum stereo channels or apply a new pan-law gain. Validate
nonzero blocks against the negotiated channel count and iterate only the one
or two declared sample-pointer entries. A supplied inactive bus still needs its
channel-pointer array; its individual sample pointers may be null. Also accept
omission of the single inactive output (`numOutputs = 0`) during nonzero
processing, as permitted by the SDK. Continue mechanics, events, automation and
fault handling while touching no output pointer or silence flag. Reject an
omitted active output. This deliberately extends the old omission rejection.
Preserve bounded rejection cleanup, including null sample pointers. Zero
frames may omit output buffers or carry a zero-channel or negotiated-width bus;
they never access sample pointers. Silence flags reflect the actual one- or
two-channel buffer width. Layout selection is host configuration, not saved
instrument configuration, and does not change latency or tail reporting.

Rejection cleanup recognizes a declared width of one or two independently of
the negotiated layout, bounded by one bus and 8192 frames. It may clear a
supported-width buffer for a rejected layout mismatch; unsupported dimensions
remain untouched. This is a deliberate extension of the old stereo-only cleanup
contract, not permission to traverse arbitrary host-provided channel counts.

## Acceptance review

1. Independent scalar output/automation checks cover expression's default,
   extrema, interpolation, duplicate offsets, combined controls, note/release
   events, zero-frame flushes and partitioned curves across all variants and
   both host sample formats. Expression at zero preserves continued synthesis.
2. State migration, exact legacy-unity bytes, malformed-length/version/model
   rejection, bounded stream transfers and coherent lock-free publication pass
   independent fixtures, including relevant floating-point modes.
3. Mono/stereo metadata, exact scalar audio, output pointer bounds, omitted
   inactive outputs, silence masks, rejected negotiation/blocks and lifecycle transitions pass direct
   and actual loaded-module tests without changing existing stereo behavior.
4. The affected core/host suite and all three SDK/module validators pass for
   the executed source on Linux, Windows and macOS. Retain exact platform
   counts/skips and source binding. Replay legacy unity audio. Report callback
   allocation/locking observations without inventing a CPU or device guarantee.
5. Update controls, state format, output layout, build and named-DAW verification
   documentation; retain exact outcomes and unresolved limits. HOST-002,
   contact/runtime research and measured fidelity have separate decisions.

## Public primary references

- [MIDI Association control-change table](https://midi.org/midi-1-0-control-change-messages)
  assigns controller 11 to Expression. The global mapping and digital response
  above are explicit project choices, not an assertion of General MIDI behavior.
- [Steinberg MIDI translation](https://steinbergmedia.github.io/vst3_dev_portal/pages/Technical%2BDocumentation/About%2BMIDI/Index.html)
  and [IMidiMapping](https://steinbergmedia.github.io/vst3_doc/vstinterfaces/classSteinberg_1_1Vst_1_1IMidiMapping.html)
  describe host controller-to-parameter delivery.
- [Steinberg audio processor API](https://steinbergmedia.github.io/vst3_doc/vstinterfaces/classSteinberg_1_1Vst_1_1IAudioProcessor.html)
  requires accepted arrangements to be reflected in the plug-in's buses.
- [Bus arrangement call sequence](https://steinbergmedia.github.io/vst3_dev_portal/pages/Technical%2BDocumentation/Workflow%2BDiagrams/Bus%2BArrangement%2BSetting%2BSequence.html)
  places negotiation between suspension and reactivation.
- [Steinberg processing FAQ](https://steinbergmedia.github.io/vst3_dev_portal/pages/FAQ/Processing.html)
  documents parameter-only process calls without audio buffers.

Implementation compatibility is checked against the repository's pinned public
Steinberg SDK. These references provide host semantics, not specimen data.
