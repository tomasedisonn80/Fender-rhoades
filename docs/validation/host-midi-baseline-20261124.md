# Host interoperability baseline — 24 November 2026

Source: `20a77e8b2033d435839df594c783837befb07938`, before this continuation's
implementation. Linux x86_64, GCC 14.2.0, CMake 4.4.5, Release configuration.
The official VST3 SDK and its four submodules were verified by the configured
repository pins. No SDK modifications or dependency downloads were needed.

Observed commands:

```sh
cmake -S . -B build-host -DCMAKE_BUILD_TYPE=Release -DRHODES_BUILD_VST3=ON \
  -DRHODES_VST3_SDK_ROOT=/workspace/scratch/d6370337c6a4/vst3sdk
cmake --build build-host --parallel 3
ctest --test-dir build-host -C Release --output-on-failure --no-tests=error -j 3
```

Configuration and compilation succeeded. **301/301 CTests passed**, zero skips,
including SDK validation, module metadata, bundle resources, host behavior,
three-module coexistence and native core/reference tests. The complete
[CTest output](host-midi-baseline-20261124.log) is retained. This is automated
Linux host-harness evidence, not a native DAW trial or a latency guarantee.

The controller text-parsing defect and pressure-event interoperability gap were
found by the separate [controller audit](../agents/reports/continuation-c/w1-002.md)
and [MIDI audit](../agents/reports/continuation-c/w1-003.md). The existing test
suite did not establish those new requirements. Their behavior and independent
acceptance checks are specified in the [continuation contract](../specs/HOST_MIDI_20261124.md).
