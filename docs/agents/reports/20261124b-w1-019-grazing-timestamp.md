# Wave 1 worker 019 — anchored grazing timestamps

Actual worker 019/100, wave 1. The integrator owns PHY-002, canonical state,
CMake registration and Git. Assigned source changes are limited to
`src/contact_reference.cpp`, `tests/cpp/contact_grazing_anchor_test.cpp` and
this report. All build and temporary arithmetic-probe files are under
`build/w1-019`; `build/baseline/rhodes_contact_reference` was only executed and
hashed. No Git operation or state mutation was performed.

## Result and acceptance boundary

Implemented the bounded improvement proposed by
[worker 004](20261124b-w1-004-contact-refinement.md): an **already certified,
single-mode, force-free hammer tangent** now gets its timestamp from the
persistent analytic anchor. Its final bracket is rounded outward and validated
by the existing analytic evaluator. No certificate condition was relaxed.

On the original `grazing_tangent` fixture, all five declared maximum steps now
produce the same complete event row. The timestamp is `0.0010073` seconds; its
bracket is `[0.0010072999999999998, 0.0010073000000000003]` seconds, width
`4.336808689942018e-19` seconds, and reported gap uncertainty is exactly zero.
The unchanged event-time tolerance is `2.5e-9` seconds.

| Maximum step, microseconds | Retained baseline timestamp, seconds | New timestamp, seconds |
| ---: | ---: | ---: |
| 20 | 0.001007301220703125 | 0.0010073 |
| 10 | 0.001007301220703125 | 0.0010073 |
| 5 | 0.0010072991699218749 | 0.0010073 |
| 2.5 | 0.0010073012207031237 | 0.0010073 |
| 1.25 | 0.0010073012207031237 | 0.0010073 |

Both executables succeeded in every row and retained exactly one grazing event.
The original approximately 2.05 ns alternation already satisfied its old time
bracket tolerance. This patch improves reproducibility and localization; it
does not retroactively change that retained result or close PHY-002.

## Production change

`configure_analytic_free_motion`, `analytic_free_step`, the physical contact
laws and ordinary contact bisection remain unchanged. The new private
`analytic_grazing_root` is reached only after the existing certificate has
identified a grazing mode. It uses the same angular-frequency expression and
phase arithmetic as `analytic_free_step`: the existing double-precision
`2*pi*frequency` expression is assigned to `long double`, the sine coefficient
is formed from the anchor velocity, and phase is `atan2(-sine_coefficient,q)`.
The projection sign determines which oscillator extremum maximizes the gap.
Long-double pi identifies trigonometric maxima and complete turns.

The existing sampling ceiling permits at most one maximum in each quarter
segment. The segment selects only its period index. Its root time and enclosure
depend on the retained anchor, not on the chosen RK or caller partition.

The initial enclosure uses the adjacent representable times around the derived
center. Both endpoints and the midpoint are evaluated using
`analytic_free_step` and `point`, preserving the existing exact-zero force and
background-loss checks. The resulting endpoint rates must enclose a maximum;
the three gaps must satisfy the original gap tolerance. If phase or libm
rounding needs a wider enclosure, expansion is outward, uses a phase arithmetic
scale, and remains bounded by the original time tolerance, gap tolerance and
per-root iteration limit. There is no assumption that `long double` has extra
precision.

Absolute-time analytic evaluation is necessary here: an enclosure a few ulps
wide is smaller than the RK progress floor. These evaluations still consume
the shared RHS budget, and each bracket attempt consumes a root iteration.
No new RK trial is charged when no RK trial is performed. No work counter,
maximum, tolerance, acceptance rule or auditor was reset or enlarged.

### Endpoint ownership

An outward bracket may extend a few ulps beyond its scanned segment. The
timestamp must still lie inside that segment. Eligibility therefore uses the
canonical phase-derived time, instead of relying on a potentially rounded
endpoint-rate sign to decide which caller interval owns the event. A final
midpoint outside the segment is explicitly rejected. Adjacent segments can
find the same endpoint, and the existing overlapping-bracket deduplication
retains one event. The initial exact anchor maximum keeps its existing
degenerate event handling.

The boundary regression independently stops at `nextafter(center,-infinity)`,
`center`, and `nextafter(center,+infinity)`. The first retains no event; the
latter two retain exactly the canonical event, with no timestamp beyond the
requested endpoint. Continuing each run produces the same three ordered
events as the unsplit run. Separate runs are required because the public
minimum advance increment is 1 ps, larger than these one-ulp differences.

## New regression coverage

The new source is a standalone selector/`all` test executable. Assertions stay
active under `NDEBUG`. The independent turning-time check bisects the
Cartesian harmonic velocity expressed from the initial position and velocity;
it does not use the production phase/period root formula.

| Selector | Actual checks | Covered behavior |
| --- | ---: | --- |
| `anchor_steps` | 160 | Original CLI tangent, all 20/10/5/2.5/1.25 microsecond steps, three periods, identical complete event rows |
| `anchor_geometry` | 606 | Frequencies 87.5/200/1123 Hz, signed projections -0.5/-2/+2, both mode indices, nonzero origins through 31.25 s, unprojected spectator motion and a projected static mode |
| `anchor_partitions` | 446 | Caller boundaries near and exactly at maxima, all five step levels, identical events and analytic endpoint state |
| `anchor_boundaries` | 2148 | One representable time before/at/after a maximum, no future timestamp, independent-root enclosure, exact count/order after continuation |
| `anchor_rollback` | 80 | Trial/RHS/event/history exhaustion after temporary progress; RHS exhaustion inside analytic bracket replay; complete public rollback and deterministic continuation |

All five selectors passed in the final native executable: **3,440 checks**.
Each successful fixture checks exact-zero force, impulse, physical loss and
actuator work. Noninitial tangencies retain separated-to-separated event
branches, the unchanged time/gap limits, and ordered disjoint brackets across
periods.

The first development run exposed a test-setup mistake in the close-root
rollback fixture: seeding one 2 microsecond interval left the adaptive
controller below the intended maximum step. The setup now grows through
2/4/8/16 microsecond accepted calls before the one-interval root call. This
correction was confined to the test fixture; the final rollback selector and
the final complete suite passed.

## Commands and actual outcomes

Required AGENTS/README/project state/roadmap context was read before editing.
The read-only state checks passed: `python3 tools/project.py validate` reported
14 tasks and 26 evidence records, `sync --check` reported current generated
state, and `next --json` reported no unclaimed available task. This work
remained under the integrator's bounded assignment.

Native environment: Linux x86_64, kernel 6.18.57, glibc 2.41, Debian GCC
14.2.0-19. The following build commands succeeded with warnings as errors:

```sh
cmake -S . -B build/w1-019 -DCMAKE_BUILD_TYPE=Release -DRHODES_WARNINGS_AS_ERRORS=ON
cmake --build build/w1-019 --target rhodes_contact_reference rhodes_contact_grazing_tests rhodes_contact_tests rhodes_contact_event_split_tests rhodes_contact_split_peak_tests rhodes_contact_peak_tests --parallel 2
c++ -std=c++20 -O3 -DNDEBUG -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Werror -I include tests/cpp/contact_grazing_anchor_test.cpp build/w1-019/librhodes_contact.a -o build/w1-019/rhodes_contact_grazing_anchor_tests
build/w1-019/rhodes_contact_grazing_anchor_tests all
ctest --test-dir build/w1-019 --output-on-failure -R '^(contact\.(grazing_(miss|tangent|contact)|cap_exhaustion|scaling|split_simultaneous|split_close_roots|split_peak_rollback|peak_rollback)|grazing\.)'
```

The final CTest invocation passed **13/13** tests: the original miss/tangent/
contact tests, cap exhaustion, scaling, split simultaneous/close-root/rollback,
peak rollback and all four existing generalized-grazing selectors. The new
standalone executable is not registered by this worker because CMake belongs
to the integrator.

For each baseline/new executable, the original CLI fixture was executed with
`--fixture grazing_tangent --max-step STEP --event-tolerance 2.5e-9`, for all
five steps above. JSON was parsed in memory; the observed table above binds to
the hashed executables below. A separate bounded comparison at 5 microseconds
and 2.5 ns found **identical entire parsed JSON and exit codes** between the
baseline and new executables for `grazing_miss`, `grazing_contact` and
`damped_impact`. All six executions exited zero.

### Binary64 arithmetic sensitivity probe

To check the absence of an extended-precision assumption without claiming a
native Windows run, temporary copies of the final source and new test were
made under `build/w1-019/binary64-probe`. The exact transformation replaced
`long double` with `double` and removed numeric literal `L` suffixes using
`re.sub(r'(?<=\d)L\b', '', text)`. Both transformed sources were compiled
together with the same C++20 Release/warning flags as above and the normal
public header, then `grazing-anchor-probe all` was run. Compilation and all
**3,440 checks passed**. This is a deliberately changed arithmetic sensitivity
probe, not an additional production build, a native ABI test or native
Windows/macOS evidence.

## Final bindings

| File | SHA-256 |
| --- | --- |
| `src/contact_reference.cpp` | `70dde1bfab9c2e35457da2ebf66ff4638cb6f56f1f19a84dbe55a1752d5166fd` |
| `tests/cpp/contact_grazing_anchor_test.cpp` | `9d2e4d21e94beeaa7c52353826aeb8f319a75dbb64d75f9e8887cb130f7d68e5` |
| Unchanged `include/rhodes/contact_reference.hpp` | `00b99e61dc73686c22675bedf647c3ad1b27ba13332180431f5a42e485057fda` |
| Unchanged `tools/validate_contact_reference.py` | `c910da5da6b6ad957b709a414cef39a08ed6e8b75c6e36c9d73ef535a064dcdc` |
| New `build/w1-019/rhodes_contact_reference` | `3f5fc5ed244f320fc7d18889a5266ae923a11cee13b99e4bfab0b1348867dbec` |
| New `build/w1-019/rhodes_contact_grazing_anchor_tests` | `d867123bf8afe626a3d9d24f7c97e415d345780c01e94651fa6042516e3026dd` |
| Retained `build/baseline/rhodes_contact_reference` | `4ed2552430f5de5846adcb1a7b99521b118a12b714ad61b146ede7bab24f8f2d` |
| Probe source `build/w1-019/binary64-probe/contact_reference.cpp` | `a48fde9d26dd9c02c12de2e43cc1248c6e6c40789e1f39ef2b9d8beb5da6fc7c` |
| Probe test `build/w1-019/binary64-probe/contact_grazing_anchor_test.cpp` | `ccc8ae51f6bfd790ba4f772e9534bee37c0c7dfb1f500ebdfd6d3eb532b51526` |
| Probe executable `build/w1-019/binary64-probe/grazing-anchor-probe` | `01f68f9eac2b7f55ea008b881e4b88101e72cef6607cd22033f044d5e3b24190` |

## Integrator handoff and remaining limits

Register the new executable using library **`rhodes_contact`**, not the CLI:

```cmake
add_executable(rhodes_contact_grazing_anchor_tests tests/cpp/contact_grazing_anchor_test.cpp)
target_link_libraries(rhodes_contact_grazing_anchor_tests PRIVATE rhodes_contact)
rhodes_target_options(rhodes_contact_grazing_anchor_tests)
foreach(test_name IN ITEMS anchor_steps anchor_geometry anchor_partitions anchor_boundaries anchor_rollback)
    add_test(NAME grazing.${test_name} COMMAND rhodes_contact_grazing_anchor_tests ${test_name})
endforeach()
```

The source and test hashes above are frozen for independent review. No massive
contact matrix or new general-extrema reference sweep was run. The unchanged
0.8 decrease rule, historical failures, ordinary contact uncertainty,
independent supported-impact extrema gate and unvalidated fidelity status
remain in force. The verified bracket is a sign/gap enclosure of the existing
analytic replay, not a general interval-arithmetic proof for arbitrary
transcendental libraries or contact motion. Independent review and broader
source-bound acceptance work remain with the integrator.
