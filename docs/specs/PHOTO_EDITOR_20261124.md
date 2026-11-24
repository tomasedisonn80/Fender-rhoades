# Photo editor development contract — 24 November 2026

## User-selected visual target

The owner's [unchanged reference](../../assets/reference/rhodes-suitcase-reference.webp)
is the authoritative visual target. The native VST3 editor must display the full
1536 × 1152 image at its original 4:3 aspect ratio. Transparent controls align
with the photographed controls and perspective keyboard. The reference is used
directly, without a substitute generated illustration or redesigned dashboard.
Transient interaction/focus feedback may appear while a control is operated.

The cabinet, legs, original background and pedal are part of the composition.
Image scaling and pointer hit testing must use the same coordinate transform.
There is no automatic crop, aspect distortion or independent control scaling.

## Working musical controls

Volume controls existing Monitoring Gain (ID 0); the pedal controls Sustain
(ID 1). Expression (ID 2 / CC11) remains independent. Five new persistent
parameters are reserved: Bass (3), Treble (4), Intensity (5), Speed (6), and
Vibrato (7). Defaults are neutral EQ, zero intensity, midpoint speed and bypass.
The exact picture remains the idle surface; keyboard and mouse editing, host
automation and state recall must update the same underlying parameters.

The tone and modulation implementation is a documented synthetic digital effect.
It must not be described as an identified or calibrated Peterson/Janus circuit.
Its default path must preserve existing sample values and latency exactly.
Existing three plugin identities and model IDs remain stable.

## State and real-time boundaries

Existing v1/v2 state codecs remain valid. An extended state format must retain
all new controls without silently discarding settings, and default panel values
must permit exact legacy exports. A single coherent configuration transaction
must cover both legacy and panel settings. Audio callbacks may not wait, spin,
allocate memory, access image files or manipulate native windows.

The editor must pair host edit gestures, release held audition notes when it
closes or loses focus, and support multiple independent instances. Photograph
key hit testing prioritizes black keys and preserves the observed perspective.
Implementation must identify the supported audition transport and its limits.

## Qualification

Verify real native module construction, editor lifecycle, parameter/state/audio
integration, malformed input rejection, neutral audio compatibility, resizing,
resource packaging and an actual rendered editor screenshot. Record each
observed platform separately. A passing headless suite cannot establish visual
correctness or DAW operation. Keep original reference, screenshot and test scope
distinguishable. Physical calibration and release packaging remain separate.

## Public technical sources

- [Steinberg IPlugView contract](https://steinbergmedia.github.io/vst3_doc/base/classSteinberg_1_1IPlugView.html)
- [Steinberg VSTGUI editor binding](https://github.com/steinbergmedia/vstgui/tree/5db272256172557818b6158cf0bb2c4410bddb25/vstgui/plugin-bindings)
- [Original Rhodes service manual index](https://www.fenderrhodes.com/service/manual.html)

These sources support host integration and historical control context. They do
not supply measurements of the depicted instrument.
