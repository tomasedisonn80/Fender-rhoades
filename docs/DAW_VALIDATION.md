# Manual native-DAW evaluation

**This is a proposed protocol, not an executed DAW result.** It is intended to
turn a trial of the three experimental instruments into a reproducible host
observation. Existing [native CI results](validation/native-platforms-20261124.md)
cover the stated harnesses and validators. A future session using this guide
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
defaults to **16x** and spans **0–32x**; Sustain is **Off/On**. The output is the
same synthetic mono signal on both stereo channels, with no audio input.
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
ordering rules remain in the [host contract](specs/HOST_PROTOTYPE_20261123.md).

## 3. Check latency and saved configuration

Record the host's reported latency for each variant. At 48 kHz, 48 frames equals
1 ms. If the host permits delay compensation to be toggled, capture both settings
with identical routing and MIDI placement. Keep the original timestamps and
leading silence. A note's first nonzero sample or peak is not an exact group-delay
measurement, and cross-correlating different variants does not establish a
48-frame contract. Exact phase/convolution checks belong to the
[filtered](specs/FILTERED_AUDIO_20261123.md) and
[quadrature](specs/QUADRATURE_AUDIO_20261123.md) numerical fixtures.

Save a project or plug-in preset with distinctive gain and sustain settings,
change them, then restore it. Check the controls and a fresh strike. Where the
host supports saving during playback, record whether saving itself interrupts
the sound and whether the host changed processing lifecycle at that moment.
Successful recall restores configuration and resets notes, mechanics and filter
history; it does **not** resume the previously ringing waveform. Suppress or
record host note chasing so a new note after recall is not misidentified as
restored ringing state.

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

Pitch bend, note expression, physical half-pedal, CC120/CC123 mappings, effects
and extra historical circuits are not advertised features. Record any host
handling without requiring those controls to produce a musical effect. Do not
inject malformed buffers, pointers, state bytes or event counts through the UI;
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
reproduction steps, expected contract and observed behavior for a failure.
The integrator can then review the session against
[project acceptance](agents/ACCEPTANCE_REVIEW.md); following this protocol alone
does not change canonical task status or authenticate a physical specimen.
