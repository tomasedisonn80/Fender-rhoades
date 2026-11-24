# Clang control-value floating-environment repair — 24 November 2026

## Observed defect

The macOS Foundation job [138232087762](https://github.com/baabi-boo/Fender-rhoades/actions/runs/45331050436/job/138232087762) at `b9df4a1` built successfully but failed `control.domains` and `control.rounding_modes`: integer control classification/conversion raised floating-point exception flags. The remaining 145 configured tests passed. The VST3 jobs had already passed on all three platforms; this was a distinct foundation contract defect.

A local optimized Clang 22.1.8 reproduction, provided by the PyPI `ziglang` 0.17.1 build-tool package, failed the original domains selector with the same assertion. This compiler was installed only for verification; no runtime dependency or third-party source was added to the repository.

## Repair and public basis

`control_value.hpp` now brackets its integer-only inline helper definitions with Clang's scoped `float_control(precise, on, push)` and `float_control(except, on)`, then restores the includer's policy with `pop`. The public control contract includes preserving exception flags even for signaling-NaN input; default Clang exception behavior does not preserve that requirement. The change protects every caller of the header without changing unrelated DSP compilation policy. The control test also declares `STDC FENV_ACCESS ON` for Clang so its deliberate environment changes/readback are explicit.

Clang's [user manual](https://clang.llvm.org/docs/UsersManual.html#controlling-floating-point-behavior) documents default ignored exceptions and strict preservation; its [language extensions](https://clang.llvm.org/docs/LanguageExtensions.html#extensions-to-specify-floating-point-flags) define the scoped pragmas and push/pop. No assertions, numerical tolerances or platform tests were removed.

## Actual local evidence

Using `python3 -m ziglang c++ -O3 -DNDEBUG -std=c++20 -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Werror -I include`:

- Original test plus original header: domains failed with the recorded FP-exception assertion.
- Original, unmodified test from `b9df4a1` plus repaired header: domains passed (229,546 checks), and rounding_modes passed (3,705,600 checks across 16 supported rounding/flush combinations). This demonstrates that the production header repair works without relying on a test-only strict pragma.
- Repaired test plus repaired header: domains, quantize (319,589 checks), widen (200,253 checks) and rounding_modes all passed.

This local compiler/architecture reproduction is not native macOS execution. The next hosted macOS result must establish that platform outcome. A separate immutable report will record hosted results and broader integration outcomes.

## Tested source hashes

- `include/rhodes/control_value.hpp`: `d7c282afdcffaef90f7872f97b37bfb34671448da64376d3439649c052d4d48d`
- `tests/cpp/control_value_test.cpp`: `955f7218b087ccc4ad231443ee4f23970ccc596c2ea55f87d54ad9ce953adb72`
