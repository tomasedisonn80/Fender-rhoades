# Contributing

Read [AGENTS.md](AGENTS.md), the [architecture](docs/ARCHITECTURE.md),
[roadmap](docs/ROADMAP.md), and the [agent workflow](docs/agents/WORKFLOW.md).
The target is an identified Rhodes Mark I Stage 73 at a defined electrical output.
The current engine is a synthetic research kernel. A convincing electric-piano
sound is not evidence that a particular instrument has been modeled accurately.

## Development environment

Use CMake 3.24 or newer, a C++20 compiler, and Python 3.11 or newer. The initial
build and checks use only the C++ and Python standard libraries. No network
downloads are required by the build. Build outside the source tree:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
python3 tools/project.py validate
python3 tools/project.py sync --check
python3 tools/validate_data.py
python3 -m unittest discover -s tests/python -v
```

On Windows, use `python` or `py -3` instead of `python3` if needed. Visual Studio
multi-configuration generators place executable files in `build/Release/`.

## A complete contribution

1. Claim one ready task with the project tool and record the assigned file scope.
2. Capture a failing scientific or software behavior before fixing it when useful.
3. Implement the smallest coherent change, with explicit units, validity domain,
   source provenance, approximations, and failure behavior.
4. Run relevant tests. Save a concise evidence record with the exact commands,
   actual outcomes, and revision or content hashes. Do not check in large captures.
5. Update the task through the project tool, regenerate status, and review the diff.
6. Commit a coherent change. Push to the user-authorized branch only. Preserve
   remote work; a rejected update requires fetching and integrating it.

Code touching DSP needs mathematical/reference checks; code touching state
automation needs failure and recovery checks. A documentation-only change does
not need an artificial test. Generated status must be current for every commit.

## Review standards

- Separate implementation correctness, numerical convergence, physical accuracy,
  listening evidence, and observed performance.
- Reject NaN, infinity, invalid units, missing source IDs, ambiguous instrument
  revisions, and synthetic data relabeled as measurement.
- Keep instrument data immutable once cited by an evidence record. Corrections
  receive a new identity and an explanation.
- Audio processing must not allocate, lock, access files, log, or call a network.
  Setup/control paths may allocate when explicitly outside the callback.
- Protect scope: the initial Stage target does not include a Suitcase amplifier,
  speakers, room, plugin SDK, installer, or GUI.

## Licensing and attribution

No project-wide reuse license was present in the initial repository. This setup
does not choose a license on the owner's behalf. See [RIGHTS.md](RIGHTS.md).
Contributions must be original or accompanied by compatible explicit permission.
Publicly readable research and code are not automatically reusable source code.
