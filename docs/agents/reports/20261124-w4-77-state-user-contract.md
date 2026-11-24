# Wave 4 / 77: user control and recall guidance

## Decision

Corrected one narrow fault-recovery instruction; the remaining reviewed user instructions match the implementation.

The VST3 fault paragraph previously required a **lifecycle** reset. In `processor.cpp`, a pending valid recall bypasses the faulted-state rejection and calls `install_recall()`, which resets the engine and clears the conversion fault. After integrator authorization, corrected only that paragraph: an explicit hard reset can come from a lifecycle transition or valid configuration recall installed at an accepted audio boundary. Queued recall and whole-block rejection descriptions remain intact; `setState()` is not described as immediately clearing faults. No implementation changed.

## Matched contracts

- Controller and processor agree on Monitoring Gain 0–32x, default 16x; normalized gain multiplies voltage by 32. Sustain is binary at 0.5, initially Off; CC64 maps on bus zero across all 16 channels.
- MIDI 28–100 is inclusive. Other valid pitches are ignored. Holders are channel/identity aware; only the final holder releases a key. The proposed two-channel overlap comparison respects this. Retrigger adds an impulse to existing mechanics; release/pedal changes damping.
- RHST version 1 stores only gain/sustain in 16 bytes. Model tags 1/2/3 are incompatible. Valid recall is queued until an accepted audio boundary or synchronized lifecycle reset; it clears notes, mechanics, cursor and applicable filter history. Saving only snapshots configuration.
- Three separate identities report 0/48/48 frames and infinite tails. Stereo duplication, no audio input, 32–192 kHz, float32/64 and 8192-frame bounds match. Unsupported controls are not promised; unsupported input event kinds can reject a block.

## Checks and limits

Read AGENTS and project context. Required checks passed: `project.py validate` (14 tasks, 22 evidence records), `sync --check`, and `next --json` (revision 27; no available tasks). No Git, build, tests or DAW execution.

The current DAW guide includes the concurrent fresh-process/loaded-bytes paragraph. Source inspection establishes implementation agreement, not actual host mapping, UI, lifecycle delivery, audio performance or fidelity. Those proposed observations remain unexecuted.

## Inspected source hashes

SHA-256 values bind the reviewed working files; no commit identity is inferred.

| Path | SHA-256 |
| --- | --- |
| `docs/VST3.md` | `9406a1b9883665a040ba58ea488853060526344b09df8c3c27a33f34def62796` |
| `docs/DAW_VALIDATION.md` | `18c7362ae9c42f054268efe613b21aed70161ea43c96965b1a13f1a0acfc12af` |
| `plugins/vst3/controller.cpp` | `79800f5df2781fc48dcf0c32afcc5d3b08efa067ba89e1a659bcfed240749349` |
| `plugins/vst3/processor.cpp` | `97b449dac4e1eb434af8ad4e279bdf7ba07f4668cb174199980eea0266493cf6` |
| `plugins/vst3/processor.hpp` | `0250eaaca92b2aa904b5a05e8aca8cc46cd8a63111d145ddf7d0956f682d1807` |
| `plugins/vst3/plugin_ids.hpp` | `1b5744731384e100c2f32cfed42fccf9fe2168d39df5cc29f1ac1108b9d94a81` |
| `plugins/vst3/state_stream.cpp` | `b5f0dd71c5523f80359027168f0df3307c85d70b3d5654f3f936144f3307d172` |
| `include/rhodes/instrument_state.hpp` | `c11c9d982887f4228c76c3ee9e10fe5627c2dc0f1053ad531cfd316285a65616` |
| `include/rhodes/note_tracker.hpp` | `76931b326daae3a112b56420f9be9ea5b03b1c30307f1dc68b6c13c710cd830f` |
| `include/rhodes/research_engine.hpp` | `7dca093866c19f18f4403f86daea96ac485d86b3e6aebc4441477bd5a8a16621` |
| `src/research_engine.cpp` | `f0facfcb2c28f989c79407d2e0571a868b07ec7a37511bc05f7a33efcdeb0703` |
| `include/rhodes/audio_engine.hpp` | `10efac8150c07e69ef41eed415a6e7403b4339179af902f91cd17ef498da7b97` |
| `include/rhodes/quadrature_engine.hpp` | `c625a82b61eb3e721061177654c12ffa6aac0049e026b9633ae53ea2abaa773d` |
| `include/rhodes/event_processor.hpp` | `36e4b64f3278b0079b20e94a333a657c900e4118dc24433ec9d8c58442651351` |

