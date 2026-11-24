# Photo-editor regression evidence — 24 November 2026

**Verified retained result: 523 unique registered tests passed across 18 assigned groups; 0 failures, 0 skips, 0 disabled cases, 0 missing names, and 0 duplicates.** This is an aggregation of existing Linux software/numerical regression and host-fixture results. W4-004 ran no tests, rebuilt no targets, and changed no production source, CMake configuration, Git state, or canonical project state.

The result is bound to the source and binary observations retained by each group. The groups span native-focus helper changes and are not represented as one globally frozen build. The subsequent helper replays and ownership-fixture warning fix are separately retained and contribute no additional unique tests.

## Portable record and integrity

The [raw evidence archive](regression-evidence.tar.xz) is **137,196 bytes** and contains 116 original evidence/source-observation files, plus an origin index, a derived verification record, and an inner `SHA256SUMS` manifest. Every member is an ordinary, non-executable file; no binaries or native host capture trees are included. Its SHA-256 is `c71225f14b2a589072ac5b3585c970eb7fb1c21e3bf082756e2e79efdb5aa203`.

The [machine-readable summary](summary.json) preserves exact test names, commands, timestamps, source/module identities, per-group hash limits and the verification result. [manifest.sha256](manifest.sha256) binds this report, the summary and the archive. Inside the archive, `archive-index.json` maps every original file to its original path, byte count, SHA-256 and filesystem mtime. Mtimes are metadata, not claimed execution timestamps.

Aggregation recorded at `2026-11-24T16:25:16.991460+00:00`. The original files were copied byte for byte, with source and copy hashes checked for equality. All 118 inner-manifest file hashes were checked by reading the completed archive. The archive was also reopened to reconstruct the 523-name JUnit union independently of the copy-stage summary. 62 retained evidence references to packaged original artifacts matched their recorded hashes.

To verify the delivered bytes, run these commands from this directory. Extraction uses a new temporary directory; the archive itself contains only relative file paths.

```sh
sha256sum --check manifest.sha256
archive_check_dir=$(mktemp -d)
tar -xJf regression-evidence.tar.xz -C "$archive_check_dir"
(cd "$archive_check_dir" && sha256sum --check SHA256SUMS)
```

## Exact coverage and counted captures

The planning record assigns the 523 names from `planning/w3-ctest-inventory.json` exactly once to `selections/w3-003.txt` through `selections/w3-020.txt`. The aggregation compares each selection with its JUnit testcase multiset and dedicated text log, checks each suite and testcase for failures/errors/skips/disabled status, verifies the recorded zero process exit, and compares the complete union with the inventory. No shared `Testing/Temporary` output is used as authority.

The primary capture for group 003 is the original `w3-003-ctest.xml`, `w3-003-ctest.log` and `w3-003-ctest-result.json`. Groups 004–020 use their dedicated `w3-NNN.xml`, `w3-NNN.log` and original evidence JSON. Each original filename is retained under `groups/w3-NNN/`. The later narrative `groups/w3-003/w3-003.md` describes final run03; it does not replace or relabel the initial primary capture.

| Group | Assigned software scope | Passed | JUnit timestamp, as recorded |
| --- | --- | ---: | --- |
| w3-003 | Photo editor, front panel, and bundle assets | 32/32 | `2026-11-24T09:03:39` |
| w3-004 | Audition ownership and plugin-state transactions | 20/20 | `2026-11-24T08:59:07` |
| w3-005 | Core state and control representations | 42/42 | `2026-11-24T08:59:54` |
| w3-006 | Physical core and transition cache | 21/21 | `2026-11-24T09:01:48` |
| w3-007 | Contact and grazing reference mechanics | 41/41 | `2026-11-24T09:03:14` |
| w3-008 | Quadrature DSP and observation mathematics | 23/23 | `2026-11-24T09:05:25` |
| w3-009 | Filtered audio engine and offline interfaces | 29/29 | `2026-11-24T09:05:27` |
| w3-010 | Runtime events and MIDI ownership | 20/20 | `2026-11-24T09:06:13` |
| w3-011 | Host control metadata and state streams | 29/29 | `2026-11-24T09:06:33` |
| w3-012 | Host expression audio and variant contracts | 33/33 | `2026-11-24T09:07:50` |
| w3-013 | Host mono layouts and buffer transactions | 33/33 | `2026-11-24T09:08:29` |
| w3-014 | Host expression transaction bounds | 36/36 | `2026-11-24T09:10:35` |
| w3-015 | Host MIDI commands and note models | 27/27 | `2026-11-24T09:10:53` |
| w3-016 | Host audio regression, automation, and FP modes | 39/39 | `2026-11-24T09:11:50` |
| w3-017 | Host lifecycle, recall, and stream failures | 33/33 | `2026-11-24T09:12:23` |
| w3-018 | Host state and expression concurrency | 21/21 | `2026-11-24T09:14:28` |
| w3-019 | Host expression fault recovery | 18/18 | `2026-11-24T09:13:53` |
| w3-020 | Loaded modules, SDK validators, and host benchmarks | 26/26 | `2026-11-24T09:16:13` |
| **Unique union** | **All registered names** | **523/523** | Per-capture timestamps above |

The JUnit timestamps contain no timezone. Wrapper metadata separately records UTC start/end timestamps for groups 004–020, from `2026-11-24T15:59:07.002708+00:00` through `2026-11-24T16:16:31.992549+00:00`; exact per-group values remain in the summary and raw JSON. The initial group 003 result retains only its JUnit timestamp. The seven-hour difference between the two timestamp representations is preserved without assigning a timezone or fabricating a missing timestamp. Group numbers are assignments, not a promise of chronological execution order.

The inventory SHA-256 is `2169223536d12fca211d69710ec22e07927a2de0ff2ca4d13065f9d64b0d0066`. A read-only parse of the existing generated `CTestTestfile.cmake` at aggregation found the same 523-name multiset, and both CMake source files still matched the planning hashes. This inspection did not run CTest or reconfigure the build.

| Registration identity | SHA-256 |
| --- | --- |
| `CMakeLists.txt` | `951a0f584c2ebea1f7428ac344d1012be97190e1f89d98308c5151b81093b55c` |
| `cmake/Vst3.cmake` | `4bfaf06f12e4eceb79afc5127079ae693b782b0359536ef70b748097428bf36a` |
| Generated `CTestTestfile.cmake` observed during aggregation | `9a1c16a7b54960497c237885fd8373fa9c50f0c117a9dd0c5f030665a1386fb6` |
| Planning composite CMake identity | `4404e39b61b68bd255286ff7d0fc57b0ab879b616ef25881848a3dd9a5a18f5c` |

The composite is SHA-256 over sorted relative-path + NUL + file-SHA-256 + LF records, as defined by the original planning JSON. The standalone native-focus fixture was outside this 523-test registration at the captured boundary; native assertions are not added to the registered-test total. This is a historical registration checkpoint at the aggregation time above. Subsequent CMake/CI registration of that fixture is separate work and may increase the live inventory; it does not change this original 523-name assignment or require future CMake bytes to match the retained checkpoint.

## What the retained hashes establish

All explicit before/after pairs below were compared from their retained values and were equal. A single hash snapshot does not establish before/after stability. Where an original worker recorded equality but omitted separate after-values, that statement is preserved as a worker-reported result, not promoted to an independently rechecked pair.

| Group | Explicit paired files verified | Capture boundary |
| --- | ---: | --- |
| w3-003 | 3 | Primary capture contains explicit before/after hashes for the three native modules. It has no contemporaneous complete source or test-executable manifest. The later native evidence is separate. |
| w3-004 | 10 | Explicit before/after test-list and executable hashes. Source hashes were separately captured at 2026-11-24T16:00:12.077200+00:00, after this run. |
| w3-005 | 45 | Explicit before/after selected source, executable, configuration, selection and environment hashes. |
| w3-006 | 12 | Explicit before/after selected source, executable, CTest registration and selection hashes. |
| w3-007 | 28 | Explicit before/after selected source, executable/tool, registration and selection hashes. |
| w3-008 | Not retained | One retained hash map for selected source, binaries, registration, environment and results; worker reports pre/post equality but separate post-run values are not retained. |
| w3-009 | 38 | Explicit before/after selected source, executable/tool, registration and selection hashes. |
| w3-010 | Not retained | One retained binary/input snapshot; worker reports binary hashes unchanged after execution. Separate post-run values and source stability are not recorded. |
| w3-011 | Not retained | Single selected executable and relevant-input snapshot; no before/after stability assertion is retained. The source list is explicitly not a full dependency closure. |
| w3-012 | 5 | Explicit executable before/after hashes. Source/dependency/configuration hashes are a separate single snapshot; external SDK/system headers are excluded. |
| w3-013 | Not retained | Single selected executable/source snapshot; no before/after stability assertion is retained. |
| w3-014 | Not retained | Single input snapshot with worker-reported post-run equality for selection, binaries and sources; separate post-run values are not retained. |
| w3-015 | 62 | Explicit before/after source, executable, selection, registration and environment hashes. |
| w3-016 | Not retained | Executable/source/configuration hashes explicitly collected after the run; no before/after stability claim. |
| w3-017 | Not retained | Input, binary and source hashes explicitly collected before execution; no retained after-run comparison. |
| w3-018 | 41 | Explicit before/after tracked source, executable, selection, registration and environment hashes. |
| w3-019 | Not retained | Executable/source/environment/registration hashes captured at 2026-11-24T16:14:40.057229+00:00, after this run; no before/after comparison. |
| w3-020 | 63 | Explicit before/after selected source, executable/tool, all three native module and metadata, configuration, selection and environment hashes. |

The raw manifests preserve the exact selected files and hashes, including test executables and tools even though executable bytes are not bundled. These hashes bind observed sources and prebuilt executables; this aggregation does not independently reproduce builds or assert a complete source-to-binary dependency closure. Capturing current source during aggregation does not retroactively strengthen an earlier run.

## Native-helper changes and source boundaries

The original group 003 capture used one set of three native modules. Two subsequent group 003 captures, run02 and run03, each passed the same 32 selected tests with stable module hashes within that capture. They are preserved under `followups/native-helper/` and excluded from the unique union. Their literal JUnit timestamps are `2026-11-24T09:07:51` and `2026-11-24T09:14:09`. Final run03 and group 020 identify exactly the same three module hashes:

| Capture | Module | SHA-256 |
| --- | --- | --- |
| Initial group 003 | `RhodesResearch.so` | `68ac2ab5f6b83e8c5656da2421850a18dd2f6d6711320b93ec8667ace5c45131` |
| Initial group 003 | `RhodesResearchFiltered.so` | `93d2e190f7ac0b4a3091e74b52f10b56ea1bee627001e27a83b2aefb492ece82` |
| Initial group 003 | `RhodesResearchQuadrature.so` | `59ee9ce1906b5d35677d05ba3faa55bbeaa47bcaa12d55ceed40f0b850518794` |
| Group 003 run02 | `RhodesResearch.so` | `4fdf654806191e04e7169518e9ca73a94ab1d50047f55c9cf379a7f81ef38a52` |
| Group 003 run02 | `RhodesResearchFiltered.so` | `e7fbcb4bc08d020feef4a66d011c46dbbf81f19f95b6c4d65d11b14131821797` |
| Group 003 run02 | `RhodesResearchQuadrature.so` | `41c499403f9974edefa780a47fc99beb82f8110575afb8bb1f4363d347d09e98` |
| Group 003 run03 and group 020 | `RhodesResearch.so` | `d55b7427a770657ef5bd7874f5cb10ab8e4c0325466492ffea0e2eb4c027b2e3` |
| Group 003 run03 and group 020 | `RhodesResearchFiltered.so` | `504d99b120c98bb69bca672d7b52161a05e529fe410bc2ebfc62b58b7d0e0705` |
| Group 003 run03 and group 020 | `RhodesResearchQuadrature.so` | `9d1e4ccfe4f042388b876d4c16ccac156f546d5aa9959eb9b908bdf7fef545e5` |

The final native context identifies the following source bytes. The copies in `source-observations/` were captured at aggregation and match these retained final identities:

| Final source identity | SHA-256 |
| --- | --- |
| `plugins/vst3/photo_native_focus.cpp` | `c24f8d70731a193b0b7bf833984c00df5a60375d2d306986f1440757af938c5f` |
| `plugins/vst3/photo_native_focus.hpp` | `9bf4553ec6a1418747d5379ba531bf15bbfb7e589f3923ff9c8131485704396a` |
| `tests/cpp/photo_native_focus_test.cpp` | `09b125635cc9ce89770d96ea6d9395e19ccc28b7deb199b0c42f7d9233b34144` |

The earlier group 004 source snapshot observes helper `.cpp` hash `3b4fb4a575f306bfc7363884cdd2dbaca55cb32d47e0b69e0cbada52d055c8ed` and `.hpp` hash `fbcc4ba33dc8e779e39d441ef3bd67d76596c9a2cdb90f20631d1acb519084e9`. Group 015 observes intermediate hashes `b29fa86cce2da3a3e6a8633eedbeccdc3f301f45ed53d3bb4b9940433223a72f` and `3d3d1206490eb3d0490f7fbd9d8f11436490c77ef8af25b27c4c2147a1e4c59b`. Those observations are retained rather than rewritten to final identities. They do not establish the helper source compiled into the initial group 003 modules; that initial result retained module identities without its own complete source manifest.

Detailed Linux/X11/Xvfb focus behavior, retained unsuccessful/intermediate observations, and actual REAPER/SDK interaction evidence belong to the separate [final native record](../photo-editor-final-native-20261124/README.md). `context/w3-003-evidence.json` is retained here solely to preserve the source/replay relationship. Its native assertion counts and host captures are not included in the 523-test result.

## Later ownership-fixture warning follow-up

After the partitioned runs, the parent renamed the test helper `Recorder::release(index, accepted)` to `expect_release`, preserving the SDK virtual `release()` and the existing assertions, to address GCC `-Werror=overloaded-virtual`. The retained group 004 source snapshot identifies `tests/cpp/vst3_audition_ownership_test.cpp` as `98920e74a218d520a888d01affc59c81607e02b5e3ee76e287f986425e60043a`; the follow-up source identity is `612d6a78768f7b6588d92dce529517bbf4e97c8c352c325b28cd43faae1399e0`.

The original build log shows six completed compile/link steps for the three ownership executables. The dedicated CTest log records **3/3 passed**, covering `vst3.audition_ownership`, `vst3.filtered_audition_ownership` and `vst3.quadrature_audition_ownership`, all already present in group 004. The parent’s retrospective `root-overloaded-virtual-fix-evidence.json` retains the exact syntax and CTest commands, directly observed zero exit codes, file hashes and `execution_timestamp_retained: false`. The syntax log is empty because the successful syntax-only compile produced no output. Its retrospective note time is not an execution timestamp.

The three logs and retrospective JSON are preserved under `followups/ownership-warning/`. This follow-up is reported as **3/3 for those existing tests**, not added to 523 and not a rerun of the complete suite. The record makes no broader claim about any remote CI job.

## Scope and remaining qualification

This evidence establishes the retained software/numerical and host-fixture outcomes for the named selections and recorded per-run identities. It does not establish a calibrated physical model, specimen fidelity, audible device output, real-time audio deadlines, general native-DAW operation, other operating systems, or unrecorded source changes. Native helper behavior and further compatibility gates retain their own evidence and acceptance boundaries. No canonical task completion or fidelity status was changed by this aggregation.
