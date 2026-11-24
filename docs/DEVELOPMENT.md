# Development and verification

The current equations, units and numerical failure behavior are specified in
[NUMERICS.md](NUMERICS.md).

## Local checks

The core requires C++20 and CMake 3.24+. Project tooling requires Python 3.11+.
Retained concurrency tests also require standard-library support for
`std::barrier` and `std::jthread`. Use Xcode 26 or later for these tests with
Apple's bundled library: its [release notes](https://developer.apple.com/documentation/xcode-release-notes/xcode-26-release-notes)
document nonexperimental `std::jthread` support. The configured macOS deployment
target is 11.0; this is build configuration, not an observed minimum-OS result.
No compiler or third-party audio framework is downloaded by the build. Use a
compiler supplied by your operating system's normal development environment.
The optional [VST3 build](VST3.md) uses an explicitly supplied, pinned SDK and
requires CMake 3.25 or later. The offline contact reference and runtime benchmark
are included in the ordinary dependency-free C++ build.

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release --parallel 2
ctest --test-dir build -C Release --output-on-failure --no-tests=error
python3 tools/project.py validate
python3 tools/project.py sync --check
python3 tools/validate_data.py
python3 tools/check_docs.py
python3 tools/campaign.py validate
python3 tools/campaign.py sync --check
python3 tools/development_campaign.py validate
python3 tools/development_campaign.py sync --check
python3 tools/validate_continuation_campaign.py
python3 tools/quadrature_campaign.py validate
python3 tools/quadrature_campaign.py sync --check
python3 tools/validate_development_waves.py
python3 tools/verify_vst3_matrix_archive.py \
  docs/validation/vst3-matrix-20261124.json \
  docs/validation/vst3-matrix-batched-20261124.json \
  docs/validation/vst3-matrix-batch-final-20261124.json
python3 tools/generate_audio_filter.py --check
python3 tools/generate_quadrature_filter.py --check
python3 -m unittest discover -s tests/python -v
```

Ninja presets provide `dev`, `release`, and supported-compiler `sanitize` builds.
The supplied `sanitize` preset is disabled on Windows, regardless of compiler.
On Windows, the generic CMake commands work with Visual Studio's generator;
executables normally appear under `build/Release/`. Substitute `python` for
`python3` where appropriate.

`RHODES_ENABLE_SANITIZERS=ON` requires a GNU, Clang or AppleClang compiler with a
GNU-style driver and the corresponding sanitizer runtime. Unsupported families
and MSVC-style drivers, including clang-cl, fail configuration explicitly. Normal
MSVC builds do not request these flags. Successful configuration alone is not
evidence that the resulting instrumented tests passed.

All test presets reject an empty test selection. The sanitizer test preset also
sets fail-on-UB and stack-trace options, matching CI's diagnostic policy. These
[test-preset fields](https://cmake.org/cmake/help/v3.24/manual/cmake-presets.7.html#test-preset)
are supported by the declared CMake minimum. Record any environment-specific
sanitizer override and its reduced coverage in the evidence report.

### Floating-point build configuration

The tested macOS VST3 workflow uses `Unix Makefiles` and does not request
`-ffast-math` or `-ffp-contract=fast`. The Clang-only control helpers use a scoped
precise/exception policy, and `QuadratureFir::output()` disables contraction in
its function body. The [scope review](agents/reports/20261124-w3-47-clang-scope.md)
binds these changes to the native regression results; it does not establish
equivalence across arbitrary toolchains or flags.

Clang's [floating-point options](https://clang.llvm.org/docs/UsersManual.html#controlling-floating-point-behavior)
specify that `-ffp-contract=fast` overrides contraction pragmas, while
`-ffast-math` enables that setting and additional unsafe transformations. The
pinned SDK's Xcode-generator branch adds both flags. Changing to that generator
or adding external fast-math flags therefore requires a new verification result;
the scoped pragmas alone do not preserve the tested configuration. See the
[pragma semantics](https://clang.llvm.org/docs/LanguageExtensions.html#extensions-to-specify-floating-point-flags)
and [VST3 build instructions](VST3.md#build-the-optional-adapter). Keep exact
control, state and continuation assertions when changing flags; do not widen
tolerances to make a different configuration appear equivalent.

## Offline tools and numerical dependencies

The project/data/document checks, campaign validators, filter generators and
retained timing-archive verifier use the Python standard library. The
[current-wave validator](agents/DEVELOPMENT_WAVES_20261124.md) accepts honest
progress records; add `--require-complete` only when checking the completed
five-wave campaign. It checks metadata and report bytes, not whether agents
executed or scientific acceptance was earned.

The archive command above verifies all three retained captures without extracting
files or executing archived commands. It recomputes the declared workload and
timing summaries from raw records. It does not rerun a benchmark, authenticate
the original acquisition, or validate current binaries. See
[benchmark methodology](BENCHMARKING.md) for actual measurement prerequisites.

| Work | Python dependency boundary |
| --- | --- |
| Native candidate sweep and default four-fixture peak comparison | Standard library; native candidate generation is a separate C++ executable. |
| Contact peak comparison with `--with-damped-time` | Optional SciPy timing worker; unavailable or inconclusive timing remains explicit. |
| General contact audit, full audio reference studies and exact audio replay tool | Optional NumPy/SciPy numerical environment, with actual versions recorded. |

Follow the [contact peak guide](CONTACT_PEAK_DIAGNOSTIC.md) for byte-preserving
candidate capture, exclusive output directories and exit meanings. A default
four-fixture pass does not complete PHY-002. The
[dependency notes](DEPENDENCIES.md#optional-independent-python-audit) identify
the numerical packages and observed versions; those versions are not a complete
environment lock. Missing optional packages cause explicitly marked numerical
test skips, dependency errors or unavailable requested references. Such a run
does not establish the omitted numerical coverage, and these packages are not
VST3 runtime dependencies.

## Automated checks

`.github/workflows/ci.yml` configures project/data/campaign validation and Python
tests on Linux, Windows and macOS, C++ builds on the same three platforms, and an
additional Linux sanitizer build. It also checks the current development-wave
ledger and all three retained timing archives. Symlink/FIFO and undecodable-path
fixtures explicitly skip when the host lacks the required facility or permission;
macOS ARM skips the x86 FTZ/DAZ modes. A skipped case is not a pass for that
condition. Inspect skip reasons and optional-dependency coverage before claiming
platform results.
The workflow has read-only repository permissions and never commits or pushes.
Actions are pinned by commit. Dependency update proposals are limited to the
GitHub Actions ecosystem because the initial runtime has no external packages.

A separate `.github/workflows/vst3.yml` configures the optional module, retained
host harness, all three official SDK validators and all three module metadata
validators on the same three platforms. It checks
the SDK root and all four initialized submodule revisions, and leaves plugin
installation links disabled. The development campaign has its own ledger;
unfinished assignments remain visible until their actual reports are complete.

A configured CI job is not a completed test result. Read the Actions run for the
specific commit before reporting a platform as passing. Local Linux verification
cannot establish that Windows or macOS passed. There is no plugin binary release
or installer pipeline in this prototype.

The [retained native checkpoint](validation/native-platforms-20261124.md) at
`015767f` records successful core and three-variant VST3 jobs on Linux, Windows
and macOS, including exact passed/skipped/registered counts. The later
[Foundation run at `510141e`](https://github.com/baabi-boo/Fender-rhoades/actions/runs/45340189549)
passed all seven jobs, including the ledger/archive checks and Python tooling
on all three platforms. These are results for those revisions, not automatic
coverage of later source edits, native DAWs, audio devices or deployment baselines.

The current C++ command-line interfaces use narrow process arguments. Arbitrary
Unicode filesystem paths and non-ASCII machine labels have not been validated on
Windows. Use simple portable paths/labels for reproducible baseline runs until
native wide-argument conversion and dedicated fixtures are implemented.

The renderer stages its WAV in an exclusively reserved sibling directory and
checks flush/close before publication. Default publication never replaces an
existing entry and requires hard-link support. `--force` permits atomic
replacement of a regular file; final symlinks and nonregular destinations are
refused. Use trusted, quiescent parent directories. Forced replacement is not an
adversarial-path sandbox or a power-loss durability guarantee. A cleanup warning
after successful publication does not mean the completed output was lost.

## Evidence records

Record exact commands and actual outcomes in a compact report, then register its
hash through the project tool. Evidence should identify its code revision or
content hash. Avoid a circular rule in which a report must contain the hash of the
commit that contains the report; record the tested source tree or parent revision
and subsequent verification instead.

Numerical tests address analytical correctness, state persistence, physical signs,
passivity where applicable, and deterministic behavior. They do not establish
the measured accuracy of the physical instrument. Performance evidence requires
the machine, compiler, flags, sample rate, block size, load scenario, duration,
maximum callback time, and deadline misses. This repository currently claims no
CPU percentage or fixed polyphony guarantee.

## Integration policy

The current setup is authorized for `main`. Keep one integrator for shared state
and Git operations. Give concurrent workers disjoint paths; they return reports
and changes for integration. Before publishing, inspect remote `main`, reconcile
any new commits, rerun affected checks, and use a non-forced update. Never replace
someone else's work to satisfy a deadline or an agent-count target.
