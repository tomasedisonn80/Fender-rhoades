# W2-037 — retained grazing-tangent refinement slice

24 November 2026; actual worker 037/100, wave 2. This bounded PHY-002 assignment
owns this report and the new
[exact capture archive](../../validation/grazing-refinement-slice-20261124b.json).
The integrator owns Git, canonical state, product files, and the shared build.

## Observed result

**All 13 declared `grazing_tangent` native runs completed successfully.** Each
retained exactly one hammer `grazing` event at `0.0010073` seconds, with the
identical bracket `[0.0010072999999999998, 0.0010073000000000003]` seconds.
Its width is `4.336808689942018e-19` seconds. Both endpoints are the adjacent
representable binary64 times around the independently prescribed center.

The unchanged refinement auditor reports **zero failures for the supplied
slice**, including the original final-step 0.8 decrease policy. Its original and
depth-1 results remain **`incomplete`** because the other 25 fixtures are absent.
This is a current observation of the tangent fix, not a new complete contact
acceptance result or an amendment to historical evidence.

| Retained check | Original matrix | Depth-1 extension |
| --- | ---: | ---: |
| Selected unique slots | 8 | 13 |
| Required full-matrix slots | 208 | 338 |
| Missing slots, all belonging to other fixtures | 200 | 325 |
| Reported failures for supplied data | 0 | 0 |
| Overall auditor status | `incomplete` | `incomplete` |

The original missing-reason list contains 25 absent fixtures and three absent
signed-coordinate comparisons. The extended list additionally retains all 325
missing slot identifiers. No absent fixture or rescaling gate was removed to
obtain a pass.

## One declared native capture

Read `AGENTS.md`, README, the generated state, roadmap, canonical task definitions,
the [worker 004 diagnosis](20261124b-w1-004-contact-refinement.md), and the
[worker 019 implementation report](20261124b-w1-019-grazing-timestamp.md).
The required startup commands actually run were:

```sh
python3 tools/project.py validate
python3 tools/project.py sync --check
python3 tools/project.py next --json
```

All exited zero: revision 32, 14 tasks, 26 evidence records, generated state
current, no unclaimed available task, fidelity `unvalidated`. This assignment
remained under the integrator's PHY-002 claim; no task state was changed.

The existing native executable matched the assigned SHA-256 before execution.
The read-only sweep `--plan` was retained first and its one-fixture, 13-command
matrix checked. The following generation command then ran **once**:

```sh
python3 tools/run_contact_sweep.py \
  --executable build/vst3/rhodes_contact_reference \
  --output-dir build/w2-037-grazing-refinement \
  --fixture grazing_tangent --extra-step-levels 1
```

The actual launch used the absolute output directory and Python executable
recorded in the archive. It began at `2026-11-24T09:14:06.574435+00:00` and ended
at `2026-11-24T09:14:06.897268+00:00`, exit zero, elapsed 0.322835315 seconds.
This elapsed value describes candidate generation, not audio callback cost.
The sweep retained its default 30-second timeout per native process; the wrapper
had a 420-second overall timeout. There was no timeout or retry.

The declared settings comprise maximum steps 20, 10, 5, 2.5 and 1.25 microseconds;
event tolerances 10, 5 and 2.5 nanoseconds; and local tolerance scales 1, 0.1 and
0.01. Original event/local axes are retained at 2.5 microseconds, and extension
axes are retained at 1.25 microseconds. Deduplication produces 13 unique commands,
not a Cartesian product. All native stderr streams are empty.

No build, baseline-binary execution, full 338-run sweep, CTest run, or independent
Radau solve was performed. Python under `-S` performed the retained analysis:

```sh
python3 -S /workspace/scratch/cc294adc3e2f/w2_037_analyze.py
```

That script calls the existing
`validate_contact_reference.refinement_gates(runs, extra_step_levels=1)` without
changing its fixture set, resolution estimate, budgets, or 0.8 rule. The earlier
interactive inspection evaluated the same retained runs; no further native
process was launched. Both the capture and analysis scripts are embedded in the
archive as exact UTF-8 files, so their commands and checks remain inspectable.

## Independent event and physical checks

The fixture prescribes amplitude `A=0.0001 m`, frequency `200 Hz`, and turning
time `tc=0.0010073 s`. Its force-free motion is
`q(t)=-A*cos(omega*(t-tc))`, with stationary hammer position `-A`. Thus the hammer
gap is `A*(cos(omega*(t-tc))-1)`, which is nonpositive; the tangent has zero
penetration without a force impulse. The gap rate
`-A*omega*sin(omega*(t-tc))` is positive at every retained left endpoint and
negative at every right endpoint. These checks use the prescribed constants,
not a fitted candidate time or the production phase-root routine.

For every native run, the analysis checked exactly one correctly classified
event, time within the output duration, enclosure of the prescribed center,
width within that run's unchanged event tolerance, and zero event work and gap
uncertainty. All 13 complete event dictionaries are equal.

There are **663 exported sample observations**: 51 per run. Hammer position and
velocity remain exactly `-0.0001 m` and zero. Every reported hammer/damper force,
hammer/damper impulse, modal/hammer/damper physical loss, clipped contact loss,
actuator work and launch work is exactly zero. All reported sampled compression
and force maxima are zero. These are sample and diagnostic observations, with
the nonpositive-gap identity applying to this particular analytic fixture.

A supplemental elementary harmonic check found maximum sampled displacement
error `2.710505431213761e-20 m` and velocity error
`2.7755575615628914e-17 m/s`. These values are retained observations; they introduce
no new acceptance threshold or general independent contact reference.

## Relationship to the historical failure

The immutable
[full historical result](../../validation/contact-split-depth1-20261123.json)
contains ten original-grid failures. Its last two tangent event-time differences
are `0.010216619828488243` and `0.010216609384085291` of the frozen time budget.
They fail the required 0.8 decrease relation even though each is within the
accuracy budget. Its depth-1 tangent step sequence ends in zero and already
passes that separate extended check.

The current tangent step event-time differences are **exactly zero at all four
successive pairs**. Event/local-axis event-time differences are also zero.
Every non-event quantity in the original and extended tangent step/event/local
comparison tables is exactly equal to the corresponding historical quantity.
The existing resolution rule classifies the current small mechanical differences
as below resolution; it was not relaxed. All event/local comparisons are zero.

The original tangent issue is therefore absent in this current, fully populated
one-fixture refinement slice. **The other nine historical failures were not
reevaluated here**, and their original messages are retained in the new archive.
The old result remains ten failures for its original source and inputs. No new
whole-matrix failure count is asserted.

## Provenance and exact retention

Twenty-one bound files matched before capture, after capture, and before archive
assembly: contact implementation/header/CLI, sweep/auditor, CMake context,
frozen specifications, prior reports/result, native binary, library, objects,
and relevant compile/link metadata. Root held those inputs throughout the short
native capture. The metadata describes the existing Linux Release build; this
worker did not witness or repeat its compilation.

| Binding | SHA-256 |
| --- | --- |
| Native executable | `3f5fc5ed244f320fc7d18889a5266ae923a11cee13b99e4bfab0b1348867dbec` |
| `src/contact_reference.cpp` | `70dde1bfab9c2e35457da2ebf66ff4638cb6f56f1f19a84dbe55a1752d5166fd` |
| Public contact header | `00b99e61dc73686c22675bedf647c3ad1b27ba13332180431f5a42e485057fda` |
| Native CLI source | `36f30eebe08856989eeefd940acdff491c8f9cfc217342893f8d113c61865113` |
| Sweep tool | `81b45563f7409b4c6378a7867d7b8d33394676482c14e165f9919c1bc40e627a` |
| Unchanged refinement auditor | `c910da5da6b6ad957b709a414cef39a08ed6e8b75c6e36c9d73ef535a064dcdc` |
| New exact capture archive | `81e5f5a8a366c2c6d581b928cfe6e0761bece55744f62e87eebb8d22dfb11301` |

The archive is **2,397,105 bytes**. It retains **47 exact text files totaling
1,698,909 original bytes**: all 28 sweep files, 13 control/analysis files, four
build-metadata files, and two scripts. All 26 native stdout/stderr hashes were
verified against the sweep manifest; all 13 aggregate runs equal their native
JSON documents. Every embedded UTF-8 file was reconstructed in memory and its
byte count and SHA-256 checked. No available capture file was omitted.

For reconstruction, encode each entry's `text` as UTF-8 and verify its `bytes`
and `sha256` before writing it to a new directory. Paths in the embedded original
records describe this run; changing paths for a later replay creates a distinct
artifact and must not rewrite this archive.

## Remaining boundary

This establishes the selected synthetic tangent's behavior for the retained
Linux binary. It does not prove arbitrary continuous extrema, portable
transcendental interval bounds, native Windows/macOS behavior, the other contact
fixtures, physical fidelity, or PHY-002 completion. The next acceptance step is
a separately declared current-source study of the remaining convergence issues
and general contact coverage, preserving this and all earlier observations.

Only the two assigned repository paths were created. No product, state, CMake,
historical report, or Git mutation was performed by this worker.
