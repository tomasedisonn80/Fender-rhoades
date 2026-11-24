# Worker 040 — wave-two integration verification

24 November 2026; actual worker 040/100, continuation B wave 2. The integrator
owns Git and canonical state. Authored repository deliverables are this report
and its [standalone archive](../../validation/wave2-integration-20261124b.json).
Generated build/test outputs, capture scripts and logs remain under ignored
build directories.

## Result

**The Linux Release integration gate passed: 301 native CTests and
848 Python tests, with no failures or skips.** All 176 bound source,
test, tool and fixture files matched before testing, after testing and at the
final repository checks.

| Check | Observed result |
| --- | --- |
| Existing VST3-enabled Release build | Exit 0; 2.635 s command wall time |
| Complete CTest suite | 301/301 passed; 75.96 s CTest total, 76.125 s command wall time |
| Complete Python discovery | 848/848 passed; 117.919 s unittest total, 118.822 s command wall time |
| Final canonical-state validation and synchronization | passed |
| In-progress continuation-B ledger validation | passed; no 100-worker completion claim |
| Repository documentation integrity | passed; includes the new supported-extrema and alias guides |

All three actual Linux VST3 modules passed their SDK validators and moduleinfo
validators. These six entries are included in the native total. The Python
command set `RHODES_AUDIO_PROBE` to the absolute existing native executable;
the previously optional three-small-comparison test ran and passed once.
Its native comparisons are unit-test coverage, not a repeat of a full retained
numerical experiment.

## Execution and binding

Read the operating contract, README, project state and roadmap. Startup state
validation, synchronization and next-task selection passed at revision 34:
15 tasks, 26 evidence records, no unclaimed work, fidelity `unvalidated`.
The assignment remained under the integrator's existing PHY-002/RT-001 scope.

The starting source context was main commit
`956df5a1f959889814c6a5cb487873285e1461bd`. The exact canonical 176-file
inventory SHA-256 is
`9ab7548e14fc39f18ab1d509033f1b44be7fe52ce3f9113e250b6fef79b14f04`.
The inventory covers CMake, native source/headers, apps, plugins, tests, Python
tools and fixture/data directories. Root held these inputs while other workers
completed documentation; advancing Git metadata is not treated as changed
compiled source. Initial and final identities are retained separately.

Actual commands, launched sequentially once from the repository root:

```sh
cmake --build build/vst3 --config Release --parallel 2
ctest --test-dir build/vst3 -C Release --output-on-failure --no-tests=error
python3 -m unittest discover -s tests/python -v
```

Only the Python command added the audio-probe environment variable. All commands
set `PYTHONDONTWRITEBYTECODE=1`; the archive records these explicit overrides,
not the process environment. The existing configuration used GCC 14.2.0,
Release `-O3 -DNDEBUG`, Python 3.12.14, NumPy 2.3.5, SciPy 1.17.0 and the
pinned headless SDK. Compiler, SDK, generated build metadata, binaries, libraries
and objects have recorded identities. This was an incremental build of the
existing configuration, not a clean-build reproducibility experiment.

## Retention and limits

The archive embeds exact stdout/stderr, command records, complete CTest
`LastTest.log`, configuration/compile metadata, before/after inventories and
the execution/packaging scripts. Every embedded UTF-8 payload is checked against
its byte count and SHA-256. No build or test failure occurred, and no gate was
retried. The final report substitutions changed no checked link destinations.

These are local Linux software-integration results. They do not establish
Windows/macOS execution, native DAW behavior, audio-device deadlines, worst-case
runtime, physical fidelity, complete contact extrema, or task acceptance.
CTest's ordinary benchmark smoke tests are included; no separate performance
capture or callback-ratio experiment ran. Historical numerical failures and
missing measured-instrument evidence remain unchanged.

Archive SHA-256: `e891f1607cec96ee6b4be63af885d15e643de7c08c30e945a08865efdb790012`.
