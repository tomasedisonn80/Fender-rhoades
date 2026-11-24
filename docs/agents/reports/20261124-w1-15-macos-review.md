# Wave 1 / assignment 15 — independent macOS fix review

Reviewed source at `b9df4a1c1f704abeb4e1783f0dac364214dfe960`, including
`cmake/Vst3.cmake`, `audio_fir_test.cpp`, the later `audio_engine_test.cpp`
conversion fixes, and the local clean pinned official SDK definitions. No source,
canonical-state or Git mutations were performed; only this report was added.

## Link-interface finding

The CoreFoundation repair is correctly placed after SDK target creation and
inside `if(APPLE)`. `find_library(... REQUIRED)` produces an explicit configure
failure if the framework is unavailable. Attaching it to `base` with `INTERFACE`
supplies the final-link requirement of the static archive without modifying
third-party source.

The pinned `SMTG_VST3_SDK.cmake` links `sdk` publicly to `sdk_common`, which
publicly links `base`. All three research benchmark executables and every
`rhodes_add_vst3_test` target link `sdk`, so they inherit the framework. Loader
support reaches the same archive through `sdk_hosting`/`sdk_common`. The model
state-stream-only test links only `rhodes_core` and has no corresponding SDK
archive dependency. Non-Apple configuration never executes framework discovery
or appends the dependency.

This agrees with CMake's official
[`target_link_libraries` semantics](https://cmake.org/cmake/help/latest/command/target_link_libraries.html):
interface libraries propagate to consumers; they do not link the named target
itself. Public propagation in the SDK supplies the required chain. Documentation
was accessed 24 November 2026.

## Numerical fixture finding

The explicit `long double` casts in FIR group-delay division and independent
filter coefficient construction preserve the prior arithmetic type. Their
supported factors are 1, 2 and 4, all exactly representable. Expected values,
formulas and tolerances are unchanged. The fixes therefore address strict
AppleClang conversion diagnostics without weakening numerical checks.

## Observed verification and limits

Queried the GitHub connector independently for
[run 45330459842](https://github.com/baabi-boo/Fender-rhoades/actions/runs/45330459842).
The macOS job **138230158399**, Windows job **138230157971** and Linux job
**138230158409** all completed successfully, including module build, retained
host tests, three official validators, packaging and artifact upload. This
confirms the original framework fix at the run's `807e078` source. The later
Foundation-only conversion changes are source-reviewed here; their separate
Foundation CI outcome was not queried by this assignment.

`project.py validate`, `sync --check` and `next --json` completed successfully.
No new build or native test execution was performed. No remaining defect was
established in the reviewed fix. CI success does not establish native DAW,
device-latency, signing/notarization or measured-instrument validation.
