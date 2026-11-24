# Callback cancellation and loaded state qualification

Date: 2026-11-24. Integrator observation for HOST-004 on `main`, based on
`c6d695dec8287a9b63aa814ae27d919421116165` plus the exact source hashes in
`callbacks-state-manifest.json`. This record does not complete HOST-004 or
promote physical fidelity.

## Production callback behavior

The first executable fixture included the actual production `PhotoView`
translation unit and a real controller. Eight of sixteen named cases failed:
cancellation in accepted begin callbacks, a new press canceled while ending its
previous gesture, stale rollback over a synchronous host update, and audition
cancellation during message allocation/dispatch. The original failing CTest
log, JUnit file and source hashes are retained as `photo-view-before.*` and
`baseline-manifest.json`.

The editor now advances a cancellation generation before invoking host callbacks,
clears drag state before callbacks, balances an accepted begin without publishing
a canceled gesture, and checks cancellation after both old-note release and
new-note delivery. Compensation releases only the current audition owner.
Per-parameter controller revisions distinguish a provisional UI setter from a
newer host write, including equal values and synchronous SDK observers. An edit
canceled before submission restores its provisional value only when no newer
write superseded it. Rejected submissions use the same guarded rollback.

After the initial sixteen cases passed, an independent review added ten actual
SDK-observer, equal-value rollback, keyboard and glissando cases. The expanded
optimized executable passed **26/26 CTests**. The separate controller revision
executable passed, including real SDK observer notification ordering and exact
legacy/current serialized bytes. `photo-view-expanded.*` retains the final
26-case result and per-test raw output. `photo-view-after.*` is the earlier
17-test result (sixteen view cases plus the revision test), before the additional
observer-specific guard and coverage; it is not the final source claim.

The view fixture keeps a live detached view. It establishes callback ordering,
values and actual message traffic, not attached native rendering or safe
whole-editor destruction during a host callback.

## Loaded component/controller state

All three real Release modules were rebuilt and loaded through SDK factory
interfaces. The expanded `vst3.loaded_expression_controls_state` test passed
**65,644 checks in 621 loaded callbacks**, including nine initialized-without-
setup and nine running-then-stopped panel cases, 72 fresh component/controller
reopens, 108 atomic panel rejections and 36 foreign-model v3 state cases.
Exact output is retained in `loaded-controls-state-raw.log` and the associated
CTest/JUnit files. The tested modules are bound by `native-cancellation-build.json`.

The positive cases deliver all eight controls through accepted host parameter
queues, save exact independent v3 bytes, destroy both objects, then restore them.
Panel defaults reduce the packet to exact v2; unity Expression restores exact
v1. Malformed final points reject the whole block without leaking staged values.
Connected-pair negative controls deliberately withhold parameter delivery:
controller state changes, while processor state remains stale even after saving
and reopening the two streams. This demonstrates the host-delivery boundary;
it does not claim that a particular DAW's stopped-engine saving has been fixed.

The implementation retains the official VST3 parameter path. Steinberg's
[component/controller communication contract](https://steinbergmedia.github.io/vst3_dev_portal/pages/FAQ/Communication.html)
requires host delivery and describes zero-buffer flushing while the engine is
stopped and the plug-in remains active. No second unsynchronized controller-to-
processor parameter path or state-recall workaround was introduced.

## Native keyboard conversion and painting

`vst3.photo_x11_keyboard` passed against the linked, adapted VSTGUI X11 backend
on a private authenticated Xvfb display. The fixture injects actual XCB packets
and verifies modifier/type/key/order, including queued Mod4 events after live
modifier state is clear. Its raw CTest/JUnit result is retained. A dedicated
negative-control execution is separately assigned and is not claimed here.

The native screenshot replay separately found seams inside black-key interiors
while white keys were held. Source inspection identified independent integral
pixel snapping of coincident outline/cutout edges. Key drawing now uses the SDK's
nonintegral antialiased path mode within a saved/restored drawing context. The
idle photograph, hit geometry and other overlays are unchanged. Native raster
confirmation is ongoing and must be recorded separately; the geometry unit tests
alone did not establish native raster correctness.

## Remote and remaining scope

The earlier pushed `c6d695d` checkpoint passed the Linux and Windows VST3 jobs,
Linux/Windows foundation jobs, and the sanitizer job. Its macOS jobs were canceled
before running any steps, so both workflows' aggregate conclusions are failure.
`ci-c6d695d.json` retains those job-level distinctions. These results do not cover
the newer uncommitted cancellation changes.

A source review also identified a separate emergency-release ordering hazard
inside controller message construction. It has a dedicated pending regression
assignment. Whole-editor close reentrancy, final native pixel results, current
platform CI and named-host qualification remain separate acceptance work.

Actual commands used were the configured Release Ninja builds and targeted
CTest expressions `^vst3.photo_view\\.`, `^vst3.controller_revision`,
`^vst3.loaded_expression_controls_state$` and `^vst3.photo_x11_keyboard$`.
The build uses the pinned SDK and task-local Linux dependencies documented in
worker report `w1-015.md`; no physical instrument or audio device was measured.
