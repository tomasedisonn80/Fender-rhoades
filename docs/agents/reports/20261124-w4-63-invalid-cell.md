# W4-63 — Engaged invalid quadrature cells

**Agent:** `/root/w4_63_invalid_cell`  
**Assignment:** 63, wave 4  
**Result:** the specific W3-48 test-coverage gap is closed locally.

## Change and scope

Changed only `tests/cpp/quadrature_engine_test.cpp` and this report. The
[W3-48 review](20261124-w3-48-fir-faults.md) found that the substitute engine
returned `nullopt` before its NaN/Inf observations reached the wrapper's own
finite-cell scan. Existing early-rejection modes and assertions remain intact.
Two separate modes now return an **engaged** six-value cell with NaN or infinity
at the selected node. Twelve calibration cases require the optional to remain
engaged, verify the selected invalid value and five finite peers, and count all
six observations.

The additional 48 scenarios cover two invalid values, six nodes and failure
offsets 0, 1, 17 and 63 after 120 warmup frames. Both the standalone wrapper and
event processor preserve the exact successful prefix and zero the failed
suffix. Counters require complete cell observation and rejection before any
mechanical advancement. Copied wrappers then produce identical 129-frame
continuations against a reference stopped at the same successful prefix,
checking retained history across the full 96-cell FIR support. This diagnostic
comparison does not advertise fault recovery without reset. The event processor
still verifies its fault latch, unchanged frame coordinates, rejection of queued
reset/empty calls, and explicit-reset recovery.

No production code, API, coefficient, tolerance, exact-equality assertion,
floating-point policy or existing prepare-failure diagnostic changed.

## Actual verification

```sh
cmake --build build/vst3 --config Release --target rhodes_quadrature_engine_tests --parallel 2
ctest --test-dir build/vst3 -C Release -R '^quadrature\.qengine_' --output-on-failure --no-tests=error -V
```

The GNU C++ 14.2 Release target built successfully. All **5/5 CTests passed**, no
skips, in **0.32 seconds**. Fault output records **132 scenarios**: 84 existing
plus 48 new, each exercised directly and through the event-processor fence.
The retained FIR overflow still first fails at frame 49. The event, partition,
prepare and calibrated allocation selectors also pass.

For sensitivity, a temporary include tree under
`/workspace/scratch/d6370337c6a4/w4-63-invalid-cell-mutant` copied the production
wrapper and removed only its three-line finite-cell scan. I reused the test
compile command with that include directory first, retained its exact Release
warning flags, and linked the existing `librhodes_core.a` into a separate
executable. The compiler working directory was `build/vst3`; exact argv:

```sh
/usr/bin/c++ \
  -I/workspace/scratch/d6370337c6a4/w4-63-invalid-cell-mutant/include \
  -I/workspace/scratch/d6370337c6a4/Fender-rhoades/include \
  -O3 -DNDEBUG -std=gnu++20 -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Werror \
  /workspace/scratch/d6370337c6a4/Fender-rhoades/tests/cpp/quadrature_engine_test.cpp \
  /workspace/scratch/d6370337c6a4/Fender-rhoades/build/vst3/librhodes_core.a \
  -o /workspace/scratch/d6370337c6a4/w4-63-invalid-cell-mutant/quadrature_engine_mutant
/workspace/scratch/d6370337c6a4/w4-63-invalid-cell-mutant/quadrature_engine_mutant qengine_faults
```

Compilation returned 0. Its `qengine_faults` run returned **1**:

```text
FAIL qengine_faults: wrapper advanced after failure
```

The normal header and executable were not overwritten. The shared wrapper, FIR
and event-processor header hashes match their before-work identities. The test
source hash below was recorded after the passing run and remained unchanged
through the mutation experiment and report creation. The mutation metadata
retains the exact compiler argv, working directory and run result.

Required project checks passed at state revision 27 with 21 evidence records.
Documentation integrity and whitespace checks passed. No full 288-test suite,
2097-case replay, benchmark, native remote job or hardware experiment was run.
Native compiler confirmation belongs to the integrator's subsequent CI. RT-001
and physical fidelity remain incomplete/unvalidated.

## Identities and retained local records

Observed HEAD at post-run identification: `510141e4b23969c0e51bbcd0e80167f0c2f4f8bf`.
The test change was uncommitted; its file hash, rather than that HEAD alone,
identifies this execution.

| File | SHA-256 |
| --- | --- |
| Test source | `481894129f3dd92ede0875d376c4034d38b3e347b44f82c6bb8ad205f0d37063` |
| Production wrapper header | `c625a82b61eb3e721061177654c12ffa6aac0049e026b9633ae53ea2abaa773d` |
| Production FIR header | `0ddcf691a5d251979a7adf1ea32d471c8fcf1fd6e5a7a2e203c8fb63974b7b6d` |
| Production event-processor header | `36e4b64f3278b0079b20e94a333a657c900e4118dc24433ec9d8c58442651351` |
| Normal test executable | `da83ad8fcb31f61f218b705ea066d644758aa2e163862e0c3d0532508866652a` |
| `build/w4-63-build.log` | `3aeaf135191f967dc7ebf52396f4bd0ad9949bc0f7139f0f6f2844baa6f14ccc` |
| `build/w4-63-qengine-tests.log` | `22ca061b43bed33310b0b077516eaba06e95a6138799df1ae4e8e0fae2284586` |
| Temporary mutant header | `5ac1d9845dbe0531f07b456943da5cb19396167b4c03c3d9a6e16701fc749ab5` |
| Temporary `metadata.json` | `9d25acc165252c68c2b296dc9d2aeda1bf95118d731ae90a8ae2891509d068b7` |
| Temporary `run.stderr` | `26e2fefbc7f0d0c78251660d8a213736a05b4956a9ba2f5a12166f4c7041a4ca` |

Root owns publication, evidence registration and milestone decisions.
