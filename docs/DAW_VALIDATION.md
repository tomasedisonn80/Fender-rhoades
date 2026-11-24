# Manual native-DAW evaluation

**This is a proposed protocol, not an executed DAW result.** It is intended to
turn a trial of the three experimental instruments into a reproducible host
observation. Existing [native CI results](validation/native-platforms-20261124.md)
cover the stated harnesses and validators at their recorded source revisions;
they predate the [MIDI continuation](specs/HOST_MIDI_20261124.md) and do not
establish native acceptance of its changes. A future session using this guide
covers its named DAW, machine, settings and build only; it does not establish
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
and live/offline mode. Export bit depth is not proof of plug-in processing
precision; device buffer size is not proof of each plug-in callback size.
Mark settings the host does not expose as unknown.

A useful first condition is **48 kHz, a requested 256-frame device buffer,
120 BPM**, with track/master effects, normalization and automatic gain changes
disabled. These are proposed reproducibility settings, not a recommended
performance limit. Keep an explicit render interval with leading silence and
a fixed tail; the plug-ins advertise an infinite asymptotic tail, so automatic
tail detection is not a consistent endpoint.

## 2. Discover and play each instrument

Scan the complete bundles using the host's ordinary procedure. Load each on its
own stereo instrument track and record the displayed name, available controls,
reported latency and any scan/load error. The current identities are defined in
[the implementation](../plugins/vst3/plugin_ids.hpp):

| Instrument | Bundle | State model | Reported latency |
| --- | --- | ---: | ---: |
| Rhodes Research Prototype | `RhodesResearch.vst3` | 1 | 0 frames |
| Rhodes Research Filtered | `RhodesResearchFiltered.vst3` | 2 | 48 frames |
| Rhodes Research Quadrature | `RhodesResearchQuadrature.vst3` | 3 | 48 frames |

Use the host's generic controls if it shows no custom editor. Monitoring Gain
defaults to **16x** and spans **0–32x**; Sustain is **Off/On**. Expect **18 host
parameters**: those two controls plus `All Notes Off Ch 1` through
`All Notes Off Ch 16`. Gain and sustain retain IDs `0` and `1`; the channel
commands use `0x1000..0x100f`. Commands are writable and nonautomatable, with
numeric normalized values `0..1`, and are not hidden or read-only. A host may
omit them from its automation chooser while still exposing them in its generic
parameter view. Record what each view actually shows. If discovery still shows
only the old two-parameter inventory, use the host's documented rescan/refresh
procedure and restart its plug-in-host process where available; retain the
before/after inventory and loaded-build identity rather than assuming a rescan
loaded the intended module.

The output is the same synthetic mono signal on both stereo channels, with no
audio input.
An unequal post-fader capture can reflect host routing or pan law, so retain the
capture point and routing before attributing it to the plug-in.

Create a short MIDI clip at a fixed nonzero velocity, recording the actual
events and channel assignments. Use MIDI pitch numbers because octave labels
differ between hosts. Repeat the following separately for all three variants;
their audio paths are different and are not expected to null against one another.

| Exercise | Procedure and observation |
| --- | --- |
| Range | Play pitches 28, 64 and 100 separately. Test 27 and 101 from a fresh, silent instance: valid pitches outside 28–100 should not excite a key. Do not mistake a preceding note's tail for an out-of-range response. |
| Retrigger and ordinary release | Strike 64, release it, then restrike while its tail remains. Capture the attack/release sequence and compare it with an isolated fresh strike. Release changes damping; it does not hard-reset the resonator. Listening is a coarse regression observation, not proof of hidden mechanical state. |
| Overlapping holders | At 1.0 s start pitch 64 on channel 1; at 1.5 s start it on channel 2. Release channel 1 at 2.0 s and channel 2 at 2.5 s. Compare with a repeat that delays only the first release to 2.5 s. With distinct holders and otherwise identical delivered events, the earlier release should not damp the still-held key. |
| Sustain | Compare an Off run with a run that sends CC64 down before note release and up later. Record whether the host maps CC64 to the Sustain parameter. Note release under pedal should retain the held damping regime until pedal-up; pedal-up need not produce immediate silence. |
| Gain | From the same reset/input conditions, compare steady 8x and 16x settings, then 0x. Expect a factor of two before host clipping or other processing, and silence at zero gain. Retain unnormalized captures; gain is a monitoring conversion, not calibrated instrument loudness. |

A host can merge channels, rewrite note identities or chase notes during seek.
Retain its event display/export or trace when available. If it cannot deliver
distinct overlapping holders, mark that exercise **inconclusive**, rather than
claiming ownership coverage from a piano-roll drawing. The detailed bounds and
ordering rules remain in the [host contract](specs/HOST_PROTOTYPE_20261123.md)
and [MIDI continuation](specs/HOST_MIDI_20261124.md).

### MIDI continuation cases — not run

Repeat this small case set for each variant, using modest event counts and the
same reset, gain, render interval and routing for each paired comparison.
Channels below use MIDI labels 1–16 (SDK indices 0–15). Leave sustain Off except
where stated. Keep ordinary note-offs later than the command under observation
so they cannot explain an apparent CC123 release. Compare with a reference clip
that replaces that command with ordinary note-offs for the same holders at the
same times; release increases damping and need not produce instant silence.

Separate **requested input**, **host delivery**, and **plug-in response** in the
session record. A MIDI monitor upstream of the plug-in establishes only what
was sent to the host. Where the host offers a diagnostic trace, retain the
delivered parameter IDs/values and event types, channels, note identities,
callback boundaries and sample offsets. Record the observation point and its
limits. An unchanged generic-control display cannot establish that a second
command point arrived. If delivery or callback grouping is unavailable, retain
the audio/UI observation but mark the corresponding delivery or exact-ordering
claim **inconclusive**.

| Case | Procedure | Expected outcome if the stated input is delivered | Result |
| --- | --- | --- | --- |
| Parameter discovery | Record the generic parameter inventory, IDs/flags if exposed, and any rescan/restart used. | 18 parameters: gain `0`, sustain `1`, and 16 writable, nonautomatable channel commands `0x1000..0x100f`. Unknown host metadata remains unknown. | Not run |
| Channel-specific CC123 | Hold pitch 64 on channel 1 and pitch 67 on channel 2; send CC123 only on channel 1, then on channel 2. | Each command removes only its channel's holders. The other channel remains held until its own release; existing mechanical motion and filter tails remain. | Not run |
| Repeated unchanged / zero CC123 | Strike 64 on channel 1, send CC123 value 0, restrike, then send value 0 again. Repeat from reset with two identical nonzero values, such as MIDI 127. Retain both command deliveries and the intervening strike. | Every delivered point releases the current channel holders, including zero and unchanged repeats. Commands are not rising-edge controls and do not automatically return to zero. | Not run |
| Shared key on two channels | Hold pitch 64 on channels 1 and 2. Send CC123 on channel 1, then channel 2; compare with the same ordinary-release sequence. | Releasing the first channel does not damp the still-held key. The last holder's removal causes ordinary release. | Not run |
| Sustained release / CC120 | With CC64 down, strike 64, send CC123, then lift CC64. Separately send CC120 while a note remains held, recording any host panic translation. | CC123 preserves shared sustain; ringing may continue until pedal-up and decay afterward. CC120 is unmapped and has no advertised plug-in action; host-generated note-offs or resets are separate observations. | Not run |
| Equal-offset ordering | If the host permits, place a sustain change, CC123 on channels 1 and 2, and a new strike at one sample offset. Retain actual delivered order/offsets; optionally duplicate one same-channel command at that offset. | Sustain applies first, channel releases in ascending channel order, then notes in input-list order. The new strike survives the prior channel release; duplicate same-channel/same-offset commands have one release effect. | Not run |
| Pressure with releases | Compare a clip with and without valid polyphonic key-pressure events around note-offs. Where traces permit, verify pressure and releases share a callback; distinguish typed polyphonic pressure from channel pressure or note expression. | Pressure produces no synthesis or ownership response, and co-batched ordinary releases still take effect. The pressure traffic must not discard those releases or leave their holders stuck. | Not run |
| Generic text and recall | Perform the text checks and own-variant/legacy preset recall in section 3, recording which input reaches the plug-in. | Strict text conversion, unchanged gain/sustain IDs, and the existing 16-byte configuration contract; transient commands are excluded. | Not run |

Some hosts remove repeated unchanged controller values before producing VST3
parameter points, including a zero matching the current/default value. The
plug-in cannot recover an omitted command. A confirmed omission is a named-host
delivery limitation, not proof that the plug-in ignored a delivered point.
Conversely, a successful first CC123 release does not verify repeated delivery.
The explicit restrike between identical commands makes each later release
observable; retain the paired ordinary-release captures and any trace before
assigning a result. A same-tick piano-roll position does not prove an equal
sample offset or a shared callback. Exact ordering, channel-release order and
pressure batching remain harness-only claims when the host cannot expose them.

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
displayed value, resulting gain/sustain and host validation message. Gain should
accept `16x`, `+1.6e1 x`, `0` and `32`, but reject `nan`, `inf`, `16junk`, `0x10`,
`1,5`, `-1`, `33`, `1e999` and `1e-999` without clamping or changing the previous
configuration. Numeric text must be consumed completely; the parser uses a
bounded ASCII decimal grammar independent of locale. Gain's optional unit is
lowercase `x`. Sustain retains exactly `Off`/`On`. Channel-command text accepts
normalized decimal `0..1` (for example `0`, `0.5`, `1`) without a unit suffix;
`0.5x`, `On` and `2` are invalid. The complete limits and binary64 range-check
qualification are in [generic parameter text](VST3.md#generic-parameter-text).

Hosts can filter, rewrite or parse text themselves. Record that behavior
separately: a host-side rejection does not establish the plug-in's parser result.
Converting command text alone does not execute a release; committing a control
edit may cause the host to deliver a command point. Treat any such delivery as
part of the observation. Null pointers, invalid IDs, unterminated strings and
caller-output preservation belong to the API harnesses, not this manual UI trial.

Save a project or plug-in preset with distinctive gain and sustain settings,
change them, then restore it. Check the controls and a fresh strike. Where the
host supports saving during playback, record whether saving itself interrupts
the sound and whether the host changed processing lifecycle at that moment.
Successful recall restores configuration and resets notes, mechanics and filter
history; it does **not** resume the previously ringing waveform. Suppress or
record host note chasing so a new note after recall is not misidentified as
restored ringing state.

When an authentic pre-continuation own-variant project or preset is available,
retain its original source/version and bytes, then open it with the selected
build. Otherwise mark legacy recall **not run**; do not manufacture a legacy
artifact by relabeling a new preset. Gain/sustain IDs remain `0`/`1`, the class
identities and model tags remain unchanged, and the plug-in configuration is
still the version-1, 16-byte gain/sustain state. CC123 command values are
transient and excluded from that state. Repeat the own-variant recall after
using channel commands and verify restored gain/sustain and a fresh strike.
Record any additional parameter replay, command delivery or note chasing by
the DAW; its project/preset container may store more than the plug-in's state
chunk. File size or a command's displayed value alone cannot prove the exact
16-byte wire contract or that recall executed a command. Byte compatibility and
all rejected-state paths remain harness checks.

Exercise the host's documented deactivate/reactivate or unload/reload operation
and record what it actually does. A transport stop or bypass button alone does
not prove that the host called the plug-in's lifecycle reset. If the host offers
cross-variant preset loading, record whether it rejects the request itself or
submits it to the plug-in. The three model tags are incompatible; ordinary UI
behavior cannot prove all six low-level rejection paths or transactional state
preservation. Those remain harness checks in [VST3 verification](VST3.md#saving-and-resetting-state).

## 4. Keep three variants in one project

Load all three together, route separate MIDI clips to them, and capture separate
stems. Change only one track's gain/sustain, reload its own preset, and remove and
reinsert that instance while the others remain loaded. Check that names, controls
and sound remain associated with the intended instances. Repeat a saved-project
reopen with all three present. Retain any host parameter-linking/group settings
that could otherwise explain cross-track changes.

This produces a named-host coexistence observation. It does not extend the
repository's Linux-only loader-fixture result to every other host or platform.

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
policy need the dedicated harnesses. A responsive UI does not verify them.

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
**inconclusive**, or **not run**, with the actual artifact and reason. Include
separate host-delivery and plug-in-response findings wherever either is uncertain,
plus reproduction steps, expected contract and observed behavior for a failure.
The unrun table above is a template, not evidence of a native DAW session.
The integrator can then review the session against
[project acceptance](agents/ACCEPTANCE_REVIEW.md); following this protocol alone
does not change canonical task status or authenticate a physical specimen.
