# Manual native-DAW evaluation

**This is a proposed protocol, not an executed DAW result.** It is intended to
turn a trial of the three experimental instruments into a reproducible host
observation. Existing [native CI results](validation/native-platforms-20261124.md)
cover the stated harnesses and validators at their recorded source revisions;
they predate the [MIDI continuation](specs/HOST_MIDI_20261124.md) and do not
establish native acceptance of its changes. The later
[Expression and mono contract](specs/HOST_EXPRESSION_MONO_20261124.md) defines
the current additional cases below. Its
[build and focused-check instructions](VST3.md#select-expression-and-output-layout-checks),
[state and rollback guide](VST3.md#saving-and-resetting-state), and
[source-bound verification status](VST3.md#current-expression-and-output-layout-verification--24-november-2026)
are separate references; a recipe or native VST3 harness pass is not a named-DAW
pass. Every proposed case here remains **Not run**. A future session using this
guide covers its named DAW, machine, settings and build only; it does not establish
measured Rhodes fidelity, complete RT-001, or qualify a release.

## 1. Identify the build and session

Obtain the complete bundles as described in [VST3 setup](VST3.md#find-and-inspect-the-bundle).
Record the full source commit, CI run/job and artifact name, OS/architecture,
archive SHA-256, and native module SHA-256 for each bundle. For a local build,
also retain the compiler, build flags, pinned SDK revisions and any source diff.
A displayed plug-in version alone does not identify the build. Record the
loaded bundle path if the host exposes it, so an older installed copy is not
mistaken for the selected artifact.

For comparisons between builds, start a fresh DAW and any separate plug-in-host
process where possible, and record whether that restart occurred. A hash of the
file currently on disk does not identify bytes already loaded by a running host;
keep uncertainty about the loaded module explicit when the host cannot show it.

Use a new disposable project. Record DAW name/version/build, OS version, CPU
architecture, audio interface and driver/version, sample rate, device buffer
request, host processing precision if exposed, delay-compensation settings,
and live/offline mode. Record the requested and accepted output arrangement,
bus activation, capture point, track pan law, channel mapping and any automatic
silent-track suspension. Export bit depth is not proof of plug-in processing
precision; device buffer size is not proof of each plug-in callback size.
Mark settings the host does not expose as unknown.

A useful first condition is **48 kHz, a requested 256-frame device buffer,
120 BPM**, with track/master effects, normalization and automatic gain changes
disabled. These are proposed reproducibility settings, not a recommended
performance limit. Keep an explicit render interval with leading silence and
a fixed tail; the plug-ins advertise an infinite asymptotic tail, so automatic
tail detection is not a consistent endpoint.

## 2. Discover and play each instrument

Scan the complete bundles using the host's ordinary procedure. Start each on its
own stereo instrument track, then test negotiated mono separately where offered.
Record the displayed name, available controls, factory category where exposed,
reported latency and any scan/load error. The current identities are defined in
[the implementation](../plugins/vst3/plugin_ids.hpp):

| Instrument | Bundle | State model | Reported latency |
| --- | --- | ---: | ---: |
| Rhodes Research Prototype | `RhodesResearch.vst3` | 1 | 0 frames |
| Rhodes Research Filtered | `RhodesResearchFiltered.vst3` | 2 | 48 frames |
| Rhodes Research Quadrature | `RhodesResearchQuadrature.vst3` | 3 | 48 frames |

Use the host's generic controls if it shows no custom editor. Monitoring Gain
defaults to **16x** and spans **0–32x**; Sustain is **Off/On**. **Expression** is
an automatable continuous multiplier, ID `2`, range **0–1x**, default **1x**.
Expect **19 host parameters**: those three controls plus `All Notes Off Ch 1`
through `All Notes Off Ch 16`. Gain and sustain retain IDs `0` and `1`; the channel
commands use `0x1000..0x100f`. Commands are writable and nonautomatable, with
numeric normalized values `0..1`, and are not hidden or read-only. A host may
omit them from its automation chooser while still exposing them in its generic
parameter view. Record what each view actually shows. If discovery still shows
the old two- or eighteen-parameter inventory, use the host's documented rescan/refresh
procedure and restart its plug-in-host process where available; retain the
before/after inventory and loaded-build identity rather than assuming a rescan
loaded the intended module.

The [factory](../plugins/vst3/factory.cpp) category is `Instrument|Synth`; that
category is not a channel-layout declaration. There is one audio output and no
audio input. Before host negotiation the default is `Stereo Out`, two channels;
an accepted standard mono arrangement reports `Mono Out`, one channel. Stereo
duplicates the same synthetic scalar signal; mono emits that scalar without
summing the stereo channels or applying a new pan-law gain. A host can negotiate
immediately on insertion, so an initial track display alone does not establish
the pre-negotiation default. Keep unavailable bus metadata unknown.

Select layouts through the host's documented reconfiguration while processing
is suspended. Capture at the plug-in output before host pan/fader/mixing where
possible. An unequal or rescaled post-routing capture can reflect host routing
or pan law; unavailable routing or boundary captures make exact plug-in scalar
comparisons **inconclusive**. Do not substitute a host stereo-to-mono downmix for
confirmed mono bus negotiation. The layout is host configuration, outside the
instrument state chunk, and does not change latency or tail reporting.

Create a short MIDI clip at a fixed nonzero velocity, recording the actual
events and channel assignments. Use MIDI pitch numbers because octave labels
differ between hosts. Repeat the following separately for all three variants;
their audio paths are different and are not expected to null against one another.

| Exercise | Procedure and observation | Result |
| --- | --- | --- |
| Range | Play pitches 28, 64 and 100 separately. Test 27 and 101 from a fresh, silent instance: valid pitches outside 28–100 should not excite a key. Do not mistake a preceding note's tail for an out-of-range response. | Not run |
| Retrigger and ordinary release | Strike 64, release it, then restrike while its tail remains. Capture the attack/release sequence and compare it with an isolated fresh strike. Release changes damping; it does not hard-reset the resonator. Listening is a coarse regression observation, not proof of hidden mechanical state. | Not run |
| Overlapping holders | At 1.0 s start pitch 64 on channel 1; at 1.5 s start it on channel 2. Release channel 1 at 2.0 s and channel 2 at 2.5 s. Compare with a repeat that delays only the first release to 2.5 s. With distinct holders and otherwise identical delivered events, the earlier release should not damp the still-held key. | Not run |
| Sustain | Compare an Off run with a run that sends CC64 down before note release and up later. Record whether the host maps CC64 to the Sustain parameter. Note release under pedal should retain the held damping regime until pedal-up; pedal-up need not produce immediate silence. | Not run |
| Gain | From the same reset/input conditions, compare steady 8x and 16x settings, then 0x, keeping Expression at 1x. Expect a factor of two before host clipping or other processing, and silence at zero gain. Retain unnormalized captures; gain is a monitoring conversion, not calibrated instrument loudness. | Not run |

A host can merge channels, rewrite note identities or chase notes during seek.
Retain its event display/export or trace when available. If it cannot deliver
distinct overlapping holders, mark that exercise **inconclusive**, rather than
claiming ownership coverage from a piano-roll drawing. The detailed bounds and
ordering rules remain in the [host contract](specs/HOST_PROTOTYPE_20261123.md)
and [MIDI continuation](specs/HOST_MIDI_20261124.md).

### MIDI continuation cases — not run

Repeat this small case set for each variant, using modest event counts and the
same reset, gain, render interval and routing for each paired comparison.
Check live MIDI input and recorded-clip playback separately where available.
Channels below use MIDI labels 1–16 (SDK indices 0–15). Leave sustain Off except
where stated, and keep Expression at `1x` for these MIDI continuation cases.
Keep ordinary note-offs later than the command under observation
so they cannot explain an apparent CC123 release. Compare with a reference clip
that replaces that command with ordinary note-offs for the same holders at the
same times; release increases damping and need not produce instant silence.

Separate **requested input**, **host delivery**, and **plug-in response** in the
session record. An upstream MIDI monitor or recorded/exported raw MIDI establishes
what reached that observation point, not what reached the processor. A controller
or generic-control UI value is also not a delivery trace. Where instrumentation
permits, retain the processor's actual `ProcessData::inputParameterChanges`:
command parameter IDs, normalized values, point counts and sample offsets, with
callback boundaries/frame counts. Retain the delivered note/event types,
channels and identities as well. Record the trace's observation point and limits;
an unchanged display cannot establish even the first zero command's arrival.
If delivery or callback grouping is unavailable, retain the audio/UI observation
but mark the corresponding delivery or exact-ordering claim **inconclusive**.

| Case | Procedure | Expected outcome if the stated input is delivered | Result |
| --- | --- | --- | --- |
| Parameter discovery | Record the generic parameter inventory, IDs/flags/defaults if exposed, and any rescan/restart used. | 19 parameters: gain `0`, sustain `1`, continuous automatable Expression `2` (default `1x`), and 16 writable, nonautomatable channel commands `0x1000..0x100f`. Unknown host metadata remains unknown. | Not run |
| Channel-specific CC123 | Hold pitch 64 on channel 1 and pitch 67 on channel 2; send CC123 only on channel 1, then on channel 2. | Each command removes only its channel's holders. The other channel remains held until its own release; existing mechanical motion and filter tails remain. | Not run |
| First CC123 at default zero | Use a fresh instance with the channel command at its default normalized value `0`; do not prime it with a nonzero command or a UI edit. Strike 64 on channel 1, then send the first CC123 with MIDI value `0`. Retain evidence of the default and separately of the incoming zero point. | The first delivered zero point releases the holder despite matching the parameter default. A value remaining at zero in the UI does not prove delivery or omission. | Not run |
| Repeated unchanged / zero CC123 | Send strike 64 → CC123(0) → restrike 64 → CC123(0) on channel 1, first at four distinct observed sample offsets in one callback, then with the commands in different callbacks and a restrike between them. Retain both zero points and callback boundaries. Repeat from reset with two identical nonzero values, such as MIDI 127. | Every delivered point releases the current channel holders, including zero and unchanged repeats within or across blocks. Commands are not rising-edge controls and do not automatically return to zero. | Not run |
| Same-offset duplicate CC123 | From identical held-note conditions, compare one CC123(0) with two CC123(0) messages for the same channel at one confirmed sample offset. Record whether the host delivers one point or both; keep this separate from distinct-offset repeats. | Same-channel/same-offset duplicates are idempotent: one release effect. Host coalescing of these duplicates is compatible with that result and does not establish delivery of later commands at different offsets. | Not run |
| Shared key on two channels | Hold pitch 64 on channels 1 and 2. Send CC123 on channel 1, then channel 2; compare with the same ordinary-release sequence. | Releasing the first channel does not damp the still-held key. The last holder's removal causes ordinary release. | Not run |
| Sustained release / CC120 | With CC64 down, strike 64, send CC123, then lift CC64. Separately send CC120 while a note remains held, recording any host panic translation. | CC123 preserves shared sustain; ringing may continue until pedal-up and decay afterward. CC120 is unmapped and has no advertised plug-in action; host-generated note-offs or resets are separate observations. | Not run |
| Equal-offset ordering | If the host permits, place a sustain change, CC123 on channels 1 and 2, and a new strike at one sample offset. Retain actual delivered order/offsets. | Sustain applies first, channel releases in ascending channel order, then notes in input-list order. The new strike survives the prior channel release; duplicate same-channel/same-offset commands have one release effect as checked separately above. | Not run |
| Pressure with releases | Compare a clip with and without valid polyphonic key-pressure events around note-offs. Where traces permit, verify pressure and releases share a callback; distinguish typed polyphonic pressure from channel pressure or note expression. | Pressure produces no synthesis or ownership response, and co-batched ordinary releases still take effect. The pressure traffic must not discard those releases or leave their holders stuck. | Not run |
| Generic text and recall | Perform the text checks and own-variant/legacy preset recall in section 3, recording which input reaches the plug-in. | Strict text conversion, unchanged gain/sustain IDs, and the exact legacy 16-byte configuration at unity Expression; non-unity saves use version 2. Transient commands remain excluded. | Not run |

The [pinned public-source review](research/HOST_MIDI_DELIVERY_20261124.md)
provides the rationale for these separate cases, **not native validation**. Its
inspected JUCE and AudioHost paths retain equal values at distinct offsets and
across blocks, but differ in same-offset coalescing. This source analysis does
not establish runtime behavior for either implementation or a named DAW/version.

A host may omit repeated unchanged controller values before producing VST3
parameter points, including a zero matching the current/default value. The
plug-in cannot recover an omitted command. A confirmed omission is a named-host
delivery limitation, not proof that the plug-in ignored a delivered point.
Conversely, a successful first CC123 release does not verify repeated delivery.
The explicit restrike between identical commands makes each later release
observable; retain the paired ordinary-release captures and any trace before
assigning a result. A same-tick piano-roll position does not prove an equal
sample offset or a shared callback. Exact ordering, channel-release order and
pressure batching remain harness-only claims when the host cannot expose them.

### Expression and output-layout cases — not run

Repeat these cases for each variant and each layout/processing format the host
actually offers. Start from identical reset, note, gain and sustain conditions;
use fixed unnormalized render intervals, such as four seconds with leading
silence. Compare each variant only with itself. Record requested UI values,
raw MIDI, the host's mapping, and actual delivered Expression ID `2` points as
separate evidence. Capture gain ID `0` points, note/release events, sample offsets
and callback boundaries as needed for the comparison. The delivery limitations
in the MIDI section apply equally to CC11 and direct UI/automation edits.

CC11 from any input channel addresses the same global Expression control. It
changes the complete output of that instance, independently of Monitoring Gain;
it does not change velocity or model a historical Stage volume circuit. The
plug-in consumes host-normalized values and does not pair CC11 with CC43 or
reconstruct a 14-bit value. Do not infer a per-channel mixer from channel-tagged
MIDI. In particular, raw CC11 data byte `64` is not an exact-half fixture:
`64/127` is not `0.5`, and the host's actual normalized mapping must be retained.
Use a confirmed delivered `0.5` for an exact-half control case. An upstream MIDI
monitor, an automation drawing or a displayed value alone is not delivery proof.

Gain and Expression interpolate independently. Each curve starts at offset `-1`
from the audio owner's canonical live value **after the recall selected for that
block**, not from a mailbox snapshot that may contain a newer unapplied UI
recall. Distinct delivered points are canonical binary32 anchors, the last point
at an equal offset wins, and the final value is held. The output uses binary64
interpolation; saved control values are canonical binary32. For normalized gain
`g` and Expression `e`, the output uses the checked monitored value
`(32 * g) * volts`, then applies `e`; unity retains the legacy arithmetic path.
Do not interpolate a single combined gain/expression curve as a substitute for
the product of the two independently interpolated curves.

Declare the comparison rule and capture precision before assessing a render.
Exact sample equality requires equivalent delivered inputs, reset, format and
capture boundary. Scaling an already rounded float capture can introduce a
different rounding step; retain that limitation when using it as a numerical
reference. A host render with unknown routing, parameter queues or callback
partitioning may support an audible observation, but the corresponding exact
scalar, interpolation or partition claim remains **inconclusive**.

| Case | Procedure | Expected outcome if the stated input is delivered | Result |
| --- | --- | --- | --- |
| Factory and default/negotiated bus metadata | Retain category, bus name/count, input/output inventory and accepted arrangement before and after the host's suspended mono/stereo reconfiguration, wherever observable. Record any immediate arrangement request on insertion. | Factory category `Instrument\|Synth`; no audio input and one audio output. Pre-negotiation default is `Stereo Out`/2; accepted mono is `Mono Out`/1, accepted stereo is `Stereo Out`/2. Category, bus name and width must agree with their distinct roles. Unexposed metadata or a pre-negotiation default never queried by the host remains unobserved. | Not run |
| Constant Expression through direct UI | From the same reset and notes at fixed 16x gain, set Expression to `1`, confirmed `0.5`, and `0` in separate runs. Retain the actual delivered values and any lead-in ramp before the comparison interval. | Steady output follows the delivered factor: unity, half level, or zero. Gain/sustain controls remain unchanged; no host normalization, clipping or later processing may explain the comparison. | Not run |
| Global CC11 across channels | Hold pitch 64 on channel 1 and pitch 67 on channel 2. Send CC11 at distinct times on channel 1, then channel 16; repeat other channels individually for any claimed all-sixteen-channel coverage. Observe live input and recorded playback separately. | Every delivered CC11 mapping targets Expression `2` for the complete instance output, including both held notes. A channel's MIDI label does not restrict the multiplier to that channel. Unknown host channel rewriting or mapping makes that delivery claim inconclusive. | Not run |
| Direct UI versus MIDI-delivered values | Retain a CC11 run's actual normalized values and offsets, then reproduce those same delivered Expression points through direct UI/automation from identical reset/input conditions. Separately compare the exact-half UI case with raw CC11 byte 64 only after recording its mapping. | Equivalent delivered curves give equivalent output under the declared capture rule. Different delivered values or timing are different test inputs; a raw 64 case cannot establish an exact 0.5 factor. | Not run |
| Independent gain and Expression ramps | Capture gain-only, Expression-only and combined runs with different ramp endpoints/times; retain both parameter queues and their initial live anchors. For example, ramp displayed gain from 8x to 24x while Expression ramps from 1x to 0.25x over an overlapping interval. | Each control follows its own delivered linear curve; the resulting multipliers combine at the output without one curve replacing the other. Reference reconstruction uses normalized gain, canonical binary32 anchors and binary64 interpolation. UI curve shapes alone do not establish this result. | Not run |
| Equal offsets and changed block partition | Where actual queue traces are available, deliver differing Expression points at one offset and record whether the host coalesces them. Repeat equivalent gain/Expression curves at two callback partitions with the necessary boundary anchors. | The final delivered point at an equal offset wins. Equivalent piecewise-linear curves agree under the declared comparison rule; changing only the device-buffer request does not prove equivalent callback inputs or partitioning. | Not run |
| Zero Expression through ordinary release | Compare an all-unity reference with note-on at 0.5 s, Expression zero by 1 s, note-off at 1.5 s, and unity restored at 2 s. Hold an explicit zero plateau across note-off, adding boundary anchors through the sample before restoration where supported. Capture through 4 s. | The zero plateau is silent while synthesis and release continue. Once delivered unity is reached, the remaining tail follows the reference's continuing timeline; restoration must not restart the strike or resume a frozen waveform. Confirm continued callbacks and unchanged lifecycle before attributing hidden-state behavior. | Not run |
| Zero Expression with sustain and channel release | Repeat the zero-plateau comparison with a sustained note, a delivered CC123 or ordinary note-off while Expression is zero, and pedal-up before unity restoration. Retain the matched unity-reference events. | Shared sustain and holder removal retain their ordinary semantics while the output is zero. Pedal-up and release continue damping/filter evolution; the resumed tail follows the reference. A later note-on or host note chase must not be mistaken for the surviving tail. | Not run |
| Default and explicit unity versus legacy stereo | With an authenticated own-variant pre-Expression build available, compare its stereo render with current default Expression and explicit unity, using identical delivered controls, notes, reset and capture boundary. Retain both loaded-build identities. | Unity keeps the legacy stereo calculation and output. If the earlier build or equivalent host delivery/capture is unavailable, leave the exact legacy-audio claim not run or inconclusive as applicable; a newer build cannot serve as a relabeled legacy baseline. | Not run |
| Negotiated mono versus each stereo channel | Render the same delivered timeline from reset through confirmed mono and stereo arrangements. Retain each pre-pan/pre-fader channel, actual bus width, routing and format. Include unity and non-unity Expression conditions. | Mono equals the scalar of either stereo channel; stereo channels equal one another. No stereo summing or added pan-law factor is expected. Unknown host downmixing/panning or unavailable boundary captures leave the exact scalar comparison inconclusive. | Not run |
| Omitted inactive output with continued callbacks | Only where the named host exposes bus activation and callback traces, deactivate its output bus while processing continues. Retain positive-frame calls, inactive-bus status and `numOutputs = 0`, delivered events/automation, then the observed reactivation/lifecycle sequence. | Accepted events, controls, synthesis and fault checks continue without output-buffer access while the bus is omitted. Compare any resumed audio with a matching reference only if no reset occurred. An inactive bus still supplied with buffers is a different case; an unavailable trace or a host that suspends processing does not establish omission coverage. | Not run |

Host track mute, bypass, disabled monitoring and silent-track suspension are
**not Expression zero**. Keep the plug-in processing and its output bus active
for the zero-Expression release cases. Record any automatic host suspension or
reset; if continued callbacks cannot be established, preserve the audible
result but mark hidden evolution **inconclusive**. No lower CPU cost follows
from zero Expression. Ensure the unity reference still has an observable tail
after restoration; two already-silent captures cannot demonstrate continuation.
Output omission is a separate conditional case; do not
use a host mute button or an absence of sound as evidence of `numOutputs = 0`.
Bus/queue traces establish delivered calls, not internal pointer access or fault
handling; those claims still require their dedicated instrumented harnesses.
Never inject invalid buffers or force a live audio-thread layout switch through
this manual protocol.

## 3. Check latency and saved configuration

Record the host's reported latency for each variant. At 48 kHz, 48 frames equals
1 ms. If the host permits delay compensation to be toggled, capture both settings
with identical routing and MIDI placement. Keep the original timestamps and
leading silence. A note's first nonzero sample or peak is not an exact group-delay
measurement, and cross-correlating different variants does not establish a
48-frame contract. Exact phase/convolution checks belong to the
[filtered](specs/FILTERED_AUDIO_20261123.md) and
[quadrature](specs/QUADRATURE_AUDIO_20261123.md) numerical fixtures.

Where the generic editor accepts typed text, record the exact entered string,
displayed value, resulting gain/sustain/Expression and host validation message. Gain should
accept `16x`, `+1.6e1 x`, `0` and `32`, but reject `nan`, `inf`, `16junk`, `0x10`,
`1,5`, `-1`, `33`, `1e999` and `1e-999` without clamping or changing the previous
configuration. Numeric text must be consumed completely; the parser uses a
bounded ASCII decimal grammar independent of locale. Gain's optional unit is
lowercase `x`. Sustain retains exactly `Off`/`On`. Channel-command text accepts
normalized decimal `0..1` (for example `0`, `0.5`, `1`) without a unit suffix;
`0.5x`, `On` and `2` are invalid for commands.

Expression should accept `0`, `0.5x`, `+5e-1 x`, `1` and surrounding ASCII
whitespace. It should reject `nan`, `inf`, `-0.1`, `1.1`, `0x1`, `1,0`, `0.5junk`,
`0.5X`, `1e999` and `1e-999`, preserving the prior value. Include `1.00000001`
and `-1e-50`: a parsed finite binary64 value outside `0..1` must reject before
binary32 canonicalization, even when that later rounding would yield unity or
zero. Accepted gain/Expression display values round-trip through canonical
binary32; commands retain their binary64 text values. A host's rounded display
alone does not prove the exact stored value. The complete limits, signed-zero
handling and binary64 range-check qualification are in
[generic parameter text](VST3.md#generic-parameter-text).

Hosts can filter, rewrite or parse text themselves. Record that behavior
separately: a host-side rejection does not establish the plug-in's parser result.
Converting command text alone does not execute a release; committing a control
edit may cause the host to deliver a command point. Treat any such delivery as
part of the observation. Null pointers, invalid IDs, unterminated strings and
caller-output preservation belong to the API harnesses, not this manual UI trial.

Save separate own-variant projects or plug-in presets with distinctive gain and
sustain settings at Expression `1x` and `0.375x`. Change all three controls, then
restore each preset. Check the controls and a fresh strike. Where the
host supports saving during playback, record whether saving itself interrupts
the sound and whether the host changed processing lifecycle at that moment.
Successful recall restores configuration and resets notes, mechanics and filter
history; it does **not** resume the previously ringing waveform. Suppress or
record host note chasing so a new note after recall is not misidentified as
restored ringing state.

When an authentic pre-continuation own-variant project or preset is available,
retain its original source/version and bytes, then open it with the selected
build. Otherwise mark legacy recall **Not run**; do not manufacture a legacy
artifact by relabeling a new preset. Gain/sustain IDs remain `0`/`1`, Expression
uses `2`, and the six class identities and model tags `1/2/3` remain unchanged.
An own-model version-1 chunk is exactly 16 bytes and recalls Expression at `1x`.
Saving canonical unity Expression retains the exact legacy version-1 encoding;
non-unity Expression requires a 20-byte version-2 chunk. A valid own-model
version-2 unity chunk is accepted and may save canonically as version 1. See the
[adaptive state contract](VST3.md#saving-and-resetting-state) for field details.
CC123 command values are transient and excluded from both formats. Repeat
own-variant recall after using channel commands and verify restored
gain/sustain/Expression and a fresh strike.
Record any additional parameter replay, command delivery or note chasing by
the DAW; its project/preset container may store more than the plug-in's state
chunk. File size or a command's displayed value alone cannot prove the exact
16-/20-byte wire contract or that recall executed a command. Keep a raw chunk
observation separate from its enclosing project/preset; retain the exact bytes
and their provenance if the host exposes them. Byte compatibility and all
rejected-state paths remain harness checks unless directly observed.

**Rollback limit:** an older version-1-only build cannot load version-2 chunks.
Keep the original project, presets and producing build. In the current build,
set Expression to `1x` and save a separate own-model preset before testing an
older build in a fresh host process. This prepares a compatible configuration;
it cannot retain the previous non-unity Expression value. Use a disposable
project and supported stereo routing for that older build. A host project may
also store Expression automation, parameter inventory and mono routing outside
the plug-in chunk, so a version-1 preset does not guarantee whole-project
rollback. Record host refusal, parameter replay or conversion separately from
an observed plug-in load result; preserve failed trials.

| Configuration/text case | Procedure and evidence | Expected contract | Result |
| --- | --- | --- | --- |
| Strict Expression text | Perform the accepted/rejected strings above from a distinctive prior value; record the exact input reaching the plug-in, displayed result and host validation. | Accepted forms produce the stated factor; invalid input leaves prior configuration unchanged. If the host parses or rejects text first, the plug-in parser result remains inconclusive. | Not run |
| Unity save and recall | Save Expression `1x` with distinctive gain/sustain, change all controls, then restore. Retain raw chunk bytes only if directly available. | All persistent controls restore; the plug-in writer emits exact legacy 16-byte version 1 at canonical unity. UI/project-container observations alone do not establish those bytes. | Not run |
| Non-unity save and recall | Repeat at Expression `0.375x` for each own-model preset, including a project reopen. | Expression, gain and sustain restore through the adaptive 20-byte version-2 configuration; recall resets dynamics, and transient commands are not recalled. Exact chunk length remains unobserved without a raw chunk. | Not run |
| Authentic legacy version-1 recall | Retain an authentic earlier own-model artifact and producing build/source; change current Expression away from unity before loading it. | Legacy gain/sustain restore and Expression becomes `1x`. Suppress or account for host automation/parameter replay that could overwrite the recalled value. An unavailable authentic artifact leaves this case not run. | Not run |
| Valid version-2 unity recall | Only if a trustworthy existing valid own-model version-2 unity fixture is available through an ordinary host load path, retain its bytes/provenance, load it, and save separately. | Unity is accepted; the plug-in may write version 1 afterward. Without raw chunks, byte canonicalization is inconclusive; do not manufacture the case by assuming a newly saved unity preset is version 2. | Not run |
| Rollback to a version-1-only build | Prepare the separate unity preset above, then use the identified older build in a fresh host process and disposable project; retain loaded paths and any rescan. | The unity configuration is representable in version 1. Non-unity version-2 presets cannot be loaded by that older decoder; whole-project compatibility is a separate host observation. | Not run |

Exercise the host's documented deactivate/reactivate or unload/reload operation
and record what it actually does. A transport stop or bypass button alone does
not prove that the host called the plug-in's lifecycle reset. If the host offers
cross-variant preset loading, record whether it rejects the request itself or
submits it to the plug-in. The three model tags are incompatible; ordinary UI
behavior cannot prove all six low-level rejection paths or transactional state
preservation. Those remain harness checks in [VST3 verification](VST3.md#saving-and-resetting-state).

## 4. Keep three variants in one project

Load all three together, route separate MIDI clips to them, and capture separate
stems. Change only one track's gain/sustain/Expression, reload its own preset,
and remove and reinsert that instance while the others remain loaded. Check
that names, controls and sound remain associated with the intended instances.
CC11 is global within the receiving instance, not across independent plug-ins;
retain all MIDI routing and any host parameter-linking/group settings that could
otherwise explain cross-track changes. Use mixed mono/stereo arrangements where
the host actually supports them, retaining each accepted bus name/count.

| Coexistence case | Procedure and observation | Result |
| --- | --- | --- |
| Independent controls and routing | Use distinctive gain/sustain/Expression settings on models 1, 2 and 3. Send CC11 only to one instance, then change its direct controls. Confirm the other instances retain their settings and matched reference output; retain any fan-out or linked-control routing. | Not run |
| Mixed saved configurations and layouts | Save a project with all three present, at least one unity Expression configuration and one non-unity configuration, and available mixed layouts. Reopen and verify all three controls, intended model identities, host-restored routing and fresh strikes. A layout restored by the host is not evidence that it was inside the instrument chunk. | Not run |
| Own-preset recall and instance replacement | Recall each own-model preset, then remove/reinsert only that instance while the other two remain loaded. Check names, control inventory and state isolation; do not compare different model audio paths as if they should null. | Not run |

This produces a named-host coexistence observation. The [retained loader-fixture
results](VST3.md) cover the recorded source, native platforms and synthetic host
harnesses.

## 5. Record host limits and live behavior

Repeat a short fixed passage at a second supported sample rate and device buffer
setting that the actual host/device offers. The adapter accepts 32–192 kHz,
float32/float64 and at most 8192 frames per call. Record unavailable settings
instead of inferring coverage. In a disposable project, an unsupported setup
such as 22.05 kHz can be tried only through an ordinary host option: distinguish
host refusal or internal resampling from an observed plug-in rejection, then
return to the known-good configuration.

Pitch bend, note expression, physical half-pedal, CC120 mapping, effects
and extra historical circuits are not advertised features. Record any host
handling without requiring those controls to produce a musical effect. CC123
and validated polyphonic pressure follow the proposed observations in section 2;
pressure is accepted without a synthesis response. Do not inject malformed
buffers, pointers, state bytes or event counts through the UI;
whole-block rejection, silence flags, fault latching and allocation/locking
policy need the dedicated harnesses. This includes malformed/duplicate queues,
late invalid points, active-output omission, bounded rejection cleanup, exact
zero-frame nonadvancement and coherent concurrent recall. Accepted engine or
nonfinite monitored-output faults still fault at Expression zero; silent output
cannot establish that behavior. A responsive UI does not verify these contracts.

For live playback, retain passage duration, note/event density, repetition count,
host load indication and every reported underrun/dropout or audible discontinuity.
Record what the counter measures and whether it is available. Keep offline
renders separate from live-device captures; neither a clean offline render nor
zero observed underruns establishes a worst-case callback or future deadline.

## 6. Retain a reviewable result

Keep the project, MIDI/event data, presets, original renders, host logs and relevant
screenshots together with a short session record. For audio, retain sample rate,
format, channel count, sample count, capture point, normalization/PDC settings and
the original bytes with SHA-256 hashes. A loopback recording also needs its device
route, gain and latency recorded; it is a different observation from an internal
render. Preserve failures and the first attempt when repeating a case.

Use one row per exercise and configuration: **observed pass**, **observed failure**,
**inconclusive**, or **Not run**, with the actual artifact and reason. Include
separate host-delivery and plug-in-response findings wherever either is uncertain,
plus reproduction steps, expected contract and observed behavior for a failure.
Record the variant/model, layout, host sample format where known, requested
input, observed delivery, capture boundary and comparison rule on each row.
If a requested routing option or trace is unavailable, mark the exact claim
**inconclusive** for the attempted session; leave cases never attempted
**Not run**. The unrun tables above are templates, not evidence of a native DAW
session. Keep every prior dated observation and first failure with its original
source and scope; append later results instead of rewriting them. Native SDK,
module-validator or loaded-module harness results remain separate from named-DAW
observations even when they run on the same OS.
The integrator can then review the session against
[project acceptance](agents/ACCEPTANCE_REVIEW.md); following this protocol alone
does not change canonical task status or authenticate a physical specimen.
