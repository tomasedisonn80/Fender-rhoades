# Reference-photo editor campaign — 24 November 2026

This continuation implements the owner's original photographic UI target and
its working controls in the native VST3 editor. It uses 100 separately spawned
workers in five waves of twenty. Each wave began only after the preceding
wave's twenty reports were complete. With three worker slots, a wave's
assignments execute in bounded batches; a wave does not imply twenty
simultaneous processes. The integrator owns main-branch commits, pushes, builds,
canonical state and final acceptance.

The [campaign ledger](../../project/photo-continuation-20261124.json) contains
every actual task name, scope, report path and completed report hash. These are
new assignments for this continuation, independent of earlier 100-worker
campaigns. Metadata validation checks report bytes and ordering requirements;
it does not itself attest that an agent or experiment ran. Actual collaboration
invocations and retained observations provide the corresponding execution record.

## Five waves

| Wave | Workers | Delivered focus |
| --- | --- | --- |
| 1 | A001–A020 | Linux controls and modifiers, state/geometry, native fixture, initial lifecycle review, dependencies and original assets |
| 2 | A021–A040 | Loaded recall, reentrant host callbacks, audition transport, visible key contours, native replay, timer behavior and campaign validation |
| 3 | A041–A060 | Native removal and reopen repairs, Cairo connection lifetime, sibling child cleanup, actual paint fixtures and preserved negative observations |
| 4 | A061–A080 | Final-source native replay, three-module gates, fresh legacy captures, original asset identity, retained screenshots, panel checks and independent audio-fixture review |
| 5 | A081–A100 | Stronger loaded audio assertions, criterion/source reviews, capture delivery repair, current CI/compiler investigation and final handoff audit |

## Review-driven changes

The exact reference remains the full editor surface. Visible controls and all
73 key regions share its transform. The work repairs rapid switch clicks,
modified-key handling, canceled host gestures, newer host-update preservation,
white-key feedback crossing black keys, and stale audition-note recovery.

Native tests reproduced unsafe removal from an accepted host callback, a
repeated-reopen Cairo failure and a child window surviving with a sibling.
Deferred teardown, checked timer startup and narrowly generated adaptations of
the pinned VSTGUI source address those observations. Original failure logs and
later successful executions are retained separately. The pinned dependency
checkout is not silently modified.

Independent review also found that an initial passing loaded-audio fixture did
not distinguish swapped tone destinations, prove opposed stereo modulation or
check the complete switch-off ramp. The strengthened fixture keeps the original
assertions and adds spectral, channel-sum and per-sample transition checks. Its
actual final run passes 96 renders and 36,000 callbacks across all three plugins.

The final capture delivery index initially retained producer-relative names
that collided with older records. The corrected repository index maps all
16 entries to their actual delivered bytes and preserves the producer manifest.
The original archives and earlier observations are unchanged.

## Results and limits

The [criterion review](../validation/photo-editor-acceptance-20261124.md) and
[numerical record](../validation/photo-editor-linux-final-20261124.json) retain
separate source-bound observations: 136 native interaction checks, 39 native
lifecycle cases, all three SDK and metadata validators, component/state tests,
96 loaded nonneutral audio renders, and 144 fresh legacy captures against
72 retained baselines with 608,256 paired stereo frames exactly equal.

The source/asset/fixture bridges explain which later commits contain the tested
bytes. They do not relabel historical runs as fresh rebuilds. Current Windows
compiler observations and the bounded MSVC compile-budget experiment remain
separate from Linux native-editor acceptance. Follow the
[current editor checkpoint](../VST3.md#current-photo-editor-checkpoint--24-november-2026)
for their recorded state.

Native Windows/macOS editor interaction, non-unit display scaling, named-DAW
operation, audio-device deadlines and measured physical fidelity retain their
own qualification work. HOST-003, PHY-002 and RT-001 are not completed by this
photo-editor campaign. No plug-in release is published by this campaign.

Final integrator gate:

```sh
python3 tools/validate_photo_continuation.py --require-complete
python3 tools/project.py validate
python3 tools/project.py sync --check
python3 tools/check_docs.py
```

Run the complete-ledger gate only after all 100 actual reports are recorded.
Task completion additionally requires the documentary and numerical evidence
registration described in the criterion review.

The [final campaign validation](../validation/photo-continuation-20261124/final-campaign-validation.json)
records the successful complete-ledger invocation after all **100 reports** were
recorded, with **20 completed reports in each of the five waves**.
The integrator then registered the documentary and numerical evidence and
completed **HOST-004** for the frozen Linux/X11 and synthetic-processing scope.
