# Worker 013 — conservative never-excited observations

Date: 2026-11-24. Scope: `src/research_engine.cpp`,
`include/rhodes/research_engine.hpp`, and this report. Root owns Git, canonical
state, integration, host rebuilds, full replay, and timing evidence. No CMake or
tracked test files were added. Read the required start documents and
[Worker 005's audit](20261124b-w1-005-silent-key-runtime.md).

## Implementation and invariant

Each private `Key` now records whether a successful nonzero `note_on` has occurred
since preparation/reset. Default construction and the successful `prepare()`
candidate initialize `excited=false`; explicit `reset()` clears it. Successful
nonzero `note_on` publishes `true` only after all three candidate impulses succeed.
The existing bit-based intensity validation remains authoritative: either signed
zero releases, while a positive subnormal marks the key excited even when DAZ,
FTZ, or underflow makes the calculated impulse zero. Rejected notes or preparation
leave the complete prior key/engine unchanged. Default copies and assignments
copy the bit with mechanical state. `note_off`, zero-intensity release,
`all_notes_off`, pedal changes, retrigger, and arbitrarily long tails never clear
an existing excitation bit.

The invariant is specific to the current **uncoupled, impulse-excited synthetic
mechanics with switched losses**. Before the first successful nonzero strike,
homogeneous finite modal transitions can change zero signs but cannot create
nonzero motion. Coupled mechanics, reciprocal forces, or any additional forcing
must revisit this optimization; the header explicitly records that boundary.
There is no amplitude threshold or tail retirement.

Only `observe()` and `observe_cell()` skip never-excited modal observations and
pickup conversion. Existing node/rate/plan validation occurs first. Every source
array still has 73 entries; skipped pickup entries are explicitly **negative
zero**, and the original ordered `harp_.voltage()` reduction remains unchanged.
The entire `sample()` function body is byte-identical to the pre-edit snapshot:
all pickup work, all 219 modal advances, signed-zero state evolution, and
partial-progress failure behavior remain in place. Public APIs, module/state
identities, loss choices, pickup constants, and electrical topology are unchanged.

## Signed-zero and rounding justification

For an untouched key, each fractional matrix product/sum produces a signed zero.
Pickup observation weights are positive and each outer motion sum starts at
`+0.0`. Under nearest, upward, and toward-zero rounding, adding either sign of zero
to that accumulator leaves `+0.0`. Therefore both observed motion sums are
positive zero and the existing positive linear pickup coefficient produces
`-0.0` voltage. The shortcut writes exactly that sign.

Downward rounding can instead make opposite-sign zero sums negative. Each
observation call therefore performs one addition of volatile `+0.0` and `-0.0`
and inspects its sign. A negative result disables the shortcut for the whole
call, preserving the original traversal. The witness is exact, changes no
rounding/flush controls, and avoids assuming that `fegetround()` describes an
independently configured SSE/MXCSR rounding mode. It is evaluated after public
plan/node validation and is not cached across calls.

GNU 14.2 Release object inspection confirms a runtime `addsd` followed by
`movmskpd` in both observation methods. The local probe confirmed the witness
selected the fallback precisely for MXCSR downward rounding, even when the fenv
rounding value disagreed, and left MXCSR controls and exception flags unchanged.

## Verification actually run

- `python3 tools/project.py validate`: valid, 14 tasks / 26 evidence records.
- `python3 tools/project.py sync --check`: generated state current.
- `python3 tools/project.py next --json`: revision 32, no available tasks.
- Configured `build/w1-013` with `cmake -S . -B build/w1-013
  -DCMAKE_BUILD_TYPE=Release`; GNU 14.2.0, C++20, `-O3 -DNDEBUG`, repository warning
  flags and `-Werror`, without fast-math. The first build invocation mistyped
  `rhodes_core_tests`; core, fractional, and quadrature targets had built, then
  Make correctly rejected the nonexistent target. Corrected to `rhodes_tests`
  and successfully built all six requested existing test executables.
- Existing selected CTest suite: **33/33 passed**, no skips, 0.65 seconds reported.
  Command:

  ```sh
  cmake --build build/w1-013 --parallel 2 --target \
    rhodes_fractional_observation_tests rhodes_quadrature_engine_tests \
    rhodes_tests rhodes_control_value_tests rhodes_control_fp_tests \
    rhodes_state_fp_tests
  ctest --test-dir build/w1-013 -C Release --output-on-failure --no-tests=error \
    -R '^(core\.|control\.|control_fp\.|state_fp\.|quadrature\.(analytic_nodes|plan_domains|observation_nonmutation|damping_regimes|qengine_))'
  ```

  This includes analytic nodes, plan domains, observation nonmutation, damping
  regimes, all five quadrature engine selectors including faults/allocations,
  all 12 core selectors, all four control selectors, and all eight control/state
  FP environment selectors.

- Temporary local comparison probe under `build/w1-013/probes/`: compiled the
  saved pre-edit source/header with `-Drhodes=baseline_rhodes` and linked it beside
  the candidate Release library. Compared sample bits/status, all 219 modal-state
  pairs, energy bits, optional presence, each scalar node, and each complete cell.
  Result: **64 fenv/MXCSR/flush combinations; 256 rate scenarios; 8,448 cells;
  11,536,000 always-active checks; all passed**. The 64 combinations independently
  select four fenv rounding modes, four MXCSR rounding modes, and FTZ/DAZ off,
  FTZ only, DAZ only, or both. Rates were 32/48/96/192 kHz. Two node sets included
  arbitrary interior fractions and positive-subnormal/adjacent-boundary fractions.
  Event coverage included default invalid plans, invalid nodes, startup silence,
  sustain before the first strike, signed-zero releases, a positive-subnormal
  strike, delayed keys 0/3/4/36/72, invalid controls/keys/preparation, copy/assignment,
  retrigger, all-notes-off under sustain, pedal release/tails, all 73 strikes,
  reset, successful preparation, and reuse of valid plans. The probe verified
  engine calls preserved rounding/flush controls; it did not require all audio
  exception-status flags to match.
- A direct text comparison of pre-edit/current `sample()` bodies passed.
  `objdump -d -C build/w1-013/CMakeFiles/rhodes_core.dir/src/research_engine.cpp.o`
  supplied the runtime-witness inspection described above.

### Source identities

Baseline HEAD at this worker's snapshot: `8adf435a0589b685bbc9f671d41a813808bdf016`.
SHA-256 values:

| Item | SHA-256 |
| --- | --- |
| Baseline `src/research_engine.cpp` | `f0facfcb2c28f989c79407d2e0571a868b07ec7a37511bc05f7a33efcdeb0703` |
| Baseline engine header | `7dca093866c19f18f4403f86daea96ac485d86b3e6aebc4441477bd5a8a16621` |
| Candidate `src/research_engine.cpp` | `01c2a9f7f9f7cfa4f759be56770178f32a71a28667d4ac6ab390e03ff7a95814` |
| Candidate engine header | `2a6ba0483a010d9b7a44517c5ed7c215f05d57ed6e96bd6609127322092929e4` |
| Temporary comparison probe | `c866b905b926169db68b2e9667ce2c6bc362a950948ff3505a974d35d58d5057` |

## Limits and handoff

Local runtime evidence is Linux x86-64/GNU 14.2, without fast-math. No new Windows,
macOS, mixed scalar/vector FP-unit, arbitrary compiler-flag, or cross-platform bit
identity claim is made. A compiler/target using different rounding domains for the
scalar witness and observation arithmetic needs separate validation. The
temporary probe is an ignored build artifact, not a permanent regression suite;
the assigned test worker supplies that coverage independently.

Skipping finite zero observations can omit denormal-operand status effects for
extreme subnormal plan coefficients. General audio arithmetic does not have the
control helpers' no-FP-effects contract. This optimization promises no denormal
timing guarantee and has not been benchmarked by this worker.

Root was notified when the API/source became ready and when source was frozen.
Next: integrate the independent regression tests, rebuild the host, run the full
exact Q6/legacy replay, and collect matched sparse/dense callback timings. No
speedup, new physical fidelity, or completed RT-001 gate is claimed here.
