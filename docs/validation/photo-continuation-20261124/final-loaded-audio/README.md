# Loaded panel audio — final Linux observation

The strengthened SDK-only fixture passed one source-bound CTest invocation on
all three actual final Linux bundles, using float32 and float64 stereo at 48 kHz.
It made **96 fresh-instance renders, 36,000 process callbacks, 24,173,604 scalar
comparisons and 12,698,530 checks**. Each render contains two seconds of synthetic
audio; no recorded-note playback or measured-instrument comparison is involved.

The retained [manifest](strengthened/manifest.json) binds source, commands, binary
identities and raw logs. All recorded source inputs and the three plugin binaries
were unchanged across configure, build and execution. The production modules are
byte-identical to those in the final native and legacy observations. CMake changed
only to register this extra fixture and include it in the macOS signing/universal
target list. This observation itself ran only on Linux.

The fixture checks meaningful nonzero output, finite samples and a conservative
32-times-neutral-peak bound in monitored synthetic harp-voltage units. It verifies
gain scaling, Sustain against an independently held-note reference, distinct Bass
and Treble spectral responses, vibrato intensity/speed changes, opposed stereo
channel sums and separation, disabled/zero-depth neutral identity, and saved-neutral
identity. The one-second switch-off occurs at offset 128 inside a 256-frame
callback; every sample of the 480-frame linear envelope is checked against the
independent enabled and neutral renders, with exact neutral bits at frame 48479.
The [complete log](strengthened/ctest.stdout.log) retains per-format metrics;
[the summary](strengthened/run-summary.json) contains the count and mapping results.

The [initial passing fixture](before-review/README.md) is retained separately.
Its weaker assertions did not establish shelf mapping, opposed stereo or every
ramp sample. Worker 80 identified those gaps; worker 81 strengthened the fixture
before this final execution. No initial assertion was removed or weakened.

These results establish the listed synthetic processing and integration contracts.
They do not establish physical circuit authenticity, all possible parameter
transactions, named-DAW rendering, Windows/macOS native runtime, non-unit display
scaling or audio-device deadline performance.
