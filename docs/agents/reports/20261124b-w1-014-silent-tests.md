# Worker 014 — permanent silent-observation regressions

Date: 2026-11-24. Assigned scope: `tests/cpp/silent_observation_test.cpp`
and this report. Root owns production integration, CMake registration, Git and
canonical project state. No production source, header, CMake or state file was
edited by this worker. Temporary build products are under `build/w1-014`.

Read `AGENTS.md`, README, project state, roadmap and canonical task JSON, the
engine/header and existing fractional/control FP tests, plus
[Worker 005's audit](20261124b-w1-005-silent-key-runtime.md) and
[Worker 013's implementation/differential report](20261124b-w1-013-silent-observation.md).
Requested additional differential/portability context from Worker 013; its
written 64-environment comparison report was available and informed the scope.

## Added coverage

The executable exposes five selectors; Root registered the corresponding CTest
names shown below. All checks remain active in Release builds.

| CTest name | Permanent coverage |
| --- | --- |
| `quadrature.silent_plan_validation` | Default, wrong-rate and stale plans; invalid node indices; invalid/duplicate fractions and rejected-plan preservation, all while every key is silent, including pedal-down silence. Valid plan reuse after reset and replacement after reprepare. |
| `quadrature.silent_network` | Delayed first strikes at keys 0, 3, 4, 36 and 72, plus an eight-key chord. Independent loaded voltage retains the four-pickup group, all 23 three-pickup groups and the complete source resistance. |
| `quadrature.silent_lifecycle` | Delayed first strike, sustained release, all-notes-off under sustain, pedal-up damped tails, moving retrigger, both signed-zero release values, copy construction, assignment, hard reset, same-rate reprepare and a new first strike. Includes a released tail below 1e-13 V and a 1e-200 normalized strike, without an absolute audio-sized error floor. |
| `quadrature.silent_transactions` | Invalid intensities, negative subnormal intensity, invalid key indices and invalid preparation leave silent or ringing mechanics, energy, sample rate and future output unchanged. Empty processing is nonmutating. A valid first strike still works on a previously rejected key. |
| `quadrature.silent_fp_environment` | Runtime fenv rounding plus independently selected MXCSR rounding/FTZ/DAZ where supported; signed-zero voltage from both observation APIs against a separate zero oracle; all 219 silent modal states against independently advanced oscillator instances; observation nonmutation and preservation of rounding/flush controls. |

Semantic cases run at 32/48/96/192 kHz, except the transaction fixture at 48 kHz.
Plans use six exact binary fractions spanning the cell, including large
fractions that change high-register transition coefficient signs at 32 kHz.

## Independent expectations

The moving-voltage oracle uses the amplitude-form damped-oscillator solution
from the engine's public boundary states and separately tracked event history.
It does not inspect the private excitation bit or plan matrices, nor call the
pickup/network helpers or another observation API as its expected value.
It sums local modal motion before applying the declared synthetic polynomial
and the complete resistive divider. Its tolerance scales with the contributing
motion, so returning zero for a tiny surviving tail fails. The existing scalar
versus batch suite remains separate; both new observation results are compared
directly with the independent expectation.

The stationary oracle computes only the signs needed for zero-valued motion.
An independent long-double closed form determines the velocity diagonal signs;
the fixture asserts that every coefficient stays away from a sign boundary and
that all phases lie in the interval where the off-diagonal sign is known.
Bit operations reconstruct exact zero-product signs, and noinline additions
with volatile operands execute the ordered zero sums in the current rounding
environment. Positive pickup weights, branch divisors and load gain preserve
zero signs; the zero-displacement pickup voltage reverses the velocity sign.
This avoids fixed libm/output bit goldens and does not reproduce the production
engine implementation in the permanent test.

Independent oscillator instances always advance every zero state. Comparison
with their 219 state pairs detects a future attempt to skip mechanical advances
and lose exposed negative-zero coordinates. General audio exception-status
flags are deliberately outside the comparison; the optimization has no promise
to preserve denormal-operand status effects.

## Verification actually run

Repository startup checks:

- `python3 tools/project.py validate`: valid, 14 tasks / 26 evidence records.
- `python3 tools/project.py sync --check`: generated state current.
- `python3 tools/project.py next --json`: revision 32, no available tasks.

Configured `build/w1-014` using GNU 14.2.0 and Release settings. The first run
compiled the test directly against the freshly built `rhodes_core` because
CMake registration was still owned by Root. After registration appeared,
explicitly reconfigured and built the actual CMake target:

```sh
cmake -S . -B build/w1-014 -DCMAKE_BUILD_TYPE=Release
cmake --build build/w1-014 --parallel 2 --target rhodes_silent_observation_tests
ctest --test-dir build/w1-014 -C Release --output-on-failure --no-tests=error \
  -V -R '^quadrature\.silent_'
```

Result: **5/5 passed, no skips, 0.08 seconds total reported by CTest.** The build
used `-O3 -DNDEBUG`, C++20, repository warnings and `-Werror`, without fast-math.

| Selector | Always-active checks | Independent moving/silent analytic observations |
| --- | ---: | ---: |
| `plan_validation` | 32,300 | 384 |
| `network` | 45,104 | 576 |
| `lifecycle` | 98,074 | 924 |
| `transactions` | 7,310 | 48 |
| `fp_environment` | 6,385,366 | Separate zero-sign oracle described above |

The FP run completed **64 environments / 256 rate scenarios**: four fenv
rounding requests, four independent MXCSR rounding requests, and FTZ/DAZ off,
FTZ only, DAZ only or both. Each scenario includes observation before/after
silent advancement and pedal changes. The same prepared engine and plan are
reused across environment changes so a cached rounding decision would fail.

### Portability and regression sensitivity

- The test does not assume which hardware unit `fegetround()` reads after
  independent MXCSR changes. It observes actual scalar zero arithmetic, then
  verifies observation calls preserve the resulting control state.
- Targets without MXCSR run the fenv cases and explicitly report the unavailable
  independent MXCSR/flush matrix. A compiler configuration without a known
  scalar binary64 SSE2 domain likewise reports that matrix skipped. Unavailable
  requested controls or rounding behavior produce explicit per-case skips.
- Compiled the complete test with the same warning flags plus `-ffast-math`.
  Invoking its `fp_environment` selector reported
  `SKIP fp_environment: compiler fast-math does not preserve signed zero` and
  returned **77** as intended. This is a skip-path check, not a fast-math
  numerical compatibility claim. Root was notified to register skip code 77.
- In temporary build-only source copies, replaced the runtime rounding safeguard
  with an unconditional bypass. `fp_environment` failed with exit 1:
  `scalar signed zero differs from independent arithmetic`.
- In another temporary copy, cleared `excited` on `note_off`. `lifecycle` failed
  with exit 1: `observation disagrees with independent loaded voltage`.
  Both mutations were linked against the registered test object and never
  applied to production files. Their sources/binaries remain under
  `build/w1-014/mutations` as local diagnostics.
- `git diff --check` passed for the shared tree at the time of verification.

## Frozen identities and boundaries

| File | SHA-256 |
| --- | --- |
| `src/research_engine.cpp` | `01c2a9f7f9f7cfa4f759be56770178f32a71a28667d4ac6ab390e03ff7a95814` |
| `include/rhodes/research_engine.hpp` | `2a6ba0483a010d9b7a44517c5ed7c215f05d57ed6e96bd6609127322092929e4` |
| `tests/cpp/silent_observation_test.cpp` | `6f9924cf19a18c458c2caaca5af93609e4319333cc421a3feff05e50870bfda1` |

Only Linux x86-64/GNU 14.2 runtime results are claimed here. No Clang executable
was available in this environment; no Windows, macOS or non-x86 execution was
performed. Publicly rejected note transactions are covered; the test does not
inject inaccessible private huge states to force a late impulse overflow.
Positive subnormal control classification and full baseline differential
coverage remain in the existing control suites and Worker 013's recorded probe.

Root was notified of the frozen test hash and exact final counts. Next action:
include these registered selectors in the integration build and future native
platform runs. Root separately owns the full retained audio replay and timing
evidence; this worker makes no speedup or physical-fidelity claim.
