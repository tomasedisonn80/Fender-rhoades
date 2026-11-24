# Public source review: mapped CC123 delivery

Inspected 2026-11-24. This is source analysis of two host implementations,
not a host build, MIDI injection experiment, or native DAW qualification.

## Source versions

| Implementation | Frozen revision and commit date (UTC) |
| --- | --- |
| JUCE 9.0.6; `master` resolved during this review | [`654bdbc7a9db0dae90849b44abe91f339e9f0768`](https://github.com/juce-framework/JUCE/commit/654bdbc7a9db0dae90849b44abe91f339e9f0768), 2026-11-19 16:04:56 |
| Steinberg AudioHost; repository-pinned VST3 SDK 3.8.1 | [`public.sdk` `586dc5e6c8012c3e4b01c79389375cbe96bdb1da`](https://github.com/steinbergmedia/vst3_public_sdk/commit/586dc5e6c8012c3e4b01c79389375cbe96bdb1da), 2026-08-11 13:35:09 |
| Steinberg interface contract, same SDK pin | [`pluginterfaces` `4f547e8e102b47de4a8b8aaf343c73b700786372`](https://github.com/steinbergmedia/vst3_pluginterfaces/commit/4f547e8e102b47de4a8b8aaf343c73b700786372), 2026-08-11 13:35:08 |

## Proven in the inspected source

**JUCE.** The public
[`juce_VST3PluginFormat.cpp`](https://github.com/juce-framework/JUCE/blob/654bdbc7a9db0dae90849b44abe91f339e9f0768/modules/juce_audio_processors/format_types/juce_VST3PluginFormat.cpp#L37)
includes the implementation now located in the headless module.
[`juce_VST3Common.h`](https://github.com/juce-framework/JUCE/blob/654bdbc7a9db0dae90849b44abe91f339e9f0768/modules/juce_audio_processors_headless/format_types/juce_VST3Common.h)
stores bus-zero mappings for all 16 channels and controller numbers
(`StoredMidiMapping`, 1088–1137). `toVst3ControlEvent` handles CC123 through its
ordinary controller branch; `sendMappedParameter` invokes the callback for a
successful mapping without consulting parameter flags (1222–1242, 1556–1567).
The conversion loop processes at most 2048 incoming MIDI messages per call.

[`juce_VST3PluginFormatImpl.h`](https://github.com/juce-framework/JUCE/blob/654bdbc7a9db0dae90849b44abe91f339e9f0768/modules/juce_audio_processors_headless/format_types/juce_VST3PluginFormatImpl.h)
initializes input queues from every declared parameter ID (108–121, 3118–3124).
The MIDI callback appends before updating the UI cache (3322–3349).
`HostToClientParamQueue::append` retains every supplied point without comparing
values or offsets for equality (1776–1784). `processAudio` clears queues per call
and supplies them to the processor (2490–2510). `isAutomatable` checks
`kCanAutomate` separately; it does not gate this MIDI path (2107–2110).
The UI cache's equality check therefore cannot remove an already appended MIDI
point. [`MidiBuffer::addEvent`](https://github.com/juce-framework/JUCE/blob/654bdbc7a9db0dae90849b44abe91f339e9f0768/modules/juce_audio_basics/midi/juce_MidiBuffer.cpp#L134-L164)
also inserts without value deduplication.

**Steinberg AudioHost.**
[`audioclient.cpp`](https://github.com/steinbergmedia/vst3_public_sdk/blob/586dc5e6c8012c3e4b01c79389375cbe96bdb1da/samples/vst-hosting/audiohost/source/media/audioclient.cpp)
collects successful mappings without checking flags (35–70), adds mapped changes
with their timestamps (380–408), and clears queues after successful processing
(274–294). [`miditovst.h`](https://github.com/steinbergmedia/vst3_public_sdk/blob/586dc5e6c8012c3e4b01c79389375cbe96bdb1da/samples/vst-hosting/audiohost/source/media/miditovst.h#L85-L101)
treats CC123 as an ordinary controller.
[`jackclient.cpp`](https://github.com/steinbergmedia/vst3_public_sdk/blob/586dc5e6c8012c3e4b01c79389375cbe96bdb1da/samples/vst-hosting/audiohost/source/media/jack/jackclient.cpp)
forwards each received MIDI event before audio processing (205–219, 290–319).
[`ParameterValueQueue::addPoint`](https://github.com/steinbergmedia/vst3_public_sdk/blob/586dc5e6c8012c3e4b01c79389375cbe96bdb1da/source/vst/hosting/parameterchanges.cpp#L68-L96)
preserves identical values at distinct offsets, but replaces an existing point
at the same offset for that parameter. Queue reuse clears prior points
(154–180); it does not compare against the previous block's value.

## Interpretation and runtime boundary

Both traced MIDI paths support writable `kNoFlags` mappings, including repeated
zero values, conditional on valid messages reaching these paths and successful
processing. JUCE retains same-offset duplicates; AudioHost merges them, which
matches the local command contract's idempotence. No flag-contract defect was
found in [our controller](../../plugins/vst3/controller.cpp).

The pinned [`ivsteditcontroller.h`](https://github.com/steinbergmedia/vst3_pluginterfaces/blob/4f547e8e102b47de4a8b8aaf343c73b700786372/vst/ivsteditcontroller.h)
separates automation permission from read-only/hidden flags and describes
`IMidiMapping` translation. However,
[`ivstparameterchanges.h`](https://github.com/steinbergmedia/vst3_pluginterfaces/blob/4f547e8e102b47de4a8b8aaf343c73b700786372/vst/ivstparameterchanges.h#L36-L55)
allows constant-curve points to be omitted. Thus these implementations support
the design; the interfaces do not guarantee universal per-message retention.

For each actual host/version, verify first/default-zero delivery, then
strike → CC123(0) → restrike → CC123(0), both within and across blocks. Separate
same-offset duplicates. Observe input queues where instrumentation permits;
UI values alone cannot prove delivery. Check live and recorded-MIDI routes
independently. Upstream filtering, timing, parameter replay, build configuration,
and named-DAW behavior still require runtime evidence.
