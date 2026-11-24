# Native photo-editor removal and reopening

Date: 2026-11-24. Integrator: `photo-editor-root`. Task: `HOST-004`.

## Result and source boundary

All **13 Linux native lifecycle selectors passed** against the actual loaded
Direct bundle. The fixture links public SDK hosting interfaces and XCB; the
production editor and VSTGUI run exclusively inside the loaded module. The
[source, executable, generated SDK adaptation and module manifest](lifecycle-after/manifest.json)
binds the observation. The [raw CTest output](lifecycle-after/final-13.log) and
[JUnit result](lifecycle-after/final-13.xml) retain the exact run.

| Native case | Required observation |
| --- | --- |
| Ordinary close | Structural teardown completes synchronously and balances a held gesture. |
| Removal inside begin, perform or end | Active native dispatch returns safely; accepted gestures balance; no later edits are accepted by the pending attachment. |
| Nested event pumping | Two actual close-timer turns defer while the original callback remains active. |
| Timer registration refusal | Removal returns false, attachment remains valid, a fresh native gesture works, and a later ordinary close succeeds. |
| Repeated removal | Exactly one pending close timer; no duplicate gesture completion. |
| Reopening | Pending attachment is rejected; the same public view accepts attachment and input after completion. |
| Parent destruction | Immediate host-parent destruction during removal does not crash the active native callback or later cleanup. |
| Host releases its view | Deferred ownership keeps the view alive; cleanup retires registrations and returns host references to their initial count after unload. |
| Focus loss | Removal requested from the canceling end callback receives the same lifetime protection. |
| Ten reopen cycles | Every attachment paints the photograph and accepts a balanced wheel gesture using the identical public view. |
| Two editors | Both share one native connection; closing the first removes its child while the second repaints and accepts input; closing the last retires all registrations. |

Every attachment waits for a viewable XEmbed child, observes a black-cleared
drawable, dispatches Expose, and requires a real 16 ms redraw plus four independently
sampled photograph patches. Their tolerance of eight values per RGB channel is a paint-readiness
gate, separate from the exact pixel comparisons in the full interaction replay.
Closure checks require synchronous retirement of native registrations before a
bounded geometry-only wait observes the X server; that wait dispatches no plugin
work. Fixed-size event buffers supply all 32 bytes required by XSendEvent.

## Repairs

The editor now distinguishes interaction cancellation from native frame teardown.
A removal requested inside editor or view callbacks registers a checked platform
timer, disables further interaction for that attachment, and retains the editor.
Structural close occurs after callback depths and frame event processing unwind.
Nested pumping keeps the timer pending. Scheduling refusal preserves the public
attachment and permits retry. Ordinary close remains synchronous. The destructor
does not schedule work or resurrect the editor.

Three narrowly guarded adaptations are generated in the build directory from the
unchanged pinned SDK. The Win32 timer reports whether SetTimer succeeded. The X11
backend retains its Cairo device until the final frame exits, then finishes it
before disconnecting XCB and clears released caches. A separate child-window
destructor destroys only the editor-owned XID, consuming an expected BadWindow
when the host already destroyed its parent. The checked request introduces one
UI teardown roundtrip; none of this work enters the audio process callback.

Cairo documents that releasing a reference and finishing external device resources
have different effects. This supports the explicit final-connection cleanup;
the particular retained Cairo cache behind the earlier intermittent reopen failure
was inferred, not instrumented: [Cairo device API](https://www.cairographics.org/manual/cairo-cairo-device-t.html#cairo-device-finish).
The dependency guide and worker reports 050 and 057 record pins, license provenance,
generated-source hashes and 22 / 13 isolated configure checks respectively.

## Negative observations and fixture corrections

The [original accepting-host probes](lifecycle-before/README.md) retain the removal
crash. Worker 052's [single negative fixture run](lifecycle-negative/outcome.json)
against the frozen before-repair bundle exited 139, with empty original stdout and
stderr. It was not labeled a positive acceptance result.

The initial eleven-selector run exposed a cross-connection observation race in
immediate child-disappearance checks. The strengthened structural check retained
the synchronous requirement while allowing server cleanup to become observable.
A later run reproduced Cairo's reopen assertion. The new paint gate initially
read an unmapped child, causing XGetImage BadMatch; constructor unwinding then
masked that assertion by unloading the module before live interfaces. Bounded
XEmbed mapping readiness and orderly constructor-failure cleanup corrected the
fixture without relaxing the photo criteria. The two-editor test subsequently
failed because the closed editor's child survived; the separate child cleanup
adaptation resolved that failure. Intermediate logs remain in
`lifecycle-investigation/` rather than being replaced by the passing result.

## Remaining qualification

This is Linux native SDK-host evidence, using deterministic SendEvent dispatch.
The fixture invokes the registered FD callback to drain native packets; it does
not establish behavior in every host's descriptor-readiness loop. The full XTEST
interaction replay remains separately source-bound. No result here establishes
Windows or macOS native editor execution, non-unit device DPI, named-DAW project
save/restore, or physical Rhodes fidelity. The earlier Linux/Windows CI success
applies to its recorded commit; canceled macOS jobs establish no execution.

The processor, queue ownership and capacity, class/model/parameter identities and
state wire formats remain unchanged. Host delivery or zero-buffer flushing is still
required for stopped processing state to reflect controller edits. This repair
does not add an unsynchronized controller-to-processor state path.
