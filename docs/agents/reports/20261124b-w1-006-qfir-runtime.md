# Worker 006 — Q6 FIR traversal audit

Date: 24 November 2026. Wave: 1/5. Scope: read-only implementation/test review and this report. Integrator owns source changes, state, evidence registration and commits.

## Recommendation

Replace the per-tap circular-index selection in [`QuadratureFir::output()`](../../../include/rhodes/quadrature_fir.hpp) with two descending contiguous history segments. Keep one `double sum`, node order `0..5`, and tap order `0..95`. Preserve the existing Clang contraction pragma, final finite check, `const noexcept` API, and all of `push()`/`reset()`.

Concrete replacement for the traversal after the existing pragma:

```cpp
constexpr auto count = quadrature_filter::history_frames;
const std::size_t split = next_ == 0 ? count : next_;
double sum = 0.0;
for (std::size_t node = 0; node < quadrature_filter::node_count; ++node) {
    const auto& coefficients = quadrature_filter::coefficients[node];
    const auto& history = history_[node];
    std::size_t age = 0;
    for (std::size_t index = split; index != 0; ) {
        --index;
        sum += coefficients[age] * history[index];
        ++age;
    }
    for (std::size_t index = count; index != split; ) {
        --index;
        sum += coefficients[age] * history[index];
        ++age;
    }
}
return std::isfinite(sum) ? std::optional<double>{sum} : std::nullopt;
```

`split` is in `1..96`. The first loop visits indices `split-1..0`; the second visits `95..split`. Their lengths are `split` and `96-split`. Thus the age sequence remains exactly `0..95` for each node. At cursor zero, the second segment is empty. At cursor one, the first segment has one tap. Predecrement occurs only after a nonzero/bounded loop guard; no unsigned underflow or pointer before the array is needed. Exhaustive integer enumeration of all 96 cursors and 96 ages matched the original indexing and visited each slot exactly once.

This removes the wrap selection from the source-level tap body. It does not establish that the current compiler emits a branch there, nor guarantee a speedup. Do not introduce segment subtotals, per-node subtotals, pairwise summation, `std::reduce`, explicit `std::fma`, padded histories, omitted zero taps, or early fault returns in this change. Those would widen the arithmetic or state contract. Preserve all 576 multiply/add evaluations and the final-only finite test, including the possibility that a prefix overflows before later terms cancel mathematically.

## Compiler and floating-point boundary

The repository already records an AppleClang continuation failure and the narrowly scoped repair in [`quadrature-clang-contraction-20261124.md`](../../validation/quadrature-clang-contraction-20261124.md). Keep both loops directly inside the existing pragma's scope; extracting the arithmetic into a helper would require a separate review of its floating-point policy.

Clang documents `contract(off)` for disabling contraction and warns that an explicit `-ffp-contract=fast` can override controlling pragmas. Preserve the supported build flags; do not assume the pragma protects arbitrary caller flags. Source: [Clang floating-point extensions](https://clang.llvm.org/docs/LanguageExtensions.html#extensions-to-specify-floating-point-flags), consulted 2026-11-24.

GCC documents contraction enabled by default for C++ through `-ffp-contract=fast`. Changing loop boundaries can change instruction selection, including fusion, even with the same source reduction order. A no-FMA x86 run therefore cannot establish equivalence on an FMA-capable target. Inspect the actual optimized code and compare pre/post binaries under their actual flags. Do not silently add a global contraction policy as part of this index-only optimization. Source: [GCC optimization options](https://gcc.gnu.org/onlinedocs/gcc/Optimize-Options.html), consulted 2026-11-24.

[`CMakeLists.txt`](../../../CMakeLists.txt) applies `/fp:precise` on MSVC. Microsoft's documentation says Visual Studio 2022 defaults to no contraction in this mode; earlier versions could contract, and `/fp:contract` can enable contraction. Preserve the recorded toolchain and flags. The proposed loops use ordinary C++20 and require no compiler extension beyond the existing guarded Clang pragma. Native Windows and AppleClang runs remain necessary before claiming those platforms. Source: [Microsoft `/fp` reference](https://learn.microsoft.com/en-us/cpp/build/reference/fp-specify-floating-point-behavior?view=msvc-170), consulted 2026-11-24.

Fast-math or finite-only assumptions are outside this audit's acceptable build policy: the API must retain NaN/infinity rejection and overflow detection. Exact pre/post compatibility is a same-build-policy requirement; no new cross-compiler identity claim is proposed.

## Existing tests and minimal additions

[`quadrature_fir_test.cpp`](../../../tests/cpp/quadrature_fir_test.cpp) already provides:

| Fixture | Existing coverage |
| --- | --- |
| `qfir_convolution` | Independent chronological, long-double, differently ordered convolution; deterministic varying input across more than five ring turns; startup, expired tail, positive/negative DC. |
| `qfir_impulses` | Each of six nodes at each of 96 insertion positions, exact tap orientation and finite support. |
| `qfir_delay` | Four fractional-node sinusoids and fixed 48-frame delay, with a fixture sensitivity check against an extra frame. |
| `qfir_rejection` | NaN and both infinities in every node immediately before/at wrap; exact immediate and future continuation against an unchanged copy. |
| `qfir_overflow` | Finite sign-aligned extreme input, final overflow, repeated const reads, invalid push, reset and ordinary recovery. |

Retain these tests and tolerances. Two focused additions address risks introduced or exposed by the split:

1. **Exact ordered history and ring rotation.** Use chronological `std::vector<Cell>` storage as a second oracle, with a single binary64 accumulator, node-major then ascending-age order, and explicit zero padding for missing startup cells. It must not share the ring or split-index expression. Keep the existing long-double oracle as the independent numerical-accuracy check. Construct 96 FIRs with zero preludes of lengths `0..95`, then feed every FIR the same 96 distinct, cancellation-rich cells. They now hold identical logical histories at all cursor positions. Compare optional presence and `std::bit_cast<std::uint64_t>` outputs against the ordered oracle, read each output twice, and continue through at least 97 common cells including a zero tail. This catches loop-position-dependent rounding and state/history errors. Control contraction consistently for the strict oracle run; additionally compare actual default/FMA-enabled pre/post binaries rather than treating a contraction-disabled build as evidence about default code generation.
2. **Overflowing prefix with cancelling suffix.** Extend the existing overflow fixture using finite magnitude `0.99 * DBL_MAX`. For nodes `0..2`, choose historical input signs matching their taps; for nodes `3..5`, choose opposite signs. Fill chronological cells according to `age = 95-frame`. The exact mirror pairs give a mathematically cancelling total, but the original node-major accumulation overflows during the positive prefix and must return `nullopt`. The first-half absolute coefficient gain is approximately `1.147862943579722`; Python binary64 evaluation first became nonfinite at node 2, age 49. Individual node absolute gains remain below one. Repeat at a few cursor positions covering an empty second segment, one-element segment and interior split; verify repeated reads, invalid-input nonmutation, and recovery after reset. This protects the API's ordered-arithmetic fault behavior rather than testing only an obviously out-of-range final mathematical sum.

Then run existing `qengine_events`, `qengine_partitions`, `qengine_prepare`, `qengine_faults`, allocation guards, and relevant Q6 host reset/continuation tests. The exact clone/partition assertions must remain exact. Run Release GCC, Clang with FMA enabled where supported, native AppleClang and native MSVC. Sanitizers can check the index-boundary change separately. Replay retained production output hashes when claiming full Q6 compatibility; passing a new oracle alone does not establish the existing 2,097-case replay.

## Performance measurement scope

First measure an isolated pre/post `output()+push()` stream using nonconstant precomputed cells, preallocated output storage, all 96 cursor positions and identical compiler/architecture flags. Keep input generation, allocation and output verification outside each timed batch; consume every output so the compiler cannot eliminate or hoist the workload. Label this timing boundary explicitly. Inspect assembly or optimization diagnostics before attributing any difference to removed branches or vectorization. The dependent accumulation, extra loop setup and short segments can limit the gain.

For an end-to-end performance claim, use the existing [`QUADRATURE_BENCHMARK_20261123.md`](../../specs/QUADRATURE_BENCHMARK_20261123.md) protocol and [`docs/BENCHMARKING.md`](../../BENCHMARKING.md): retain raw durations, repeat fingerprints, source/binary/build identity, CPU/affinity and competing load; report both rounds, percentiles, maxima and nominal-period misses. Keep direct/filtered controls and existing workload/event/sample-format boundaries. A smaller declared pilot can decide whether the optimization warrants that complete capture, but cannot be described as the full 72-process protocol. The currently inspected `build/vst3` tree uses Unix Makefiles; the documented matrix launcher requires a fresh/current Release Ninja build, so do not run it against this tree without handling that prerequisite. No benchmark was executed in this audit and no speedup is claimed.

## Observed checks and provenance

Reviewed `AGENTS.md`, `README.md`, `docs/PROJECT_STATE.md`, `docs/ROADMAP.md`, `project/state.json`, relevant implementation/tests and cited numerical/performance contracts. Base HEAD: `8813b2680a49c07c277f0d0e932843fdb356c3ee`.

- `python3 tools/project.py validate`: passed; 14 tasks, 26 evidence records, fidelity unvalidated.
- `python3 tools/project.py sync --check`: passed; generated state current.
- `python3 tools/project.py next --json`: no available work; the integrator retains active PHY-002/RT-001 claims. This bounded review made no independent claim.
- `build/vst3/rhodes_quadrature_fir_tests`: all five existing fixtures passed; observed maximum chronological error `7.02997e-16`, fixed-delay sinusoid error `1.80807e-15`.
- `build/vst3/rhodes_quadrature_engine_tests`: all five existing fixtures passed; 2,889 oracle frames over nine rates, maximum error `3.89737e-17`; 36 partition cases; 81 invalid-rate transactions and two preparation failures; fault/overflow/invalid-cell cases passed; 81 guarded callbacks observed zero allocations.
- Integer traversal enumeration passed for all 9,216 cursor/age pairs. Coefficient extraction initially matched an earlier occurrence of the word `coefficients` and failed a 576-value count assertion; correcting the anchor to `coefficients {{` yielded 576 values and the prefix-overflow calculation reported above. This was an analysis-script correction, not a product failure.

The C++ executions used existing local Release binaries; this audit did not rebuild them or implement the proposed traversal. They are baseline smoke observations, not validation of an unimplemented patch. Native compiler checks, candidate compatibility, new fixtures and timings remain unrun.

Inspected source SHA-256:

```text
quadrature_fir.hpp       0ddcf691a5d251979a7adf1ea32d471c8fcf1fd6e5a7a2e203c8fb63974b7b6d
quadrature_fir_test.cpp  68ef7b4cee30bd8ba6f8e7aba1ab9ee5d5f67318f4e96b38be84e66517576a04
```

Changed path: this report only. Next action: integrator implements the two loops and assigns exact-history/prefix-overflow tests plus compiler-specific compatibility before measuring performance. This audit does not complete RT-001 or change any physical-model acceptance gate.
