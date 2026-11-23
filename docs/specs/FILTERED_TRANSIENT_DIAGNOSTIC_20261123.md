# Filtered transient diagnostic

Integrator adoption before waveform experiments, 23 November 2026. Implement
the independent reference, continuous kernel, full case matrix, event script,
and numerical reporting rules in [worker 025](../agents/continuation-20261123/w2-025.md).
This is an exploratory numerical experiment. It does not amend the frozen
[filtered implementation contract](FILTERED_AUDIO_20261123.md), the RT-001
acceptance criteria, or any physical fidelity claim.

## Fixed comparisons

Comparison A uses independent analytic modal trajectories sampled at the
production internal times and independently generated FIR coefficients. Its
absolute implementation limit remains **1e-12 V**. Preserve the phase `L*n`,
event producer order, zero prehistory and explicit reset semantics.

Comparison B integrates the continuous loaded-harp voltage against the same
96-host-frame Kaiser/sinc prototype, normalized by its continuous DC integral.
Use separate Gauss-Legendre orders 16, 32 and 64 on each unit host-frame cell.
Both 16→32 and 32→64 differences must meet **1e-9 V maximum** and **1e-10 V RMS**
per case. An unresolved reference makes B inconclusive; do not score it as an
engine pass or fail. Resolved exploratory comparisons use **1e-4 V maximum**
and **1e-5 V RMS**. These latter limits have no asserted audibility basis.

No fitted gain, phase, time shift or onset alignment is permitted. Keep the
48-frame delay and all startup/reset samples. The residual includes continuous
prototype/discrete-filter approximation and transient sampling effects; it is
not a separately isolated alias-voltage bound.

## Matrix and pilot

The full matrix is the exact **2,097 cases / 2,534,400 host frames** specified by
worker 025: 1,971 attacks, 72 moving-state event sequences and 54 chords, over
the nine stated rates (including the next binary64 values above 48 and 96 kHz).
The event offsets, same-offset reset/strike order and three intensities remain
unchanged. Preserve a deterministic complete plan and its hash before execution.

A development pilot may use the following fixed 36-case subset, all at intensity
1: rates 32,000; 48,000; nextafter(48,000,+infinity); 96,000;
nextafter(96,000,+infinity); and 192,000 Hz. At each rate include single attacks
on keys 0, 36 and 72; moving-state scripts on keys 0 and 72; and the sparse chord
on keys 0, 12, 24, 36, 48, 60 and 72. This is 18 attack and 18 scripted cases,
totalling **92,160 host frames**. Pilot results cannot stand in for full coverage.

Record source/input/binary hashes, library versions, exact rates, per-case raw
loaded volts and reference/error peaks, the maximum-error frame and physical
time, RMS and signed mean, both reference refinements, and errors inside/outside
the union of causal event-response windows. Preserve failures and inconclusive
cases. Do not silently overwrite an earlier run or select a favorable subset.
