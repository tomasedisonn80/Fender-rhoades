# Quadrature and portability campaign

Actual assignments for the continuation starting at `60be432`. The request is
100 workers in five waves of 20. Up to three workers run concurrently; each
wave is batched. The integrator is not counted. Only returned assignments are
listed as completed. Planned later workers are not counted.

The [canonical ledger](../../project/quadrature-campaign-20261123.json) retains
actual agent identifiers and SHA-256 hashes of their reports. Reports describe
what was reviewed, changed, run and left unverified. More agents or tests do not
establish physical fidelity.

Completed so far: **40 of 100 assignments; waves 1–2 of 5.**

| Assignment | Wave | Scope | Report |
|---|---|---|---|
| 001 | 1 | audio diagnosis | [Report](transient-campaign/w1_a01.md) |
| 002 | 1 | ci audit | [Report](transient-campaign/w1_a02.md) |
| 003 | 1 | reference audit | [Report](transient-campaign/w1_a03.md) |
| 004 | 1 | windows cpp | [Report](transient-campaign/w1_a04.md) |
| 005 | 1 | windows python | [Report](transient-campaign/w1_a05.md) |
| 006 | 1 | quadrature design | [Report](transient-campaign/w1_a06.md) |
| 007 | 1 | host identity | [Report](transient-campaign/w1_a07.md) |
| 008 | 1 | modal api design | [Report](transient-campaign/w1_a08.md) |
| 009 | 1 | public basis | [Report](transient-campaign/w1_a09.md) |
| 010 | 1 | coefficient contract | [Report](transient-campaign/w1_a10.md) |
| 011 | 1 | runtime budget | [Report](transient-campaign/w1_a11.md) |
| 012 | 1 | coefficient generation | [Report](transient-campaign/w1_a12.md) |
| 013 | 1 | probe contract | [Report](transient-campaign/w1_a13.md) |
| 014 | 1 | event contract | [Report](transient-campaign/w1_a14.md) |
| 015 | 1 | packaging review | [Report](transient-campaign/w1_a15.md) |
| 016 | 1 | state tests design | [Report](transient-campaign/w1_a16.md) |
| 017 | 1 | legacy baseline | [Report](transient-campaign/w1_a17.md) |
| 018 | 1 | generator tests | [Report](transient-campaign/w1_a18.md) |
| 019 | 1 | heap fixtures | [Report](transient-campaign/w1_a19.md) |
| 020 | 1 | contract review | [Report](transient-campaign/w1_a20.md) |
| 021 | 2 | observation api | [Report](transient-campaign/w2_a21.md) |
| 022 | 2 | quadrature engine | [Report](transient-campaign/w2_a22.md) |
| 023 | 2 | reference runner | [Report](transient-campaign/w2_a23.md) |
| 024 | 2 | model3 codec | [Report](transient-campaign/w2_a24.md) |
| 025 | 2 | plugin variant | [Report](transient-campaign/w2_a25.md) |
| 026 | 2 | cli probe | [Report](transient-campaign/w2_a26.md) |
| 027 | 2 | cmake targets | [Report](transient-campaign/w2_a27.md) |
| 028 | 2 | benchmark metadata | [Report](transient-campaign/w2_a28.md) |
| 029 | 2 | observation tests | [Report](transient-campaign/w2_a29.md) |
| 030 | 2 | fir tests | [Report](transient-campaign/w2_a30.md) |
| 031 | 2 | engine contract tests | [Report](transient-campaign/w2_a31.md) |
| 032 | 2 | model3 host contracts | [Report](transient-campaign/w2_a32.md) |
| 033 | 2 | triple module test | [Report](transient-campaign/w2_a33.md) |
| 034 | 2 | bundle resources | [Report](transient-campaign/w2_a34.md) |
| 035 | 2 | reference runner tests | [Report](transient-campaign/w2_a35.md) |
| 036 | 2 | quadrature cli tests | [Report](transient-campaign/w2_a36.md) |
| 037 | 2 | controller state review | [Report](transient-campaign/w2_a37.md) |
| 038 | 2 | callback safety review | [Report](transient-campaign/w2_a38.md) |
| 039 | 2 | ci target review | [Report](transient-campaign/w2_a39.md) |
| 040 | 2 | integration contract review | [Report](transient-campaign/w2_a40.md) |

## Review corrections

[Assignment 40](transient-campaign/w2_a40.md) corrects the host-bound statement in
assignment 38: host blocks and parameter queues allow 8192 frames/points; the
underlying event processor allows 4096 frames and receives one-frame host calls.
The original returned report is retained unchanged.
