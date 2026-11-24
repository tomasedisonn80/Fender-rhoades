# Photo-editor legacy audio evidence — 24 November 2026

The fresh official Linux comparison **passed**: **216 valid captures, 144 exact
baseline/current pairs and 36 passing dense partition groups**, covering
**608,256 paired stereo frames**. This directory preserves the complete original
records and raw samples. Its offline verifier reproduces the recorded validation
and byte-comparison outcomes without loading a plug-in or executing audio.

The earlier W4-001 official gate remains **failed**. Its report, diagnosis,
commands, metadata and all raw audio are preserved under their original run
identity. The two outcomes must not be merged into one result.

| Recorded official run | Valid captures | Exact passing pairs | Passing dense groups |
| --- | ---: | ---: | ---: |
| W4-003, fresh corrected validator | 216 / 216 | 144 / 144 | 36 / 36 |
| W4-001, original stale validator | 72 / 216 | 0 / 144 | 12 / 36 |

Both matrices used three variants, binary32/binary64 stereo, 48/192 kHz,
whole/fixed64/ragged callback partitions, dense/sparse gain workloads, and
baseline/default/explicit-unity modes. There are 72 fixtures and 4,224 stereo
frames per capture. The paired-frame total is 144 × 4,224; it counts each
baseline/current comparison, including the shared baseline in two pairs.
W4-003 validates 7,992 callbacks. Dense workloads compare all three partitions;
sparse gain endpoints depend on callback size and are compared only within a
matching partition.

## Files and lossless preservation

The [archive](legacy-audio-evidence.tar.xz) is **2,376,016 bytes**. It contains
**1,376 regular files / 34,441,104 uncompressed bytes**, including both complete
run directories. Every successful raw sample, JSON trace, empty stderr file,
command record, raw manifest and immutable report is retained byte for byte.
XZ compression efficiently represents repeated bytes; extraction needs no
historical archive or content-reconstruction scheme.

Archive SHA-256:

```text
b9dfae3c976d638ba25c8375d7653b83c5f47410958430c997cd9ae61db42b8c
```

| File | Purpose |
| --- | --- |
| [manifest.json](manifest.json) | Each file's original path, byte count, SHA-256, role and source binding; archive and binary identities |
| [payload.sha256](payload.sha256) | Checks every extracted original file |
| [verify.py](verify.py) | Checks the archive and source snapshots, replays the frozen comparator functions, optionally extracts all files |
| [verification.json](verification.json) | Recorded offline recomputation and preservation result |
| [extraction-verification.json](extraction-verification.json) | Independent `sha256sum` verification of extracted files and its exact command |
| [extraction-sha256-check.log](extraction-sha256-check.log) | Full successful extracted-file checksum output |
| [SHA256SUMS](SHA256SUMS) | Checks this delivered directory |

The archive includes the full baseline build/source/SDK diagnostics; before and
after metadata; exact configure, build, native comparison and regression command
records; the original worker reports and evidence JSON; all 41 relevant tested
source/resource fingerprints as exact snapshots; the tested Python regression
file; the two original pre-fix Python files; and the exact two-file correction
patch. Native modules, capture executables, SDK objects and dependency checkouts
are excluded. The `.bin` members are the recorded stereo sample data, not native
executables. The seven native executable identities remain in the provenance.

The full original run directories after extraction are:

```text
w4-003/comparison/run-20261124T162117-9210d316775c454f9b9434d3fc5659d0/
w4-001/comparison/run-20261124T161715-c4db22a5c2f04d73ad3bdb3b85c95069/
```

Each contains its original 649-entry raw manifest and per-run report. The
published and per-run copies of each report are identical. Their SHA-256 values
are:

| Report | SHA-256 |
| --- | --- |
| W4-003, passed | `680b546fb7925272b899f551bd76fa6fec1ebbc42ae1f4f0bba96008cbb9dd44` |
| W4-001, failed | `a372fe9c746be94621842d38a867e58a30dd5a42d0725e4b4185af0740b6821a` |

## Source checkpoint and native binary identities

The successful capture used **HEAD
`3c619b74a0e60e5daf1debad6164603d8d43af5d` plus an uncommitted two-file patch**.
It was not a clean run of a later commit. That patch has SHA-256
`4a48f29ef0ccc6e87b070a6bfea88bd9447e8d3182ed33097f8bfcb5b19ec5da` and changes
only `tools/compare_vst3_legacy.py` and its Python regression test.

The later commit `5f1aaf27b3c34325886fc352dc3430f5e240ae17` contains exactly that
patch: a read-only comparison of its complete two-file diff reproduced the
captured patch bytes and SHA-256. The archive's source snapshots were retrieved
from that commit and checked against every capture-time hash. This is a source
identity bridge, not a new audio run at the committed revision. Subsequent
CMake test-registration changes are outside this capture and its tested patch.
The original capture-time `CMakeLists.txt` is retained separately in the source
snapshot.

| Python source | Before-fix SHA-256 | Tested SHA-256 |
| --- | --- | --- |
| Comparator | `b0f97f7ee8ef989a16cd15ea92fc54939bbd9cf2677dd313cea377df50af8754` | `dde69e127376fd6c24d63fc9e260d036151d30eb8ad73a1bd29e5def09e307d5` |
| Regression test | `67334296120635093cb95aa677cb6dcb0e0a2aea94223891b965098e08ad3826` | `b064b26cf40620c231039f91f60690693c2ff695d786d8dc740cb7b141dd45ab` |

The baseline is frozen revision
`6c91dab00855b67a559e92edb79ae49e567be469`. Its comparator input manifest has
SHA-256 `d8c960d5e8e7d24882450afd2011db7429d41284994e10db66d949190b8438a4`.
The manifest and its complete linked diagnostic inventory are both retained.
The native recorder, module bundle inventories and baseline manifest are
identical between W4-001 and W4-003; no binaries were rebuilt for the correction.
Each run records unchanged relevant inputs during its comparison. The sole
relevant-source difference between the two runs is the Python comparator; its
test file is separately bound by the patch and preflight records.

| Native input | SHA-256 |
| --- | --- |
| Baseline RhodesResearch | `e4f9812a59ef1bb5168ad9fdca61d0aba94988ca4f413c98a013fae87a3038bf` |
| Baseline RhodesResearchFiltered | `a410ba3073e99335187d711a3c5effaff197962afbdc43207175c511b63b9b08` |
| Baseline RhodesResearchQuadrature | `1deda8ab23b8b21ec26ab353eb497ad7bd703df024330527c7736be09eca5739` |
| Current RhodesResearch | `d55b7427a770657ef5bd7874f5cb10ab8e4c0325466492ffea0e2eb4c027b2e3` |
| Current RhodesResearchFiltered | `504d99b120c98bb69bca672d7b52161a05e529fe410bc2ebfc62b58b7d0e0705` |
| Current RhodesResearchQuadrature | `9d1e4ccfe4f042388b876d4c16ccac156f546d5aa9959eb9b908bdf7fef545e5` |
| SDK-only native capture executable | `bf683267d1a4ab2f99a1090b1683a498a10db24faaf753cdd5fafb7f2105e197` |

Build provenance records Linux x86-64, GNU 14.2.0, Ninja, Release and
`-O3 -DNDEBUG`; the baseline and candidate toolchain comparison passed. SDK root
revision is `3cdf9ca5d1f5b1b21e0a86832aa4abe55607bd96`; all recorded submodule
identities and source/build inventories remain in the original JSON. Observed
source hashes do not by themselves attest that those sources produced a binary;
the module and recorder hashes bind what the native command executed.

## Exact successful command

The working directory was
`/workspace/scratch/7a7c13ab7da3/Fender-rhoades`. The original command was:

```sh
python3 tools/compare_vst3_legacy.py \
  --capture /home/agent/rhodes-7a7c13ab7da3/build-editor/rhodes_vst3_legacy_capture \
  --baseline-root /home/agent/rhodes-7a7c13ab7da3/legacy-build/VST3/Release \
  --current-root /home/agent/rhodes-7a7c13ab7da3/build-editor/VST3/Release \
  --output-dir /home/agent/rhodes-7a7c13ab7da3/w4-003/comparison \
  --sdk-root /home/agent/rhodes-7a7c13ab7da3/vst3sdk \
  --source-root /workspace/scratch/7a7c13ab7da3/Fender-rhoades
```

`w4-003/legacy-compare.command.json` retains the exact argument array, working
directory, wrapper timestamps, exit code 0 and log hash. The official report
records `2026-11-24T16:21:17.450680+00:00` through
`2026-11-24T16:21:20.437968+00:00`; its wrapper completed at
`2026-11-24T16:21:20.500659+00:00`. Every native invocation and fresh capture ID
is in the raw `commands.json`.

The pre-fix regression failure and later 23-test pass are separately retained.
Their command records preserve the exact selected test before the fix and
`python3 tests/python/test_compare_vst3_legacy.py -v` after it. Preservation did
not rerun those tests or any native command. Re-executing the native matrix would
require the separately obtained SDK, build environment and recorded input
binaries, or newly built binaries with their own identities and new output
directories. The archive is sufficient to recompute comparisons of the retained
samples without those dependencies.

## Why the first gate stays failed

The old Python validator expected exactly **19** parameters for each current
mode. The photo-panel candidate correctly exposed **24**, and the native
recorder already required 18 for baseline and 24 for current. All 216 native
processes exited zero, but Python accepted only the 72 baseline captures and
rejected all 144 current captures with `invalid capture field: parameter_count`.
The official gate therefore passed no pairs. Its 12 passing dense groups were
the baseline groups only.

The correction requires integer 18 for baseline and integer 24 for default and
explicit-unity modes. The retained regression checks reject obsolete 19 and
other incorrect counts, including numeric values of the wrong type. The fresh
run uses 216 capture IDs disjoint from the failed run. It does not reuse or
reclassify the old captures.

A separately labeled original diagnosis found 144/144 old raw audio pairs equal
and matching legacy metadata after excluding the four declared mode differences:
`capture_id`, `mode`, `parameter_count`, and `expression_points`. The offline
verifier reproduces that diagnosis as well as the official failure. It also
checks the complete 652-file pre-fix failure inventory, and directly compares
all 216 old/new audio files: each matching fixture is byte-identical. These
supplemental comparisons do not convert W4-001 into a passed official gate.

## Verify and extract

Run from this directory with Python 3.11+ on a little-endian machine. Only the
Python standard library is needed:

```sh
sha256sum --check SHA256SUMS
python3 verify.py
```

The verifier checks the archive's regular-file inventory and normalized member
metadata, every file's SHA-256 and length, both 649-entry raw manifests, frozen
source/patch identities, and the original failure inventory. It loads the two
SHA-pinned comparator snapshots and calls their validation/comparison functions
on retained JSON and sample bytes. It checks sample finiteness, exact stereo
bits, nonvacuous musical epochs, silence tails, legacy metadata/state checkpoints,
all 144 pair comparisons and all 36 dense groups. The original native commands
are data and are never executed.

To extract every file into a new directory outside the repository:

```sh
RHODES_LEGACY_CHECK_ROOT=$(mktemp -d)
python3 verify.py --extract-to "$RHODES_LEGACY_CHECK_ROOT/evidence"
cd "$RHODES_LEGACY_CHECK_ROOT/evidence"
sha256sum --check payload.sha256
```

Extraction needs about 35 MB and refuses an existing destination. Original
records keep their absolute historical paths and bytes; archive member paths
are portable. Use `--json-output PATH` to write a fresh verification record.
The retained extraction check passed all 1,376 file hashes with exit code 0.

## Scope

This establishes **numerical digital equivalence for the recorded valid stereo
timelines on the retained Linux build pair**. It does not establish physical
instrument fidelity, playback-device audibility, named-DAW operation, mono or
malformed-input behavior, real-time deadlines, or another native platform.
Preservation verifies byte integrity and recomputes the frozen protocol; hashes
are not an independent authentication of the original execution. Later source
or build changes require their own bound results.
