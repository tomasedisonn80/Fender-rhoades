# W4-70 — Native engaged-cell fault coverage

**Agent:** `/root/w4_70_native_fault_coverage`  
**Result:** no material native-applicability gap found.

I independently retrieved [Foundation run 45340923223](https://github.com/baabi-boo/Fender-rhoades/actions/runs/45340923223), its job metadata and four native logs. All seven jobs succeeded at exact commit `240170673983f9992e4657a2d96c2172ece1ac30`.

| Native job | Compiler reported by CMake | Passed | Skipped | Registered |
| --- | --- | ---: | ---: | ---: |
| [Linux](https://github.com/baabi-boo/Fender-rhoades/actions/runs/45340923223/job/138264123055) | GNU 15.2.0 | 150 | 0 | 150 |
| [Windows](https://github.com/baabi-boo/Fender-rhoades/actions/runs/45340923223/job/138264123061) | MSVC 19.52.37029.0 | 147 | 0 | 147 |
| [macOS ARM](https://github.com/baabi-boo/Fender-rhoades/actions/runs/45340923223/job/138264122910) | AppleClang 21.0.0.21000101 | 141 | 6 | 147 |
| [Linux ASan/UBSan](https://github.com/baabi-boo/Fender-rhoades/actions/runs/45340923223/job/138264122997) | GNU 15.2.0 | 150 | 0 | 150 |

Every log explicitly records passes for `quadrature.qengine_events`, `qengine_partitions`, `qengine_prepare`, `qengine_faults` and `qengine_allocations`: five non-skipped selectors per job. These are included in the totals above, not additional tests.

The fixture and production headers match both the tested commit and [W4-63's identities](20261124-w4-63-invalid-cell.md). Its fault loop has no platform conditional: the original three node modes and three advance modes retain **84 scenarios**; two engaged-cell modes add **48**, covering six nodes and four offsets. Twelve calibrations prove engaged invalid cells reach the wrapper. Prefix/suffix, no-advance, exact 129-frame continuation and fault/reset assertions remain active in Release. The scenario counts follow the inspected unconditional source; ordinary native CTest logs report selector outcomes without printing those counters.

The macOS exclusions are precisely `state_fp.{ftz,daz,ftz_daz}` and `control_fp.{ftz,daz,ftz_daz}`. Their separate fixtures condition x86 MXCSR support and CMake recognizes return 77 as a skip. No quadrature selector has that skip rule.

I also verified the retained temporary mutant header, metadata and stderr against W4-63. The diff removes only the wrapper's three-line finite-cell scan; metadata records successful compilation followed by exit 1, `wrapper advanced after failure`. This corroborates the retained local GNU 14.2 sensitivity result; I did not rerun it or claim native mutation testing.

Required `project.py validate`, `sync --check` and `next --json` passed at revision 27 with 22 evidence records. Read-only connector retrieval, `git show`, source inspection and SHA-256 checks establish this report's scope. Documentation integrity and report-format checks passed. Only this report was written; no build, CTest, benchmark, numerical replay, Git mutation, ledger or canonical-state change occurred.

This establishes the specified synthetic fault fixture on the recorded toolchains. It does not establish arbitrary compiler flags, exhaustive faults, DAW/device behavior or measured fidelity. RT-001 remains incomplete.

## Examined identities

All source/report entries below match commit `2401706`. Log hashes identify decoded connector text normalized to LF and one final newline, not original server archive bytes.

| Examined file or log | SHA-256 |
| --- | --- |
| `tests/cpp/quadrature_engine_test.cpp` | `481894129f3dd92ede0875d376c4034d38b3e347b44f82c6bb8ad205f0d37063` |
| `include/rhodes/quadrature_engine.hpp` | `c625a82b61eb3e721061177654c12ffa6aac0049e026b9633ae53ea2abaa773d` |
| `include/rhodes/quadrature_fir.hpp` | `0ddcf691a5d251979a7adf1ea32d471c8fcf1fd6e5a7a2e203c8fb63974b7b6d` |
| `include/rhodes/event_processor.hpp` | `36e4b64f3278b0079b20e94a333a657c900e4118dc24433ec9d8c58442651351` |
| `CMakeLists.txt` | `2a70fba5924a3eeb01bf0dfd7c2a0f6a00726d27ac7535660a4bf267cf81ff3e` |
| `.github/workflows/ci.yml` | `9a0d441f70dc60be6bdf7ec3b3ab5a3c5e046feed103a043633d1bd27911e94a` |
| `tests/cpp/state_fp_environment_test.cpp` | `90e29f5903c3b67afd5f28f0b8958cf2c6bca04a3f2420655296b6fd7ab81a4d` |
| `tests/cpp/control_fp_environment_test.cpp` | `ce9dd0cdd386d9e5f84622c131202de4bf4ec8c9bd5d78ce85acb6a1683e8b3a` |
| W4-63 report | `e4b6d12815eb2d4022d914964e919a3f56fddd0e66e99ff13fb3c08e46c6d35a` |
| Linux job `138264123055` log | `e6b911937ca7a0534e4531f8092ea2754f468a1579781e0884dc00c45c4c4be3` |
| Windows job `138264123061` log | `2cbc4df88313b9fbe82f3ae86694c98b26377ecb56db70e51717fc08127e9723` |
| macOS job `138264122910` log | `e05244ca728c507072dec6399edef11037ae143e132eb499e62d4abb40896de1` |
| Sanitizer job `138264122997` log | `29e4f037e097cf9dc049b1d9099df2b56285ba481f6f1790c5f76d16c5903685` |

The three verified mutant identities remain indexed in W4-63; no temporary artifact was promoted into production.
