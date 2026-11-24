# HOST-002 continuation handoff — 24 November 2026

This handoff uses [081's retained execution evidence](host-midi-native-final-20261124.json)
through **10:57:30.673 UTC**, the [final criterion review](host-midi-acceptance-20261124.md)
using the **11:09:46.569 UTC** status cutoff, and a later
[closing status readback](host-midi-native-closure-20261124.json) at **11:20:37 UTC**.
The review passes C01–C03 within their frozen scope and leaves C04 incomplete.
Consult the [canonical state](../../project/state.json),
[evidence registry](../../project/evidence.json) and
[continuation-C ledger](../../project/continuation-c-20261124.json)
for current task status and actual campaign work.

## Recover ownership and scope

Read [AGENTS.md](../../AGENTS.md) and the [continuation contract](../agents/CONTINUE.md).
Recover current owners, tokens and leases from canonical JSON before acting.
Canonical revision **41** records HOST-002 as **blocked** for the missing current
macOS results, with no owner or claim. Its five scoped passing and inconclusive
evidence records remain attached for continuation. Read the live canonical
state before any ownership action. PHY-002 and RT-001
remain owned by `continuation-b-root`; preserve those lanes and their evidence.
The integrator retains the user's existing main-branch commit/push authorization;
scoped workers do not publish or mutate shared state.

## Implemented and tested behavior

All three variants implement bounded channel-specific CC123, chronological
ownership preflight, zero-frame handling, retained sustain/tails, validated
polyphonic pressure without synthesis response, and strict generic-control text
conversion. Invalid input rejects before live mutation; accepted later faults
retain partial state and fence processing. The [C01](../agents/reports/continuation-c/w5-082.md),
[C02](../agents/reports/continuation-c/w5-083.md) and
[C03](../agents/reports/continuation-c/w5-084.md) reviews pass within that synthetic
scope. Independent ownership comparisons share production DSP equations; exact
sustain/channel call ordering also relies on source review.

There are 18 controls. Existing FUIDs, models 1/2/3, gain/sustain IDs 0/1,
0/48/48-frame latency and version-1 **16-byte configuration** remain compatible.
Commands are transient and excluded from configuration; configuration does not
serialize ringing mechanics, ownership or tails. See the [frozen contract](../specs/HOST_MIDI_20261124.md).

| Registered evidence ID | Retained observation |
| --- | --- |
| `HOST-002-20261124-linux-integration` | [327/327 local CTests](host-midi-linux-final-20261124.json), zero skips; [Python](host-midi-python-final-20261124.json) reported 888 tests including one optional skip. Actual C++ execution was incremental dirty `ce59b0c` plus fixture/generator repairs. |
| `HOST-002-20261124-legacy-compatibility` | [72 loaded pairs](host-midi-loaded-compatibility-20261124.json), 304,128 identical stereo frames and 2,952 configuration checkpoints; [24 direct/filtered WAV fingerprints](host-midi-legacy-wav-20261124.json), 44,160 frames. Legacy valid inputs exclude new commands; historical WAVs are absent and Q6 was not replayed. |
| `HOST-002-20261124-native-linux-windows` | [Original snapshots](host-midi-native-fix-snapshot-20261124.json): Linux and Windows VST3 each 157/157 at `ce59b0c`, including loaded MIDI and all six SDK/module validators. |
| `HOST-002-20261124-native-platform-observation` | [081 consolidation](host-midi-native-final-20261124.json), registered **inconclusive**: those VST3 successes; repaired Foundation Linux core/sanitizer 170/170 each and Windows core 167/167; Python 888 reported with 50/58 Linux/Windows skips. macOS remains incomplete. |
| `HOST-002-20261124-criterion-review` | [Final review](host-midi-acceptance-20261124.md), documentary/**inconclusive**: C01–C03 pass within scope; C04 remains incomplete. The later closing status readback preserves that decision. |

The three core-count differences are Linux-only `/dev/full` registrations.
Windows' actual open-file replacement retained its WinError 5 skip; mandatory
identity/callback regressions passed. Counts include skips where stated; SDK
internal checks are not additional CTests.

## Verify source applicability before reuse

Exact source identities are:

- Native VST3: `ce59b0c254ab99b399f03e63094b10ce10933e71`.
- Repaired Foundation: `d4c8a6b2668e478f48facd2eeb146aff0f551506`.
- 081's code-equivalent checkout: `4cf0de7089e5f622916cc5f6a25bb4034715d781`.
- Legacy capture label: `a06d2cd60bc53e3939e89f64254aed7ed5586e7c`;
  loaded baseline `20a77e8b2033d435839df594c783837befb07938`, separate WAV baseline
  `5aac654f8569aaa782e7bce58e82b3213b57da23`.

Recompute registered artifact hashes; compare current relevant bytes and scoped
additions against the retained 99-file Linux and 91/195-file native manifests.
Read the [source audit](host-midi-source-binding-20261124.json) and
[087](../agents/reports/continuation-c/w5-087.md): legacy loaded033 currently differs
only by the specification's 428-byte reference bullet; WAV036 retains its
documented CMake test-registration difference. Source bridges establish
applicability, not clean-build/binary provenance or execution authenticity, and
do not promote later documentation/state to remotely tested inputs.

## Capture the missing native result

[VST3 run 45373649775](https://github.com/baabi-boo/Fender-rhoades/actions/runs/45373649775)
attempt 1 failed with macOS cancelled before execution. Attempt 2's
[macOS job 138393683256](https://github.com/baabi-boo/Fender-rhoades/actions/runs/45373649775/job/138393683256)
was subsequently cancelled without execution, as recorded in the **11:20:37 UTC**
closing readback; its Linux/Windows successes reuse original executions. The d4
Foundation workflow was cancelled: macOS core never ran and
interrupted arm64 Python has no terminal summary.
[Replacement Foundation 45379468043](https://github.com/baabi-boo/Fender-rhoades/actions/runs/45379468043)
at `4cf0de7` had five successful Linux/Windows jobs and two queued macOS jobs
without runners or steps at that later cutoff. No new test totals are inferred
from status-only reads, and no macOS pass is established. Cancellation causes
were not established.

Before passing [C04](../agents/reports/continuation-c/w5-085.md), retain completed,
applicable macOS SDK/module/loaded-MIDI and relevant core results, plus the full
Python terminal summary. Capture run/attempt/job, source/manifest, OS/architecture,
compiler, commands, totals/skips, original log lines and hashes. Append new
immutable evidence, preserve cancellations, then have the integrator register
and review every criterion before any completion decision. Frozen definition:
`eefa42abc22e4a677e76f1968e9c1760650b1031496e795a2fd7d6219b59701c`.

Once the missing input is available, use the state CLI to unblock and claim
HOST-002, retain new immutable results, and replace the incomplete acceptance
decision with a new reviewed record. Preserve existing inconclusive records.

## Reproduce only the needed check

From repository root, the [verification guide](../HOST_MIDI_VERIFICATION.md)
provides these exact commands; obtain its prerequisites/pinned SDK first:

```sh
python3 -B tools/verify_host_midi_archive.py
cmake -S . -B build/host-midi -G "Unix Makefiles" -DCMAKE_BUILD_TYPE=Release \
  -DRHODES_BUILD_VST3=ON -DBUILD_TESTING=ON \
  -DRHODES_VST3_SDK_ROOT="${PWD}/../vst3sdk"
cmake --build build/host-midi --config Release --parallel 2
ctest --test-dir build/host-midi -C Release --output-on-failure --no-tests=error \
  -R '^(control_text\.|events\.release_port$|vst3\.)'
python3 -B tools/generate_control_text_vectors.py --check
```

Archive verification checks retained bytes only. Use the guide's Windows setup
where applicable. Reuse old expensive quadrature/contact/timing studies while
their relevant source/input scope remains unchanged; target the actual native gap.

## Remaining named-DAW work

Execute the [unrun protocol](../DAW_VALIDATION.md) per named DAW/build and variant:
first/default-zero CC123, identical repeated commands with intervening restrikes
within/across callbacks, shared holders/sustain, same-offset ordering, co-batched
pressure/releases, text and legacy recall. Separate live/recorded routes; retain
processor parameter-queue/event traces, callback boundaries, audio, configuration
and build hashes. Mark unobservable delivery/order inconclusive. Documentation
satisfies C04's remaining-DAW-work clause; it supplies no DAW pass. Fidelity stays
`unvalidated`; PHY-002/RT-001, device deadlines and release qualification remain
outside this completion decision.
