# W3-45 — Batched observation arithmetic review

**Agent:** `/root/w3_45_batch_arithmetic` · **Scope:** bounded RT-001 review.
**Finding:** no material arithmetic or electrical-membership regression identified.
Only this report was added; no production code or acceptance state changed.

## Source reasoning

Compared the current observation implementation with scalar revision
`b9df4a1c1f704abeb4e1783f0dac364214dfe960`. Plan preparation retains the same
transition calculation and validation; only its storage indices are transposed.
For each node, `observe_cell` retains the scalar expressions
`T0*q + T1*v` and `T2*q + T3*v`. Modes still accumulate in increasing order,
using the same pickup weights and zero-initialized sums. The different traversal
across nodes does not introduce a reduction across nodes.

Each mode computes six position/velocity pairs, validates all six, then adds
that mode to the local node sums. A failure returns no cell. Earlier local sums
are never published, and the const observation path does not advance mechanics.
The nonlinear pickup is called only after all three modal contributions have
been combined. Its input/output finite checks also catch overflow in those sums.

The implementation visits every key, without a held-key or quiet-key skip, and
fills each 73-element source array. The unchanged harp function accumulates keys
in order with the original four/three branch weights and loaded resistance.
Silent sources therefore remain part of the electrical network.

The wrapper keeps FIR output provisional until the complete cell, one-frame
mechanical advance, and FIR push succeed. Invalid observations prevent advance;
a later failure preserves earlier output frames and clears the failed suffix.
Completed-cell timing and the reported 48-frame latency are unchanged.

## Existing evidence inspected

The fractional fixture compares batch/scalar output bits and independently
evaluates modal motion, nonlinear pickup, and the full loaded network. Its nine
rates include adjacent binary64 boundary values. Held/released/sustain/retrigger
and nonmutation cases remain present. The wrapper fixture separately uses scalar
observations and chronological long-double convolution; injected faults retain
the provisional-output contract.

Python inspection of the retained compatibility JSON counted **2,097 exact Q6
rows and 24 exact WAV rows**. The three production files below and the fractional
fixture exactly match that report's source manifest. The current wrapper test
has a later diagnostic revision, so the earlier replay does not identify that
test revision. Later native confirmation is separately recorded in
[native-platforms-20261124.md](../../validation/native-platforms-20261124.md).
This inspection did not rerun a replay, compiler, benchmark, or CTest.

## Commands and identities

`python3 tools/project.py validate`, `sync --check`, and `next --json` passed at
state revision 27. Read the required operating/project documents. Executed
`git diff b9df4a1 -- include/rhodes/research_engine.hpp src/research_engine.cpp include/rhodes/quadrature_engine.hpp`,
then inspected fixtures and recomputed SHA-256 identities with Python.
Reviewed HEAD: `18738833a778c8dec7652e224fec9621ff39f30f`.

| File | SHA-256 |
| --- | --- |
| `src/research_engine.cpp` | `f0facfcb2c28f989c79407d2e0571a868b07ec7a37511bc05f7a33efcdeb0703` |
| `include/rhodes/research_engine.hpp` | `7dca093866c19f18f4403f86daea96ac485d86b3e6aebc4441477bd5a8a16621` |
| `include/rhodes/quadrature_engine.hpp` | `c625a82b61eb3e721061177654c12ffa6aac0049e026b9633ae53ea2abaa773d` |
| `tests/cpp/fractional_observation_test.cpp` | `5ea8c5deab6dc8612d680413abd1f6765a463b420d34fa17f48540cd4e8dfa55` |
| `tests/cpp/quadrature_engine_test.cpp` | `dd69ecb8a5880a72d0f46862086f9ea8b52194289bfbb8a71531354d699760ca` |
| Compatibility JSON | `2ecedc02e1fb11628cfa98017035f0e3e92b7a428e72ed6ec281ce6f1b6b40d4` |

This source review adds no universal compiler bit-identity, SIMD, deadline, or
measured-fidelity claim. RT-001 remains incomplete. Preserve the scalar oracle
and independent network checks for future optimization changes.
