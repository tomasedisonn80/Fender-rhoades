# Never-excited observation fast path — 2026-11-24

## Change and scope

The experimental quadrature engine can avoid fractional modal observation and
pickup evaluation for a key that has never received a successful positive strike
since its last prepare/reset. The change does not retire released or decaying
keys. It still advances every oscillator, includes all 73 pickup branches in the
same electrical network, and preserves the legacy sample path. A positive strike
marks the key excited even when its impulse underflows. The implementation relies
on the current uncoupled mechanics; any future sympathetic coupling or other
forcing must revisit this invariant.

Signed zero is part of the compatibility contract. Skipped source voltages are
explicitly negative zero. A volatile zero-addition witness detects the arithmetic
rounding behavior actually in use, including independently configured SSE
rounding. Downward rounding uses the original observation traversal.

## Actual verification

The retained [complete compatibility record](silent-observation-compatibility-20261124b.json)
replays the prior quadrature study and historical renderer fixtures using the
changed core and the contiguous QFIR traversal:

| Replay | Cases matched exactly | Frames |
|---|---:|---:|
| Q6 probe output | 2,097 / 2,097 | 2,534,400 |
| Historical WAV bytes | 24 / 24 | 44,160 |

All tracked input and binary hashes remained unchanged during that replay. Its
record identifies the parent commit and exact uncommitted source hashes, compiler
outputs, raw stream hashes, and local raw retention directory. Subsequent CMake
edits register tests only and do not change these production source files.

The [implementation review](../agents/reports/20261124b-w1-013-silent-observation.md)
includes an original-source differential check across independent fenv/MXCSR and
FTZ/DAZ settings. The [durable observation tests](../agents/reports/20261124b-w1-014-silent-tests.md)
exercise plan validation, all-branch electrical loading, released tails, reset,
transactional rejection, and floating-point environments. The
[ordered FIR tests](../agents/reports/20261124b-w1-012-qfir-regression.md) use an
independent chronological history, all 96 cursor positions, rejection, and a
cancelling history whose ordered prefix overflows.

The strict FIR oracle specifies separately rounded products and additions, so
its target alone disables contraction on GNU-style GCC/Clang drivers. An observed
GCC native-FMA mismatch also occurs in the original header; it is not evidence of
a new regression. The same-compiler original/candidate native replay matched all
20,352 ordinary and 472 overflow records reported by worker 012. Production
compiler settings are unchanged.

## Limits

These checks establish compatibility for the stated inputs and environments.
They do not establish a portable callback-time improvement, worst-case execution
time, DAW qualification, measured-instrument fidelity, or general contact
certification. Matched callback measurements are a separate experiment. The
three model identities, latency, state formats, and historical evidence remain
unchanged.
