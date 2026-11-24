# W4-67 — Manual native-DAW evaluation protocol

**Agent:** `/root/w4_67_daw_protocol`  
**Assignment:** 67, wave 4  
**Result:** documentary protocol delivered; native DAW execution remains unperformed.

Created [DAW_VALIDATION.md](../../DAW_VALIDATION.md) and this report only.
The guide provides a practical sequence from exact artifact/session identity to
discovery, MIDI range, retrigger/release, overlapping holders, sustain, gain,
latency, configuration recall, lifecycle behavior, three-variant coexistence,
host limitations and retained results. It uses the current adapter/controller
implementation and existing frozen contracts; it introduces no third-party
code or unsupported physical parameters.

The overlap exercise compares two explicit channel/timing schedules and makes
host event rewriting an inconclusive condition. The latency section distinguishes
reported 0/48-frame delay from note onset and cross-variant waveform differences.
Recall restores gain/sustain and resets mechanics, rather than restoring ringing
notes. Host note chasing, transport/bypass semantics, export precision and device
buffer requests are kept separate from unobserved plug-in callbacks.

Results require actual source/artifact hashes, named host/device/settings,
original captures, observed underruns, and per-exercise pass/failure/inconclusive/
not-run records. The protocol does not turn ordinary GUI operation into proof of
hidden rejection, allocation/locking, exhaustive concurrency or device deadlines.
Its opening explicitly says it is proposed and unexecuted. Physical fidelity
remains unvalidated and task decisions remain with the integrator.

## Executed verification

```sh
python3 tools/project.py validate
python3 tools/project.py sync --check
python3 tools/project.py next --json
python3 tools/check_docs.py
```

Required state checks passed at revision 27 with 21 evidence records; no task
was independently claimed or changed. Documentation integrity passed. Targeted
source reads checked names, model tags, latency, controls, MIDI and lifecycle
rules. A Python whitespace/newline check passed. All eight supporting file
hashes below matched before and after drafting.

Verification was documentary: no DAW session, installation, audio capture,
build, test suite, benchmark or numerical replay ran. No Git, ledger or canonical
state mutations were performed. Root can link the guide from the current VST3
instructions, then use a recorded native session to create actual
host evidence. Historical CI/harness results are not relabeled as that session.

## Examined source identities

| File | SHA-256 |
| --- | --- |
| `docs/DAW_VALIDATION.md` (new guide) | `a2661f2514e1dc2cdbca7f1c94be8dbea115d506bc0657a4ed8381fc34a82b91` |
| `docs/VST3.md` | `ca02322d8a5c9dff5a64321bf6c622c6c78f0ada139e8d3cb28844d0476cd11b` |
| `docs/specs/HOST_PROTOTYPE_20261123.md` | `d22d1b4757c6a37655f40ae5d1b6b2b41d6d033504cf613526e3827b47a1cd46` |
| `docs/specs/FILTERED_AUDIO_20261123.md` | `d5e154a32944d08b3944ead7fe0eb52b978ff914961c40dfffa6f40b85cdc3c4` |
| `docs/specs/QUADRATURE_AUDIO_20261123.md` | `cf47025b89006496ed999036c01dabc347c210a0140679e727580c91601e5006` |
| `plugins/vst3/processor.cpp` | `97b449dac4e1eb434af8ad4e279bdf7ba07f4668cb174199980eea0266493cf6` |
| `plugins/vst3/controller.cpp` | `79800f5df2781fc48dcf0c32afcc5d3b08efa067ba89e1a659bcfed240749349` |
| `plugins/vst3/plugin_ids.hpp` | `1b5744731384e100c2f32cfed42fccf9fe2168d39df5cc29f1ac1108b9d94a81` |
| `include/rhodes/note_tracker.hpp` | `76931b326daae3a112b56420f9be9ea5b03b1c30307f1dc68b6c13c710cd830f` |
