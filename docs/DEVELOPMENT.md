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

## Automated checks

`.github/workflows/ci.yml` configures project/data/campaign validation and Python
tests on Linux, Windows and macOS, C++ builds on the same three platforms, and an
additional Linux sanitizer build. Symlink/FIFO fixtures explicitly skip only when
the host lacks the needed facility or permission; a skipped case is not a pass
for that facility. Inspect job summaries before claiming platform coverage.
The workflow has read-only repository permissions and never commits or pushes.
Actions are pinned by commit. Dependency update proposals are limited to the
GitHub Actions ecosystem because the initial runtime has no external packages.

A separate `.github/workflows/vst3.yml` configures the optional module, retained
host harness and official SDK validator on the same three platforms. It checks
the SDK root and all four initialized submodule revisions, and leaves plugin
installation links disabled. The development campaign has its own ledger;
unfinished assignments remain visible until their actual reports are complete.

A configured CI job is not a completed test result. Read the Actions run for the
specific commit before reporting a platform as passing. Local Linux verification
cannot establish that Windows or macOS passed. There is no plugin binary release
or installer pipeline in this prototype.

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
