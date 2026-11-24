# Worker 012 — ordered QFIR regression and compiler comparison

Date: 24 November 2026. Wave: 1/5. Assignment: add one standalone regression
source and this report. The integrator owns production code, CMake registration,
canonical state, evidence registration and Git operations.

## Result

Added [`quadrature_fir_order_test.cpp`](../../../tests/cpp/quadrature_fir_order_test.cpp)
with an independent chronological binary64 oracle, all 96 circular-history
positions, invalid-push continuation, repeated reads, and a mathematically
cancelling history whose positive accumulation prefix overflows. Both fixtures
pass GCC's default target, GCC native with contraction disabled, and local Clang
with default and native targets.

**GCC native with its default contraction setting fails the strict oracle. The
original traversal produces the same failures.** A separate actual-flags
comparison found zero original/candidate differences in 20,352 ordinary history
records plus 472 overflow/reset records. Preserve this as an existing arithmetic
policy boundary, not a newly introduced contiguous-loop regression. No production
change was made to suppress contraction.

## Added coverage and arithmetic scope

`qfir_ordered_history` constructs 96 FIRs using zero preludes of lengths `0..95`.
Each receives the same 96 distinct cells, followed by a rejected cell, 17 live
continuation cells and 97 zero cells. Every accepted-history state is checked
twice against the same chronological reference. Inputs are constructed with
integer operations and binary64 bit patterns; the second half of the common
history is the negative time/node mirror of the first half, providing substantial
cancellation. Missing startup cells are explicit zeros in the reference. The
fixture observes **40,704 exact output reads** across all 96 cursor positions.

The reference stores cells chronologically in a vector and visits node `0..5`,
then age `0..95`. It shares neither the circular storage nor a wrap/split index
formula with production. Volatile stores force a binary64 rounding after each
product and addition; comparisons check optional presence and exact `uint64_t`
representations, including the final positive-zero tail. This is an exact
**separate multiply/add** oracle. Its intended test target must disable
contraction for the included production header too. It does not define a new
default/native production arithmetic policy.

`qfir_prefix_overflow` uses finite magnitude `0.99 * DBL_MAX`. Nodes `0..2`
align input signs with their taps; nodes `3..5` oppose them. Assertions verify
exact coefficient mirror symmetry, finite individual products, individual
absolute node gains below one, and a positive-prefix gain above the finite
range threshold. The mirror-opposite terms cancel exactly in real arithmetic,
but ordered binary64 accumulation returns `nullopt`. The observed prefix gain
printed as `1.14786`. No wider-than-double exponent range is needed to establish
the fixture's mathematical cancellation or overflow threshold.

The overflow fixture covers cursors `0`, `1`, `48`, and `95`: an empty second
range, a one-element first range, an interior split and a one-element second
range. It retains repeated reads, late-node invalid rejection, 97 zero-tail
continuation cells, a renewed overflowing history, reset and 17 ordinary
startup-continuation cells. It observes **944 exact output reads**. This checks
both fault status and the finite values obtained when the extreme history
expires. Existing long-double convolution, impulse, delay and engine tests are
unchanged and were not rerun by this worker.

## Observed compiler runs

Environment: Linux `6.18.57`, x86-64, glibc `2.41`; AMD EPYC 9V74. GCC is Debian
`14.2.0-19`. Its `-march=native` resolves to `znver4` with FMA, AVX2 and AVX-512
enabled. Clang `22.1.8` is available through the installed
`python3 -m ziglang c++` command; no standalone `clang++` was on PATH.

All compilations used C++20, `-O3 -DNDEBUG`, and
`-Wall -Wextra -Wpedantic -Wconversion -Wshadow -Werror`.

| Compiler/header | Additional options | Ordered history | Prefix overflow/continuation |
| --- | --- | --- | --- |
| GCC / contiguous candidate | Default target | Pass | Pass |
| GCC / contiguous candidate | `-march=native` | Fail against strict oracle | Fail at finite tail against strict oracle |
| GCC / original traversal | `-march=native` | Same failure | Same failure |
| GCC / contiguous candidate | `-march=native -ffp-contract=off` | Pass | Pass |
| Clang / contiguous candidate | Default target | Pass | Pass |
| Clang / contiguous candidate | `-march=native` | Pass | Pass |

Both original and candidate GCC native executables printed the following first
failure for each fixture and returned status 1:

```text
FAIL qfir_ordered_history: common history rotation=0 frame=0 read=0 actual=0x1.5a43c332e9d52p-17 [0x3ee5a43c332e9d52] expected=0x1.5a43c332e9d51p-17 [0x3ee5a43c332e9d51]
FAIL qfir_prefix_overflow: prefix continuation rotation=0 frame=92 read=0 actual=-0x1.7f73fe5b1e496p+1006 [0xfed7f73fe5b1e496] expected=-0x1.7f73fe5b1e499p+1006 [0xfed7f73fe5b1e499]
```

The first ordinary result differs by one binary64 ULP; the finite extreme-tail
result differs by three ULPs. Initial cancelling-prefix overflow and rejection
still returned `nullopt` before that finite-tail assertion. The native failure is
not an overflow-status failure.

## Separate original-traversal comparison

The original header was extracted without source edits from commit `2a8caab`
into `build/w1-012/legacy/rhodes/quadrature_fir.hpp`. Temporary harnesses include
the candidate and original headers in one translation unit, renaming only the
original class with a preprocessor macro. Separate `noinline` wrappers retain
inspectable generated arithmetic. Both implementations receive identical cells,
zero preludes and rejected pushes. The ordinary harness reuses the committed
test's deterministic fixtures and reference functions; the overflow harness
also compares fault expiry and reset continuation at the four boundary cursors.

| Actual harness flags | Ordinary records | Original/candidate differences | Strict-oracle mismatches, each implementation | Rotation differences, each implementation |
| --- | ---: | ---: | ---: | ---: |
| GCC default | 20,352 | 0 | 0 | 0 |
| GCC native | 20,352 | 0 | 877 | 2,114 |
| GCC native, contraction off | 20,352 | 0 | 0 | 0 |
| Clang native | 20,352 | 0 | 0 | 0 |

Rotation differences count records whose output differs from the same logical
history at rotation zero. They expose the existing GCC native dependence on
where the logical history lies in the circular array. They do not indicate
different node/tap source order between the two implementations.

The additional GCC native overflow/reset comparison passed **472 records with
zero original/candidate differences**. Each implementation differed from the
strict oracle in the same 24 finite records. All four initial prefix-overflow
statuses were explicitly checked as absent optionals.

`objdump -d -C` of the actual native differential binary found six
`vfmadd231sd` sites in each GCC traversal wrapper, alongside 25 `vmulpd` and
192 `vaddsd` sites. With `-ffp-contract=off`, both wrappers instead contain six
`vmulsd` and 198 `vaddsd` sites, with the same 25 `vmulpd` sites and no FMA.
Clang's candidate wrapper and original out-of-line `output()` use separate
scalar multiply/add instructions and no FMA. These observations, together with
the contraction-off control, support contraction as the source of the strict
oracle deviations. Instruction-site counts are not runtime timings or executed
operation counts; no speedup is claimed.

## Commands and local artifacts

The actual compiler command forms were:

```sh
g++ -std=c++20 -O3 -DNDEBUG -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Werror -Iinclude tests/cpp/quadrature_fir_order_test.cpp -o build/w1-012/qfir-order-gcc-default
build/w1-012/qfir-order-gcc-default
g++ -std=c++20 -O3 -DNDEBUG -march=native -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Werror -Iinclude tests/cpp/quadrature_fir_order_test.cpp -o build/w1-012/qfir-order-gcc-native
build/w1-012/qfir-order-gcc-native
g++ -std=c++20 -O3 -DNDEBUG -march=native -ffp-contract=off -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Werror -Iinclude tests/cpp/quadrature_fir_order_test.cpp -o build/w1-012/qfir-order-gcc-native-no-contract
build/w1-012/qfir-order-gcc-native-no-contract
g++ -std=c++20 -O3 -DNDEBUG -march=native -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Werror -Ibuild/w1-012/legacy -Iinclude tests/cpp/quadrature_fir_order_test.cpp -o build/w1-012/qfir-order-legacy-gcc-native
build/w1-012/qfir-order-legacy-gcc-native
python3 -m ziglang c++ -std=c++20 -O3 -DNDEBUG -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Werror -Iinclude tests/cpp/quadrature_fir_order_test.cpp -o build/w1-012/qfir-order-clang-default
build/w1-012/qfir-order-clang-default
python3 -m ziglang c++ -std=c++20 -O3 -DNDEBUG -march=native -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Werror -Iinclude tests/cpp/quadrature_fir_order_test.cpp -o build/w1-012/qfir-order-clang-native
build/w1-012/qfir-order-clang-native
```

The differential commands used those same warning/language/optimization flags
and `-Iinclude`, with these actual inputs and outputs:

| Driver | Compiler and additional flags | Executable run |
| --- | --- | --- |
| `build/w1-012/differential.cpp` | `g++` default | `build/w1-012/qfir-diff-gcc-default` |
| `build/w1-012/differential.cpp` | `g++ -march=native` | `build/w1-012/qfir-diff-gcc-native` |
| `build/w1-012/differential.cpp` | `g++ -march=native -ffp-contract=off` | `build/w1-012/qfir-diff-gcc-native-no-contract` |
| `build/w1-012/differential.cpp` | `python3 -m ziglang c++ -march=native` | `build/w1-012/qfir-diff-clang-native` |
| `build/w1-012/prefix_differential.cpp` | `g++ -march=native` | `build/w1-012/qfir-prefix-diff-gcc-native` |

All five differential executables returned status 0. Harnesses, extracted
baseline, binaries and disassembly remain temporary local artifacts under
`build/w1-012`; they are not additional tracked deliverables.

Startup workflow checks passed: `python3 tools/project.py validate` reported
14 tasks / 26 evidence records with fidelity unvalidated;
`python3 tools/project.py sync --check` found current generated state;
`python3 tools/project.py next --json` returned no available unclaimed task.
This bounded worker did not claim or update project tasks. `git diff --check`
also passed. No full CTest run, native AppleClang run, MSVC run, sanitizer run,
performance capture or 2,097-case replay was performed by this worker.

## Integration recommendation

Recommended target: `rhodes_quadrature_fir_order_tests`.
Recommended CTest names: **`quadrature.qfir_ordered_history`** and
**`quadrature.qfir_prefix_overflow`**, using their corresponding command-line
selectors. With no argument the standalone executable runs both and reports
both failures independently. Registration can follow the existing QFIR target's
pattern. For GNU/Clang command-line frontends, add **target-only
`-ffp-contract=off`** so the production header and oracle agree on separate
roundings. Retain the existing MSVC floating-point policy and verify that
platform separately. Do not add a global compiler option or change production
arithmetic merely to make this strict oracle pass.

The actual native/FMA comparison remains a distinct compatibility observation.
It does not extend the retained full replay to another compiler or architecture,
and passing these fixtures does not establish physical-model fidelity.

## Source binding and changed paths

Work started at candidate commit `7356288`. The shared integration branch advanced
to `8adf435a0589b685bbc9f671d41a813808bdf016` during this assignment; the tested
QFIR header and coefficients retained the following hashes. The final source
comment explicitly documenting contraction scope was added after the initial
standalone runs; executable test logic was unchanged, and the subsequent native
prefix differential compiled that final source.

```text
cf6c85e70036b3f1794ecf131a9db43bcb8da373e47c5335f56b1bb57559b45e  include/rhodes/quadrature_fir.hpp
3cca4ea6553ceb01810a6230f2144491364fa48af7d6cfbc43a8e3dcf5d08b51  include/rhodes/quadrature_filter_coefficients.hpp
381c77467b48baeef726d7739624161862a8b7af1d99830b93ad3b2dedc6ada5  tests/cpp/quadrature_fir_order_test.cpp
0ddcf691a5d251979a7adf1ea32d471c8fcf1fd6e5a7a2e203c8fb63974b7b6d  original header from 2a8caab
6b7ffb25cf1c91ca0046038f1ce22e89b945ea73eae5efb20ec3a73671f45a7a  build/w1-012/differential.cpp
c8a5291ebd873f380be8c0130b0a972ce3d1a15c4bb211e51e9e483433d949a1  build/w1-012/qfir-diff-gcc-native
```

Changed tracked deliverable paths: the new C++ source and this report only.
Next action: integrator registers the two tests with their explicit arithmetic
scope, preserves the native contraction result, and decides separately whether
broader production contraction guarantees are desired.
