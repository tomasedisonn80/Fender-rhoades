# Clang quadrature continuation diagnostic — 24 November 2026

Native macOS Foundation CI at `9966cdd2b63dcebe1f4e3865d9e74ea37f081b3a` failed only `quadrature.qengine_prepare` among 147 registered tests. The same test passed on local GCC and local Clang 22.1.8 with x86 FMA enabled. This local compiler is not an equivalent reproduction of the native AppleClang environment.

Commit `cfd8ef452c2a053bf41e636668498d13ccf37dc0` added failure-only hexadecimal diagnostics and retained exact equality. Native [job 138242617361](https://github.com/baabi-boo/Fender-rhoades/actions/runs/45334296428/job/138242617361) again failed. At 32 kHz, after rejecting positive zero as the new rate, continuation frame 1 differed by one binary64 ULP:

```text
actual   -0x1.7ce971fead695p-8
expected -0x1.7ce971fead694p-8
```

The added final mechanical-state equality check passed before the output assertion failed. Independent review found that every invalid rate returns at the first preparation guard, before construction or member assignment; plans, mechanics and FIR histories are owned values, not shared pointers. This narrows the investigation but does not by itself prove the location of the numerical difference. In particular, frame 1 does not support the stronger inference that would apply to frame 0, whose output uses only inherited FIR history.

The candidate repair disables optional contraction only inside `QuadratureFir::output()` for Clang, using a block-scoped `#pragma clang fp contract(off)`. It leaves node/tap order, observation arithmetic, assertion equality and numerical tolerances unchanged. Clang's [floating-point extension documentation](https://clang.llvm.org/docs/LanguageExtensions.html#extensions-to-specify-floating-point-flags) defines this lexical scope; its [user manual](https://clang.llvm.org/docs/UsersManual.html#controlling-floating-point-behavior) explains why fused multiply/add may round differently from separate operations. Call-site contraction is the hypothesis being tested, not an independently proven compiler defect.

With that repair, local optimized Clang 22.1.8 using `-mavx2 -mfma` passed:

- 81 invalid-rate transactions and both injected preparation failures;
- 36 block-partition cases, including empty and one-frame blocks;
- 2,889 event/reference frames across nine rates, maximum convolution error approximately `3.3407e-17` V.

```sh
python3 -m ziglang c++ -O3 -DNDEBUG -std=c++20 \
  -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Werror \
  -mavx2 -mfma -Iinclude src/research_engine.cpp \
  tests/cpp/quadrature_engine_test.cpp -o build/clang-fma-quadrature-fixed
build/clang-fma-quadrature-fixed qengine_prepare
build/clang-fma-quadrature-fixed qengine_partitions
build/clang-fma-quadrature-fixed qengine_events
```

These are observed local checks. Native macOS confirmation for the repair remains pending at this diagnostic checkpoint and must be recorded separately. The earlier VST3 job's public SDK fetch failure occurred before compilation; the subsequent run progressed beyond that step, so no credential or workflow workaround was applied.
