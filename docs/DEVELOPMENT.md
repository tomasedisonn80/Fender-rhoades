# Development and verification

The current equations, units and numerical failure behavior are specified in
[NUMERICS.md](NUMERICS.md).

## Local checks

The core requires C++20 and CMake 3.24+. Project tooling requires Python 3.11+.
No compiler or third-party audio framework is downloaded by the build. Use a
compiler supplied by your operating system's normal development environment.

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release --parallel 2
ctest --test-dir build -C Release --output-on-failure
python3 tools/project.py validate
python3 tools/project.py sync --check
python3 tools/validate_data.py
python3 tools/campaign.py validate
python3 tools/campaign.py sync --check
python3 -m unittest discover -s tests/python -v
```

Ninja presets provide `dev`, `release`, and supported-compiler `sanitize` builds.
On Windows, the generic CMake commands work with Visual Studio's generator;
executables normally appear under `build/Release/`. Substitute `python` for
`python3` where appropriate.

## Automated checks

`.github/workflows/ci.yml` runs project/data/campaign validation, Python tests,
C++ builds on Linux/Windows/macOS, and an additional Linux sanitizer build.
The workflow has read-only repository permissions and never commits or pushes.
Actions are pinned by commit. Dependency update proposals are limited to the
GitHub Actions ecosystem because the initial runtime has no external packages.

A configured CI job is not a completed test result. Read the Actions run for the
specific commit before reporting a platform as passing. Local Linux verification
cannot establish that Windows or macOS passed. There is no plugin binary release
or installer pipeline in this foundation.

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
