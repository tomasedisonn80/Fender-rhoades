# W5-83 — HOST platform and bounded-processing review

Date: 2026-11-24. Agent: `/root/w5_83_host_platform_contract`.

**Decision: HOST-001/C02 and C04 pass for the declared experimental prototype.** This independent review does not decide C01/C03 or complete the task. The integrator must combine those reviews and register the required evidence.

## Frozen definition

Recomputed from state revision 27 using UTF-8, sorted keys, compact separators and `ensure_ascii=False`: SHA-256 `b5e43a5c4bfccd246d49bf60783b320a1629741489023757d01cb50207e48336`.

```json
{
  "task_id": "HOST-001",
  "acceptance": [
    "Bounded sample-offset event processing preserves producer order and physical key state; invalid blocks reject without mutation.",
    "An optional VST3 instrument builds against an explicitly identified public SDK and passes the available SDK validator with bounded audio processing and documented output scaling.",
    "Host configuration round-trips through a versioned validated state format; MIDI range, sustain, overlapping notes, reset and unsupported host conditions are explicit and tested.",
    "Document the actual tested platform and synthetic sound boundary without claiming measured fidelity or an untested release."
  ],
  "dependencies": ["PHY-001"],
  "required_evidence": ["numerical", "documentary"],
  "target": "One documented 73-key Mark I Stage specimen at its specified loaded electrical output; identity and measurements pending."
}
```

`PHY-001` is done. Its completion does not supply specimen measurements.

## Criterion mapping

| Criterion and exact text | Evidence and observed coverage | Limitation | Decision |
| --- | --- | --- | --- |
| **HOST-001/C02:** An optional VST3 instrument builds against an explicitly identified public SDK and passes the available SDK validator with bounded audio processing and documented output scaling. | `cmake/Vst3.cmake`, `processor.cpp/.hpp`, `instrument_state.hpp`, `vst3_host_test.cpp`; [native evidence](../../validation/native-platforms-20261124.json) and independently retrieved current CI below. Optional configuration verifies SDK root `3cdf9ca5d1f5b1b21e0a86832aa4abe55607bd96` plus four exact submodule pins. All three bundles pass SDK and metadata validators on each OS. Fixed bounds, allocation guards and exact scalar output checks support the declared processing/scaling contract. | Foreign host getters and pointer validity remain host responsibilities; bounded work is not a device-deadline guarantee. | **Pass** |
| **HOST-001/C04:** Document the actual tested platform and synthetic sound boundary without claiming measured fidelity or an untested release. | Current `README.md`, `docs/VST3.md`, `docs/DEPENDENCIES.md`, `RIGHTS.md`, retained native evidence and W4-68/70/71/72/75/76. Guides identify tested revisions, platform skips, separate variant identities, synthetic parameters and offline contact. | Actual DAW sessions, portable deployment baselines, signing/notarization, distribution approval and measured fidelity remain unestablished. These are explicit boundaries, not additional prototype criteria. | **Pass** |

## Source and execution applicability

Compared all 74 current files with the retained `015767fee5635ff85a717739188ff4af60fe84f2` native manifest: 73 match; only `tests/cpp/quadrature_engine_test.cpp` differs through the later fault-coverage extension. The retained archive and all ten member digests match. These observations bind relevant unchanged production/build/host-test bytes without claiming that the entire current checkout was tested at the older revision.

Independently retrieved the job records and complete decoded logs for [VST3 run 45340923158](https://github.com/baabi-boo/Fender-rhoades/actions/runs/45340923158), all bound to `240170673983f9992e4657a2d96c2172ece1ac30`. Each job built, ran six SDK/metadata validator CTests, archived and uploaded successfully. Scalar-output and allocation-guard selectors passed for all three variants.

| Job | Observed platform/compiler | CTest passes / skips / registered |
| --- | --- | --- |
| `138264122400` | Ubuntu 26.04 x64; GNU 15.2.0 | 138 / 0 / 138 |
| `138264122571` | Windows Server 2025 x64; MSVC 19.52.37029.0 | 134 / 0 / 134 |
| `138264122093` | macOS 26 ARM64; AppleClang 21.0.0.21000101 | 125 / 9 / 134 |

The nine macOS skips are the three variants' x86 FTZ/DAZ modes. These are CTest counts, not counts of internal SDK assertions. This review does not identify remote artifact bytes from the separately inspected local Linux bundles.

Source inspection confirms at most 8192 host frames, 256 input events, two parameter queues with 8192 points each, and 256 combined note/pedal events per sample. Scratch arrays are fixed; mailbox operations use lock-free 64-bit atomics and single compare/exchange attempts. Conversion is `(32 * normalized_gain) * synthetic_harp_volts`, default multiplier 16. Scalar fixtures demand exact converted sample equality at 32/48/96/192 kHz in both output formats; they independently schedule the selected underlying engine rather than independently validate its physics.

The tested macOS workflow selects Unix Makefiles. The pinned SDK's Xcode branch adds `-ffast-math -ffp-contract=fast`; that configuration needs its own verification. Project MSVC targets request `/fp:precise`, while SDK target settings differ. Local Clang pragmas do not establish correctness under arbitrary external flags. SDK notices are packaged, and the original project's license remains an owner decision.

## Checks and handoff

Executed `python3 tools/project.py validate`, `sync --check`, and `next --json`: valid 14 tasks/22 evidence records, generated state current, fidelity unvalidated. Remaining work here was source/document inspection, SHA-256 comparison, tar-member reads and read-only CI retrieval. No build, test suite, numerical replay, DAW, Git operation, ledger or canonical-state edit occurred. Only this report was written. New platform/flag or distribution claims need corresponding new evidence; no additional prototype gap was found.

## Examined SHA-256 identities

The native JSON supplies the complete 74-file baseline; the selected current identities below bind this review. The sibling SDK path is explicitly external to the repository.

| File | SHA-256 |
| --- | --- |
| `CMakeLists.txt` | `2a70fba5924a3eeb01bf0dfd7c2a0f6a00726d27ac7535660a4bf267cf81ff3e` |
| `cmake/Vst3.cmake` | `e779743e238c5b7ac2632d73764eb19974fcef3f4ae5c8699ac6f188fde783a6` |
| `.github/workflows/vst3.yml` | `6cf2593353d05a6b122c99736399132b4c813ec8fba5af03b39ed4d5fdd8e3f3` |
| `plugins/vst3/processor.cpp` | `97b449dac4e1eb434af8ad4e279bdf7ba07f4668cb174199980eea0266493cf6` |
| `plugins/vst3/processor.hpp` | `0250eaaca92b2aa904b5a05e8aca8cc46cd8a63111d145ddf7d0956f682d1807` |
| `tests/cpp/vst3_host_test.cpp` | `ca4a6302eebefee1f59a17118bb29492cc2c534d6d166631475c028bc6127da4` |
| `docs/VST3.md` | `9406a1b9883665a040ba58ea488853060526344b09df8c3c27a33f34def62796` |
| `docs/DEPENDENCIES.md` | `2da17d8502587a45f83c900633a0a53f1fcbaf60a901bb589370b61c773b2126` |
| `RIGHTS.md` | `b9ceeec2a73487a305be1c0fb579a52ec3b25efd81301983f53d08b3f16562af` |
| `docs/validation/native-platforms-20261124.json` | `3b28ae2ee831786e1f69c8a340f07eb888e6be23743a155f59a014621dbc5f7c` |
| `docs/validation/native-platforms-20261124.tar.gz` | `99655044dd9815379b892f3fddd0e9f5e37496cff794bbbd2bf9bd205b091694` |
| SDK `../vst3sdk/cmake/modules/SMTG_PlatformToolset.cmake` | `57f94d9fc125aae7b1e5c790a62a740ec7d843c485251761cea008cc5c6d6ef2` |
