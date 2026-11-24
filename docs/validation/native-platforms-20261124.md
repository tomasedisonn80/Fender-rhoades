# Native platform confirmation — 24 November 2026

Commit `015767fee5635ff85a717739188ff4af60fe84f2` passed both hosted workflows: [Foundation CI](https://github.com/baabi-boo/Fender-rhoades/actions/runs/45334821142) and [Experimental VST3 CI](https://github.com/baabi-boo/Fender-rhoades/actions/runs/45334821145). All ten jobs succeeded. The three VST3 jobs built, validated, archived and uploaded all three experimental bundles for their respective platforms.

The [structured evidence](native-platforms-20261124.json) retains job and step results, exact skipped tests, 74 source hashes checked against that commit, local executable identities, and the hash of the [retained log archive](native-platforms-20261124.tar.gz). Retrieved GitHub log text is normalized to LF with one final newline; the archive does not claim to preserve an original server ZIP representation.

| Observed job | Passed | Skipped | Registered |
| --- | ---: | ---: | ---: |
| Linux core | 150 | 0 | 150 |
| Windows core | 147 | 0 | 147 |
| macOS core | 141 | 6 | 147 |
| Linux VST3 | 138 | 0 | 138 |
| Windows VST3 | 134 | 0 | 134 |
| macOS VST3 | 125 | 9 | 134 |
| Linux address/undefined-behavior sanitizers | 150 | 0 | 150 |

The macOS ARM skips are the explicit x86 FTZ/DAZ environment modes, not removed assertions or silent failures. Linux additionally exercises independently loaded module coexistence. Each platform passed all three official SDK validators and all three new `moduleinfotool` metadata validators. The three separate project-state/Python jobs also succeeded; their step records are retained without treating later Python changes as already tested.

The previously failing native macOS `quadrature.qengine_prepare` test now passes with unchanged exact equality after the scoped Clang FIR-contraction setting. The earlier [one-ULP diagnostic](quadrature-clang-contraction-20261124.md) remains preserved. This is native confirmation of the repair; it is not a claim to have independently proved a particular compiler optimization defect.

The final local Linux SDK-integrated build passed **288 of 288 CTests**, with no skips. After rebuilding, the Q6 probe, WAV renderer and all three callback benchmark executables remained byte-identical to the binaries in the [exact compatibility](batched-quadrature-compatibility-20261124.json) and [final timing](vst3-matrix-batch-final-20261124.json) records. The Clang-only repair therefore does not require reinterpreting those GCC observations.

These are synthetic core, host-adapter and hosted-CI results. They do not establish behavior in an untested real DAW, audio-device deadlines, measured Rhodes fidelity, code signing/notarization, or a release.
