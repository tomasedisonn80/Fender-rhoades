# Final native photo-editor evidence — 24 November 2026

This preserves **W3-003's passing reparent-refresh implementation** and its exact
final Linux/X11/Xvfb observations. The final REAPER run passes its unchanged
assertions, including the first Volume gesture from a cold PointerRoot baseline.
The [manifest](manifest.json) binds every retained file to its original path,
SHA-256, role and source record. Historical attempts retain their own identities.

| Frozen final observation | Result | Native screenshots |
| --- | --- | --- |
| REAPER 7.86, Filtered variant, audio device closed | 21 pass / 0 fail | 17 |
| SDK accepting diagnostic host, direct variant | 27 pass / 0 fail; 63 edit-callback lines | 17 |
| SDK supplemental focus and held-motion probe | 20 pass / 0 fail; 28 edit-callback lines | 8 |
| Standalone XCB observer fixture | 64 passing assertions | — |
| Assigned W3 group 003, final run03 replay | 32 pass / 0 skip | — |

The 32 CTests are a replay of the assigned group, not additional distinct tests
in the wider W3 regression union. The standalone result predates its later CTest
integration. This package preserves the observations; its verifier does not
launch a host or rerun the plug-in tests.

## What the final native runs establish

The first REAPER Volume drag starts with X input focus equal to PointerRoot
(`1`). Only parameter index `0` changes: normalized `0.5` becomes
`0.699999988079071` while held and remains there after release. The final probe
contains no tracing preload. Its assertion-call syntax trees are identical to
the retained W2-020-followup baseline; only its output-directory path changed.
The original ReaScript, Python probe, parameter TSV, launch transcript, loaded
module map, startup captures, configuration and executable acquisition provenance
are preserved. These are visible UI and host-parameter observations with the
audio device closed.

The SDK native and supplemental runs record actual accepted `beginEdit`,
`performEdit` and `endEdit` callbacks. They cover resizing, control gestures,
keyboard feedback, native focus loss, rapid focus out/back, movement while still
held after loss, descendant focus, direct XEmbed deactivation and closing while
held. The standalone fixture covers attachment, removal, independent observers,
bounded event handling, cold ancestor handoff, startup ordering, actual reparent
refresh and destruction. These descriptions are backed by the unmodified raw
records in the archive and the [independent verification record](verification.json).

The SDK full-size idle capture is **1536 × 1152**. Preservation independently
decoded the original WebP, plug-in PNG and final idle screenshot and compared
all **1,769,472 RGB pixels**: zero differ. No crop, mask, registration, resampling
or tolerance was used. The final idle PNG's file SHA-256 is also identical to
the [already retained native idle PNG](../photo-editor-native-20261124/idle-1536x1152.png),
so another copy would add no evidence. The final run's original capture metadata
and source bindings remain separate. REAPER's 1024 × 768 Volume-after-loss,
keyboard-after-loss and final captures each match its own idle frame exactly
across 786,432 RGB pixels.

## Preserved files and deduplication

The [lossless archive](final-native-evidence.tar.xz) is **6,473,736 bytes**.
It stores 108 unique new payload files. Together with content-addressed references
to the immutable [historical W2 archive](../photo-editor-native-20261124/native-photo-editor-evidence.tar.xz),
it reconstructs **166 original files / 51,195,715 bytes**.

All **42 final screenshots** are preserved byte for byte: **30** refer to equal
file bytes already in W2 and **12** are stored here. Each historical reference
pins the archive SHA-256, full member name and member SHA-256. The verifier also
checks those bytes against W2's original manifest. A content match reuses bytes;
it does not treat a historical run as validation of the newer source. Across
all payload types there are 51 historical references; further exact duplicates
inside this package share one archive member. No PNG was recompressed or edited.

The archive also preserves the final helper and fixture source, all other
recorded final non-binary source/resource inputs, exact commands, callback logs,
raw outcomes, W3-003's frozen report and evidence JSON, 32-test JUnit and logs,
SDK diagnostic-host recipe/diff/provenance, and the original environment scripts.
SDK objects, plug-in modules, commercial REAPER binaries, the diagnostic preload
binary, installed REAPER asset trees and license registration files are excluded.
Their relevant executable hashes remain in the raw provenance. The package is
an evidence snapshot, not a complete dependency checkout.

| Deliverable | Purpose |
| --- | --- |
| [manifest.json](manifest.json) | Per-file identities, immutable archive/member references, source bindings, commands via raw records, failures and scope |
| [payload.sha256](payload.sha256) | Checks all reconstructed original files |
| [verify.py](verify.py) | Reconstructs and verifies hashes, source bindings, raw callbacks, assertions and pixel comparisons |
| [verification.json](verification.json) | Recorded independent preservation checks, including every capture's file/RGB hash |
| [SHA256SUMS](SHA256SUMS) | Checks the files delivered in this directory |

Archive SHA-256:

```text
6facd7d532eba1d22096e5dcc2b624e4718eb45801afc2f5596014d651303c6f
```

## Source and executable identities

| Final item | SHA-256 |
| --- | --- |
| `photo_native_focus.cpp` | `c24f8d70731a193b0b7bf833984c00df5a60375d2d306986f1440757af938c5f` |
| `photo_native_focus.hpp` | `9bf4553ec6a1418747d5379ba531bf15bbfb7e589f3923ff9c8131485704396a` |
| Standalone repository fixture | `09b125635cc9ce89770d96ea6d9395e19ccc28b7deb199b0c42f7d9233b34144` |
| Direct module | `d55b7427a770657ef5bd7874f5cb10ab8e4c0325466492ffea0e2eb4c027b2e3` |
| Filtered module | `504d99b120c98bb69bca672d7b52161a05e529fe410bc2ebfc62b58b7d0e0705` |
| Quadrature module | `9d1e4ccfe4f042388b876d4c16ccac156f546d5aa9959eb9b908bdf7fef545e5` |
| REAPER 7.86 executable | `5a3e7719e21f1f29c18aabae13ea683108e1ffdfbbbbbe70f39590b340d44482` |
| Accepting diagnostic SDK host | `37f557e9863001b54b158482338ff66dcf9fb3934ba12d8ace0c9ddbbb0eebe9` |
| Native full-size idle PNG | `5167f25366c8e5d8e7eda298bf089af18478ad5e28ccf78ae02531a2c7fff140` |
| Full-size decoded RGB | `9ca2f5bb49d0bebeb0f9d779753b7f5c357d8a76f6d995db75b528aca5f4cdd3` |

REAPER's before/after source and module hashes match. SDK outcomes likewise
record unchanged inputs. The verifier checks each preserved final non-binary
input against the original raw result. Integrator revision `3c619b7` is retained
as contextual provenance; the per-file hashes identify the tested source.

## Verify or reconstruct the evidence

Keep this directory and `photo-editor-native-20261124` as siblings. Python 3.11+
and Pillow are sufficient for the verifier; it does not require the SDK, X11,
REAPER or the original task paths. Run from this directory:

```sh
sha256sum --check SHA256SUMS
python3 verify.py
```

To reconstruct every original file, including all 42 PNGs, into a new directory:

```sh
RHODES_NATIVE_CHECK_ROOT=$(mktemp -d)
python3 verify.py --output-dir "$RHODES_NATIVE_CHECK_ROOT/evidence"
cd "$RHODES_NATIVE_CHECK_ROOT/evidence"
sha256sum --check payload.sha256
```

Reconstruction requires about 52 MB outside the repository. An existing output
directory is refused. Raw files retain their original bytes and absolute paths;
the reconstructed directory names are the manifest's portable logical paths.
Use `--historical-dir PATH` if W2 is stored elsewhere, or `--json-output PATH`
to record a fresh verification report. The archive by itself omits deduplicated
files; use the verifier when a complete extracted run directory is needed.

The verifier checks archive identity and member metadata, every logical file,
all 42 PNG file/RGB identities, 156 retained source references, the unchanged
REAPER assertion ASTs, cold Volume values, stable source/module identities, raw
SDK callback sequences, standalone/CTest results and preserved failure counts.
It independently repeats the full-image and cleanup-image comparisons.

## Commands and executed-host reproduction

The recorded platform is `Linux-6.18.57-x86_64-with-glibc2.41`, Debian 13, X11
under Xvfb without a window manager. Native probes record Python 3.12,
Pillow 13.0.0 and python-xlib 0.33. REAPER provenance records
`7.86/linux-x86_64 rev f6d046 (Nov 13 2026)` and the official archive hash/URL;
the distribution must be obtained separately. SDK checkout and native build
setup are described in the [VST3 guide](../../VST3.md) and retained diagnostic
recipe. SDK root pin: `3cdf9ca5d1f5b1b21e0a86832aa4abe55607bd96`.

After reconstruction, these are the exact original command records:

| Operation | Preserved logical path |
| --- | --- |
| SDK native harness | `final/sdk-native/command.json` |
| Supplemental focus/motion harness | `final/focus-motion/command.json` |
| Actual REAPER launch and probe | `final/reaper/run-command.json`, `final/reaper/run.json` |
| Standalone C++20 compile | `standalone/w3-003-repository-focus-test-command.json` |
| Production-flags and non-Linux-branch compile checks | `standalone/w3-003-compile-commands.json` |
| Exactly 32 selected CTests | `ctest/w3-003-ctest-run03-result.json`, `ctest/w3-003.txt` |

The original REAPER wrapper command was:

```sh
python3 /home/agent/rhodes-7a7c13ab7da3/with-xvfb.py \
  python3 /home/agent/rhodes-7a7c13ab7da3/w3-003-reaper-run03/probe_reaper.py
```

The SDK/motion command records include the original `tools/verify_native_photo_editor.py`
or `focus-motion-probe.py` invocation, direct bundle, accepting host, extra source
inputs and output path. Their wrapper used the preserved `with-xvfb.py` with an
**in-memory `1920x1440x24` screen override**; the saved wrapper defaults to
`1280x900x24` for REAPER. The standalone command links the retained fixture and
helper with `-lxcb`, and runs under an isolated X server.

For a new executed-host replay, adapt working copies of the absolute-path
scripts and environment recipes to a new checkout/dependency root, use new
empty output directories, rebuild from the recorded source, and record new
module/source hashes. Preserve these archived originals. No accepting host,
SDK object, REAPER distribution or plug-in binary is bundled here.

## Failures and causal trace remain visible

Three ordinary/diagnostic intermediate REAPER runs each report 20/21, with the
cold Volume failure preserved in their original `run.json` and analysis.
The first intermediate SDK motion run reports 18/20: its reset after quick
return emitted balanced begin/end callbacks without a value callback, and the
following gesture started at 0.6 and reached 0.8. The unchanged later runs pass;
no cause for that intermediate reset anomaly is claimed.

Differential standalone logs preserve a cold-handoff failure in the W2 helper,
a startup-order failure in the first W3 helper, and a reparent-refresh failure
in the next helper. Their helper snapshots remain separate. The first two
intermediate SDK outcomes reference an earlier repository fixture whose exact
snapshot is absent; those two historical input hashes are explicitly marked
`hash_only_historical_input`. Every final non-binary input is preserved.

The compact original causal trace and interposer source live at
`historical/trace/w3-003-reparent-causal-trace.log` and
`historical/trace/w3-003-xcb-trace.cpp` after reconstruction. The trace observes
the embedding container being reparented into the floating host after initial
attachment. The prior helper invalidated topology permanently; the final helper
cancels immediately and refreshes once on the next same-window attachment.
The passing final run uses no interposer. The original W3-003 report explains
the event sequence and the remaining topology boundary.

## Scope limits

These observations qualify the recorded **bare-Xvfb Linux/X11 topology**.
There is no native Windows, macOS or Wayland result in this package, no
desktop-wide or arbitrary docked-host qualification, and no guarantee for the
extra frame a desktop window manager may insert. X-server request counts are
bounded, but synchronous reply latency is not time bounded.

The accepting SDK host supplies edit callbacks and has no audio-processing
loop. **REAPER's audio device was closed**: direct REAPER `endEdit` and processor
note-off delivery were not observed. Visual key release and host parameter
stability do not establish note-release audio timing, device deadlines or
physical-instrument fidelity. The non-Linux compile was a preprocessor smoke
check on Linux. Later source changes require their own bound results.
