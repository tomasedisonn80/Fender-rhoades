# Historical native photo-editor evidence — 24 November 2026

This preserves the **W2-013 checkpoint from 15:42–15:43 UTC**, before subsequent
Wave 3 focus changes. The actual direct VST3 editor ran on Linux/X11 under Xvfb
with an isolated SDK diagnostic host. This archive preserves observations and
source identities; it does not validate later source revisions.

| Frozen run | Recorded checks | Edit callback lines | Native PNG captures |
| --- | --- | --- | --- |
| `sdk-native-run02` | 27 pass / 0 fail | 63 | 17 |
| `focus-motion-run02` | 20 pass / 0 fail | 28 | 8 |

The captured full photograph has **1536 × 1152 pixels**. Preservation independently
redecoded the original WebP, the plug-in reference PNG and the idle capture:
all **1,769,472 RGB pixels match**, with zero differing pixels. This comparison
uses no crop, masking, registration, resampling or tolerance. The source file
bytes and the capture file bytes differ because their encodings differ.

[View the unchanged native idle capture](idle-1536x1152.png).

## Files and integrity

- [Lossless evidence archive](native-photo-editor-evidence.tar.xz): **10,289,820 bytes**;
  xz, one thread, 64 MiB LZMA2 dictionary.
- [Manifest](manifest.json): all **56 original payload files**, their original
  absolute paths, archive paths, byte counts, roles and SHA-256 hashes.
- [Payload checksums](payload.sha256): checks all payload files and the manifest
  after extraction. [Delivery checksums](SHA256SUMS) cover the files in this folder.
- [Verification record](archive-verification.json) and
  [extraction check output](extract-sha256-check.log): every extracted payload
  matched its original bytes, all 25 capture file/RGB hashes matched their
  recorded results, and both callback logs matched the recorded callback sequence.

The archive retains both complete final run directories; the original
`w2-013-evidence.json`; the focused native C++ test, final 21-case log and exact
compile commands; the supplemental motion probe; the worker's detailed report;
and the isolated accepting-host recipe, diff, commands and provenance. Original
failed run01 outcome JSON/logs and the initial helper/test/regression log remain
separately labeled. Run01 screenshots are outside this archive. Original paths
inside all raw JSON/log/source files are unchanged.

The archive also retains the reference WebP/PNG, unchanged recorded repository
inputs and environment recipes. It includes **no SDK objects, SDK host binary,
plug-in module binary or commercial REAPER binary**. Container metadata is
normalized for reproducibility; every original payload byte is unchanged.

Archive SHA-256:

```text
698e3fd85c8880be260ae847778b64f16c150fe288b2a0e9b357b0a47fe3fa8c
```

## Original checkpoint identities

| Input or observation | SHA-256 |
| --- | --- |
| SDK final outcome | `1d7cce344500927bb9c23b4a39f8f4fbbbc939d3c3deead99fd96abb0379626e` |
| Focus-motion final outcome | `f58d094b7c03fd8e511d744fd36befaa2bd65c04facbd27f23dbf96dabc8b268` |
| Original WebP | `782b4c83a2bf827ed0d363bc567cdc2211485da6e650db4ae99448e78b7da24b` |
| Reference PNG | `84e2d48fc0311eb923083cc3e1f96dc5fa1b3f8a6e085daee7c973e54d754d26` |
| Native idle PNG | `5167f25366c8e5d8e7eda298bf089af18478ad5e28ccf78ae02531a2c7fff140` |
| Decoded full-image RGB | `9ca2f5bb49d0bebeb0f9d779753b7f5c357d8a76f6d995db75b528aca5f4cdd3` |
| Tested direct module | `ae3b732ca33e4fdca57367e6b1beafac28bd78a4f619cadad93ae7c4e0fa7306` |
| Accepting diagnostic host | `37f557e9863001b54b158482338ff66dcf9fb3934ba12d8ace0c9ddbbb0eebe9` |
| W2 `photo_native_focus.cpp` | `3b4fb4a575f306bfc7363884cdd2dbaca55cb32d47e0b69e0cbada52d055c8ed` |
| W2 `photo_native_focus.hpp` | `fbcc4ba33dc8e779e39d441ef3bd67d76596c9a2cdb90f20631d1acb519084e9` |

The two focus-helper production files had already changed during Wave 3 when
packaging began. Their newer contents are deliberately excluded from the W2
repository snapshots. The manifest records this omission; the verification
record includes the old and observed newer hashes. After this archive was frozen, the exact W2 helper files were recovered from
W3-003's pre-edit backup. They accompany it as
[historical C++ source](historical-source/photo_native_focus.cpp) and
[historical header](historical-source/photo_native_focus.hpp); both match the
old hashes in the table above. These two supplements are outside the immutable
archive and covered by the delivery checksums. The archive remains a checkpoint
evidence package, not a complete standalone build checkout.

## Verify and reproduce

From this directory, verify the deliverables, extract to a new scratch directory,
and check every payload:

```sh
sha256sum --check SHA256SUMS
RHODES_EVIDENCE_CHECK_DIR=$(mktemp -d)
tar -xJf native-photo-editor-evidence.tar.xz -C "$RHODES_EVIDENCE_CHECK_DIR"
cd "$RHODES_EVIDENCE_CHECK_DIR/photo-editor-native-evidence"
sha256sum --check payload.sha256
```

For an executed editor replay, use the [VST3 build instructions](../../VST3.md)
and the recorded source identities. The SDK root pin is
`3cdf9ca5d1f5b1b21e0a86832aa4abe55607bd96`. The retained
`w2-001/build-diagnostic-host.py` expects a sibling `vst3sdk` checkout and a
configured Ninja `host-build`; it compiles an isolated host translation unit and
links against the separately built SDK. Its diff changes **only `beginEdit`,
`performEdit` and `endEdit`**, accepting edits and writing callback lines.
Original SDK and host-build source files remain separate.

The retained `worklogs/w2-013.md` contains the exact original run commands,
including the in-memory 1920 × 1440 × 24 Xvfb adjustment. Use an isolated X11
display with XTEST, Python 3.12, Pillow 13.0.0 and python-xlib 0.33. Run the
repository's `tools/verify_native_photo_editor.py`, then the archived
`w2-013/focus-motion-probe.py`, into **new empty output directories**. The original
environment, compiler and probe scripts contain task-specific absolute paths;
adapt working copies to the new checkout and dependency installation, preserving
these archived originals. Retain new source/binary hashes with every replay.

## Observed boundary

The final native records cover photo identity, accepted parameter gestures,
held-key visual release, external focus loss, rapid focus out/back, held movement
after loss, descendant focus, explicit XEmbed deactivation and close while held.
The separate C++ observer log records 21 passing cases. The diagnostic host has
**no audio-processing loop**. These results do not establish processor note-release
timing, REAPER or other named-DAW acceptance, native Windows/macOS/Wayland behavior,
hardware deadlines or measured instrument fidelity. Newer Wave 3 fixes require
their own source-bound results.
