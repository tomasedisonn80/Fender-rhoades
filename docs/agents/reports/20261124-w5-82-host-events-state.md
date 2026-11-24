# W5-82 — Host event and configuration acceptance review

Evaluator: `/root/w5_82_host_events_state`; 24 November 2026.

**HOST-001/C01 and C03 pass within the documented synthetic prototype domain.**
This bounded review does not decide C02/C04 or change canonical task status.

## Frozen definition

The exact projection below was read from state revision 27. Its canonical
UTF-8, sorted-key, compact-JSON SHA-256 is
`b5e43a5c4bfccd246d49bf60783b320a1629741489023757d01cb50207e48336`. Dependency PHY-001 is done;
required kinds remain numerical and documentary. No criterion was narrowed.

```json
{
  "task_id": "HOST-001",
  "acceptance": [
    "Bounded sample-offset event processing preserves producer order and physical key state; invalid blocks reject without mutation.",
    "An optional VST3 instrument builds against an explicitly identified public SDK and passes the available SDK validator with bounded audio processing and documented output scaling.",
    "Host configuration round-trips through a versioned validated state format; MIDI range, sustain, overlapping notes, reset and unsupported host conditions are explicit and tested.",
    "Document the actual tested platform and synthetic sound boundary without claiming measured fidelity or an untested release."
  ],
  "dependencies": [
    "PHY-001"
  ],
  "required_evidence": [
    "numerical",
    "documentary"
  ],
  "target": "One documented 73-key Mark I Stage specimen at its specified loaded electrical output; identity and measurements pending."
}
```

## Criterion mapping

| Exact criterion | Observed implementation and retained execution | Limitation | Decision |
| --- | --- | --- | --- |
| **HOST-001/C01:** Bounded sample-offset event processing preserves producer order and physical key state; invalid blocks reject without mutation. | `BasicEventProcessor` preflights all offsets, payloads and equal-offset sequence numbers before dispatch. Its 4096-frame/256-event API and the host's 8192-frame/256-note, two-queue limits remain distinct. Host note lists retain order; sustain precedes same-offset notes. Ownership is staged in a copied tracker before recall/commit. Retrigger adds impulses to existing modes; release/pedal changes damping. Retained `equal_offset_order` source fixtures, scalar/partition models, host `invalid_atomicity`, automation/ownership rejection and Q6 recall-order fixtures compare exact state and future output. | No mutation means musical/configuration state; input scratch and permitted rejection-output cleanup can change. An accepted arithmetic fault preserves its successful prefix and latches reset-required rather than rolling back. | Pass |
| **HOST-001/C03:** Host configuration round-trips through a versioned validated state format; MIDI range, sustain, overlapping notes, reset and unsupported host conditions are explicit and tested. | RHST v1 is exactly 16 bytes; model tags 1/2/3 are isolated. Golden bytes, round-trips, all six ordered foreign-model directions, malformed payloads and bounded fragmented/failed streams have retained passing selectors. MIDI 28–100, all 16 channels, anonymous/identified overlap, zero-velocity release, binary sustain, lifecycle reset, deferred recall/abort and unsupported setup/input conditions have explicit source assertions. Processor concurrency fixtures cover ordered races and callback stress. | Recall saves configuration, not ringing mechanics. It installs at an accepted audio boundary or synchronized lifecycle reset. One audio owner, valid accessible host storage and documented lifecycle synchronization remain required. | Pass |

## Evidence applicability

I verified the [native manifest](../../validation/native-platforms-20261124.json),
its archive hash and all ten member hashes. The retained local log records
**288/288 CTests, zero skips**, including the mapped selectors. Each retained
Linux, Windows and macOS VST3 log explicitly passes the same twelve selected
ownership/state/recall selectors; none of those is an architecture skip.
These are inspected execution records, not newly executed tests.

**73 of 74** native source-manifest entries match current bytes. The sole
exception is the later expanded `quadrature_engine_test.cpp`, documented with
native confirmation in [W4-70](20261124-w4-70-native-fault-coverage.md) at
`240170673983f9992e4657a2d96c2172ece1ac30`. All event, ownership, codec,
stream, host, concurrency and Q6-host sources relevant here match the retained
`015767fee5635ff85a717739188ff4af60fe84f2` observation. I do not relabel its
288-test run as an execution of the later fault-test expansion.

Thresholds are exact return/status, wire-byte, ownership, cursor, mechanical-state
and sample comparisons in the cited fixtures, not a relaxed audio tolerance.
The scalar-output assertion compares both channels exactly with the converted
reference. Snapshot and continuation checks detect rejected-state changes;
Q6 recall-order checks preserve/rebuild convolution history explicitly.

Required `project.py validate`, `sync --check`, and `next --json` passed
(14 tasks, 22 evidence records, fidelity unvalidated). Additional work was
read-only source/JSON/log inspection and Python SHA-256 comparison. No Git,
build, test suite, benchmark or DAW operation ran. Public-host behavior beyond
the recorded harnesses, exhaustive concurrency schedules, device deadlines and
physical fidelity remain unestablished. Root should combine this review with
C02/C04 and its final source-bound evidence before any completion decision.

## Current byte identities

These hashes identify reviewed files, not acquisition authenticity. All source
entries also match the native manifest except guide/specification files, which
are bound directly here. The immutable native manifest supplies the wider
74-file list; its one later test-file difference is explicit above.

| Path | SHA-256 |
| --- | --- |
| `include/rhodes/event_processor.hpp` | `36e4b64f3278b0079b20e94a333a657c900e4118dc24433ec9d8c58442651351` |
| `include/rhodes/note_tracker.hpp` | `76931b326daae3a112b56420f9be9ea5b03b1c30307f1dc68b6c13c710cd830f` |
| `include/rhodes/instrument_state.hpp` | `c11c9d982887f4228c76c3ee9e10fe5627c2dc0f1053ad531cfd316285a65616` |
| `src/research_engine.cpp` | `f0facfcb2c28f989c79407d2e0571a868b07ec7a37511bc05f7a33efcdeb0703` |
| `plugins/vst3/processor.cpp` | `97b449dac4e1eb434af8ad4e279bdf7ba07f4668cb174199980eea0266493cf6` |
| `plugins/vst3/state_stream.cpp` | `b5f0dd71c5523f80359027168f0df3307c85d70b3d5654f3f936144f3307d172` |
| `tests/cpp/event_processor_test.cpp` | `f1d9383ad34843dabcca0c58edaecf7cba56b2e19e57b2e065e37c2b5f60793e` |
| `tests/cpp/event_model_test.cpp` | `f6ba26ff7f79a370d730abe4b98a819dcab7a46de7995352468027c8e5186ee2` |
| `tests/cpp/note_tracker_test.cpp` | `96f00bf3fa5a1b54352fae347e8f6cbdfb7f543bed53c15a8885be4400f365e1` |
| `tests/cpp/instrument_state_test.cpp` | `be54581bb3efab5096bfada65e20eec230716f7c2ff110231157d569c080cecc` |
| `tests/cpp/instrument_model_state_test.cpp` | `ab881e0590f3872505231215c95a21ba91de52b719276d93667e6e75122902e7` |
| `tests/cpp/vst3_host_test.cpp` | `ca4a6302eebefee1f59a17118bb29492cc2c534d6d166631475c028bc6127da4` |
| `tests/cpp/vst3_quadrature_contract_test.cpp` | `e6579e640768077e2de380d5daa9b415e99e17b8c9ca781cd5dce6a3b8ce3dd1` |
| `tests/cpp/vst3_state_concurrency_test.cpp` | `175141258725b48497a05e48e9386009143d81ca8460a9a4fa1801bd2a422f99` |
| `tests/cpp/vst3_test_support.hpp` | `7ab3ae32616886bad13f88ffe0d6e2a8409d7b7335529f3e1ca87b8046419a01` |
| `cmake/Vst3.cmake` | `e779743e238c5b7ac2632d73764eb19974fcef3f4ae5c8699ac6f188fde783a6` |
| `docs/specs/HOST_PROTOTYPE_20261123.md` | `d22d1b4757c6a37655f40ae5d1b6b2b41d6d033504cf613526e3827b47a1cd46` |
| `docs/VST3.md` | `9406a1b9883665a040ba58ea488853060526344b09df8c3c27a33f34def62796` |
| `docs/validation/native-platforms-20261124.json` | `3b28ae2ee831786e1f69c8a340f07eb888e6be23743a155f59a014621dbc5f7c` |
| `docs/validation/native-platforms-20261124.tar.gz` | `99655044dd9815379b892f3fddd0e9f5e37496cff794bbbd2bf9bd205b091694` |
