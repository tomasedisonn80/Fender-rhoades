# W3-46 — Batched observation storage review

**Agent:** `/root/w3_46_batch_storage`  
**Scope:** bounded RT-001 storage/lifetime review; no implementation changes.  
**Result:** no material indexing, lifetime, or allocation defect found.

## Findings

Compared the `9966cdd2` change with its parent and inspected the current headers,
implementation, and retained fractional/wrapper tests. The coefficient layout
changed from `[node][regime][key][mode][component]` to
`[key][regime][mode][component][node]`. Both contain
`73 × 2 × 3 × 4 × 6 = 10,512` doubles: **84,096 bytes of binary64 coefficient
payload**, plus the sample-rate tag. This is source-level arithmetic, not a
measured ABI `sizeof` or stack bound. The existing “84 KiB” test comment is approximate.

All storage is owned by nested `std::array` values. Plans contain no pointer or
reference into their preparing engine. Copying a plan therefore copies its
coefficients and rate binding; reuse by another same-rate engine is valid for
the current fixed synthetic parameters. Future configurable modal parameters
would require a stronger plan identity than sample rate alone.

Preparation validates fractions before constructing a complete local candidate.
Every loop uses the corresponding declared extent; all four transition
components and both regimes are populated before the rate tag and final
assignment publish the plan. Failures preserve the caller's existing plan.
The wrapper likewise prepares engine and plan candidates before assigning either.
Nested preparation can have two full plan candidates alive simultaneously; that
pre-existing preparation cost remains outside audio processing. Default plans
have rate zero and cannot match a valid engine.

`observe_cell` borrows a const plan and never copies it. Its source matrix holds
`6 × 73` doubles (**3,504 bytes** of payload). Per-key displacement/velocity and
per-mode q/v each use six-element arrays; the result contains six doubles.
These fixed automatic values and `std::optional` returns introduce no heap
allocation in the inspected path. Generated stack frames, spills, inlining,
callee behavior, and actual audio-thread stack headroom were not measured.

The wrapper owns one persistent plan, copies only the six-value result during
processing, and preserves the plan across reset. Its compile-time node-count
assertion prevents a different FIR cell extent. The retained tests explicitly
exercise default/rate-mismatched plans, rejected preparation, alternate plans,
repeated nonmutation, and guarded ordinary/array C++ allocations. Their source
coverage was reviewed; their previous executions are not claimed as new runs.

## Executed checks and limits

Read the required repository guidance and state.
`python3 tools/project.py validate`, `sync --check`, and `next --json` passed at
revision 27. Inspected the bounded Git diff and current files; Python integer
arithmetic confirmed the payload counts. No compiler, build, runtime test,
benchmark, or numerical study was executed. Root retains integration and
milestone decisions. RT-001 remains open; no physical-fidelity claim follows.

## Examined source identities

HEAD at identity capture: `1b2e4fc04e08cff41edcbe1ca420e690c9845ff4`.

| File | SHA-256 |
| --- | --- |
| `include/rhodes/research_engine.hpp` | `7dca093866c19f18f4403f86daea96ac485d86b3e6aebc4441477bd5a8a16621` |
| `src/research_engine.cpp` | `f0facfcb2c28f989c79407d2e0571a868b07ec7a37511bc05f7a33efcdeb0703` |
| `include/rhodes/quadrature_engine.hpp` | `c625a82b61eb3e721061177654c12ffa6aac0049e026b9633ae53ea2abaa773d` |
| `tests/cpp/fractional_observation_test.cpp` | `5ea8c5deab6dc8612d680413abd1f6765a463b420d34fa17f48540cd4e8dfa55` |
| `tests/cpp/quadrature_engine_test.cpp` | `dd69ecb8a5880a72d0f46862086f9ea8b52194289bfbb8a71531354d699760ca` |

Next: measure actual callback stack use on a selected host if its stack budget
becomes a release constraint; this review does not establish that budget.
