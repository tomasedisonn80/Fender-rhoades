# Three-variant callback benchmark protocol

Prospective `RT-001` observations; no timings are recorded here. The
[plan](../validation/quadrature-benchmark-plan-20261123.json) fixes arguments,
order, counts, evidence and analysis. Record its canonical hash before measurement;
preserve this version if the protocol changes.

## Timed operation and matrix

Use the three Release targets from the [VST3 guide](../VST3.md).
Time one concrete `RhodesProcessor::process()` call, including
host getters, preflight, ownership, automation, output clearing, rendering and
mailbox publication. Setup, schedule construction, warmup, resets, verification
and reporting remain outside the timestamps. Keep timer overhead and scheduling
interruptions; apply no subtraction or fitted correction. See the
[source](../../apps/vst3_benchmark.cpp) and [methodology](../BENCHMARKING.md).

Every row runs both rounds on direct, filtered and quadrature:

| Profile | Hz | Frames | Keys | Schedule | Bits | Measured / warmup |
|---|---:|---:|---:|---|---:|---:|
| p01 | 48000 | 64 | 0 | held | 32 | 512 / 32 |
| p02 | 48000 | 64 | 1 | held | 64 | 512 / 32 |
| p03 | 48000 | 64 | 73 | held | 32 | 512 / 32 |
| p04 | 48000 | 64 | 73 | pedal | 64 | 512 / 32 |
| p05 | 48000 | 256 | 73 | gain | 32 | 512 / 32 |
| p06 | 192000 | 64 | 73 | held | 64 | 512 / 32 |
| p07 | 192000 | 64 | 73 | pedal | 32 | 512 / 32 |
| p08 | 192000 | 32 | 73 | gain | 64 | 512 / 32 |
| p09 | 192000 | 1 | 1 | held | 32 | 1024 / 32 |
| p10 | 192000 | 1 | 73 | gain | 64 | 1024 / 32 |
| p11 | 48000 | 8192 | 73 | pedal | 64 | 32 / 4 |
| p12 | 48000 | 64 | 73 | gain | 64 | 512 / 32 |

Totals are **72 processes, 40,128 measured callbacks, 2,136 warmup callbacks,
3,846,144 measured host frames and 338,304 warmup host frames**. Each executable
resets after warmup. Its existing half-second retrigger schedule, binary32 `0.7`
intensity and initial gain/sustain remain unchanged.

## Order and environment

Run profiles p01–p12 in round one and p12–p01 in round two. Rotate the first
round's variant order by profile index; reverse each profile's order in round
two. Each variant occupies each position eight times. Execute the plan's exact
argument lists sequentially, with a new process and exclusive output files.
Never adapt repetitions, discard outliers or overwrite/retry a result.

Before capture, finish compilation and other intensive work. On this Linux
host, choose the lowest permitted logical CPU and pin every benchmark child to
it before execution. Verify and retain that binding; inability to apply it
ends this protocol with an operational record. Keep inherited priority and power
settings. Record remaining competing activity, including other workers.

Retain compiler, complete flags/linkage, source and executable hashes, SDK pins,
CPU/topology, OS/kernel, cgroup limits, affinity, scheduling, clock and available
power/frequency information. Unknown information stays explicit. Preserve the
benchmark's raw metadata; an external manifest supplies missing fields and the
launcher affinity. Recheck compiled inputs and binaries after capture.

## Interpretation and retention

Require correct identity/workload, successful callback accounting, finite stereo
output, correct silence, cursor, owners and getter counts before treating a run
as a completed workload. Retain failures and omissions. Compare all 36 pairs of
same-variant repeat fingerprints; different variants need not produce equal audio.
Recompute each run's mean, nearest-rank percentiles, maximum and strict nominal
miss count from every raw duration. Report both rounds separately.

The nominal period is `1e6 * frames / rate` microseconds. It is a reporting
comparison, with no timing pass threshold. Preserve raw JSONs and execution
records in durable evidence with hashes. This finite synthetic matrix supports
measured callback observations; device deadlines, long-tail behavior, allocation
policy, physical fidelity and [numerical reference error](QUADRATURE_AUDIO_20261123.md)
retain their separate evidence requirements.
