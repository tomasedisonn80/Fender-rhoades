# Wave 1, assignment 12 — host allocation and ownership audit

Read-only source review at `807e078b3670a7eb3b874dfb9e28980e29a209f9`;
only this report was written. No new callback-allocation or locking defect was
established in the inspected paths.

## Inspected behavior

`plugins/vst3/processor.cpp::process` validates buffers, parameter queues and
notes before consuming the state mailbox. It then translates ownership into a
stack-local `NoteTracker` candidate and rejects invalid transactions before
changing mechanics. Host scratch arrays are instance-owned and fixed-size in
`processor.hpp`. Caps bound frames, points, events and dispatch; the render loop
uses one-frame spans and fixed arrays rather than allocating containers.

`include/rhodes/instrument_state.hpp::StateMailbox` uses one lock-free 64-bit
atomic, enforced by `is_always_lock_free`. `begin_block`, `end_block` and
`abort_block` each perform bounded loads/CAS operations without an audio retry
loop. Pending recalls win over stale audio publication. `getState` snapshots
the mailbox; `setState` validates the 16-byte model-specific chunk and publishes
configuration, leaving live mechanics to the audio owner. State-stream calls
remain outside `process`.

For model 3, `BasicQuadratureEngine::prepare` builds its candidate mechanics and
fractional observation plan before publication. That preparation is substantial
stack/setup work, but is called during configuration rather than each callback.
The six observations, endpoint advance and fixed FIR in `process` use fixed
storage. Recall/reset clears mechanics, ownership and FIR history while retaining
the prepared immutable rate-bound observation plan.

`plugins/vst3/controller.cpp` allocates SDK parameters during initialization.
This controller path is separate from processing. Its state and parameter
methods are not made concurrently callable by the audio mailbox; host lifecycle
serialization remains the contract. Similarly, concurrent `setupProcessing`,
`setActive`, `setProcessing` or `terminate` with `process` would race on live
state. The current code explicitly relies on host synchronization for lifecycle
transitions; arbitrary concurrent lifecycle calls are outside the tested scope.

## Focused verification actually run

Using the integrator's existing Linux executables, all commands passed:

```sh
build/vst3/rhodes_vst3_host_tests allocation_guard
build/vst3/rhodes_vst3_filtered_regression_tests allocation_guard
build/vst3/rhodes_vst3_quadrature_regression_tests allocation_guard
build/vst3/rhodes_vst3_quadrature_state_concurrency_tests state_preflight_race
```

Each variant reported **114 allocation-guarded process calls** and calibration
of eight ordinary/array/aligned/nothrow allocation forms: **342 guarded calls**
total. The fixtures cover float/double output, 48/96/192 kHz, repeated rendering,
malformed-input rejection, parameter flush and recall consumption. The Q6
ordered race passed eight scenarios, 32 process calls and 100 explicit checks.
These focused commands completed together with state checks in under one second;
no benchmark or stress run was started.

Executable SHA-256 values, in command order:

```text
4ebabd10959b0ca4b23f5b262ddb1fd946708e37078cbeac86878e52982c1121
34ada7d029ac4654da4d9b4ca978573bbb88c4f8ed46efc1fbac47e4efdcb6bb
ddfee5d809b1f24ab38d739dc77572b0f6cb925f202a64f3c9b06b37ef4b0f59
032e3805396386ea14c719468b94175640988a13146a67c89a9df46c72fb5828
```

`project.py validate`, `sync --check` and `next --json` also ran successfully.
No build was performed by this worker.

## Remaining verification boundary

The allocation trap observes C++ allocation forms reached by these synthetic
host calls, not arbitrary `malloc`, deallocation latency, SDK/host-internal
blocking or every future parameter path. The host-supplied queue/event accessors
are external virtual calls; fixed test doubles cannot establish another host's
latency. The concurrency tests check selected ordered races and coherent
snapshots, not a ThreadSanitizer proof or real DAW scheduling. No real audio
device, native DAW, deadline, stack-maximum or physical-fidelity claim follows.
Keep the forthcoming callback benchmark and later DAW/device observations
separate from the allocation result.
