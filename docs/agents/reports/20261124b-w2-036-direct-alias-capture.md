# W2-036 — Retained four-case stationary primitive alias experiment

2026-11-24; actual worker 036/100, wave 2; bounded RT-001 assignment.
**The single authorized capture passed all four cases and retained all 6,144
samples.** Numerical error and stationary alias contribution are reported
separately. There is no alias-magnitude acceptability threshold, historical
budget change, C03 promotion, or task-completion claim.

The complete [retained archive](../../validation/direct-alias-initial-20261124b.json)
contains every driver output, the capture sidecars, and both worker scripts as
exact UTF-8 text with byte counts and SHA-256 identities. Its 36 files reconstruct
without executing the producer. No available artifact was omitted.

## Scope, inputs and actual execution

Read `AGENTS.md`, the README, project-state documentation and canonical JSON,
the roadmap, and the [design](20261124b-w2-025-alias-diagnostic-design.md),
[oracle](20261124b-w2-028-alias-oracle.md),
[producer](20261124b-w2-029-alias-primitive-probe.md),
[driver](20261124b-w2-032-direct-alias-driver.md), and
[independent oracle](20261124b-w2-033-alias-oracle-tests.md) and
[driver tests](20261124b-w2-034-alias-driver-tests.md) reports.
Startup `project.py validate`, `sync --check`, and `next --json` all exited 0:
the observed revision was 32, with 14 tasks, 26 evidence records, current
generated state, no unclaimed available task, and fidelity `unvalidated`.
Those are startup observations; the integrator later reported unrelated
concurrent canonical-state changes. This worker made no Git, state, product,
test, or historical-report changes.

The fixed cases use one 13,500 Hz mode or two 7,500/13,500 Hz modes, with the
design's amplitudes/phases, zero decay, 0.001 kg modal mass, and synthetic
pickup coefficients `(0.4, 800, 400000)`. Each motion is observed at 48 kHz for
1,024 samples and at 96 kHz for 2,048 samples. The boundary is one pickup's
voltage before a harp network or gain stage. The common duration is 1024/48000 s.
Index zero is stationary phase origin, not a strike. All samples were retained;
there was no fitting, alignment, trimming, retry, or adaptive repetition.

Preflight independently checked the frozen driver/probe hashes and saved
`--plan` stdout before launching the experiment. The exact command was:

```sh
/opt/codex/runtimes/codex-primary-runtime/dependencies/python/bin/python3 -B \
  /workspace/scratch/cc294adc3e2f/Fender-rhoades/tools/validate_direct_alias_toy.py \
  --probe /workspace/scratch/cc294adc3e2f/w2-029-alias-smoke/rhodes_alias_primitive_probe \
  --output-dir /workspace/scratch/cc294adc3e2f/Fender-rhoades/build/w2-036-direct-alias-initial \
  --build-record /workspace/scratch/cc294adc3e2f/w2-029-alias-smoke/build.json
```

The working directory was the repository root. The wrapper command was
`python3 -B /workspace/scratch/cc294adc3e2f/w2_036_capture.py`.
It launched the driver exactly once, from **09:12:32.280127 to
09:12:33.109262 UTC**, with observed subprocess duration **0.826957495 s**.
The driver exited 0, stderr was empty, and all four native children exited 0,
were reaped, and retained complete stdout and empty stderr. The final manifest
reported `numerical_passed=true` and `reproducibility_complete=true`.
This elapsed duration is bookkeeping, not callback-performance evidence.

Python was **3.12.14**, on Linux; its exact version string and path are retained.
Linux RLIMIT_AS was applied and read back at **268,435,456 bytes**, inherited
by the probe. The driver recorded main-thread SIGALRM preemption with its
unchanged 27-second work phase and three-second cleanup reserve. No deadline,
memory or stream cap was reached. This is a virtual-address-space limit, not
a measurement of peak RSS or a realtime/filesystem-latency guarantee.

## Observed numerical results

`P` is native primitive voltage, `S` is the independently differentiated full
harmonic polynomial, and `B` retains only original frequencies strictly below
Nyquist. The identity checked sample by sample is

\[
P-B=(P-S)+(S-B).
\]

The following values are the retained driver metrics, in volts. RMS uses every
sample in the coherent record.

| Case | P−S peak | P−S RMS | S−B peak | S−B RMS | P−B peak | P−B RMS |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| one mode, 48 kHz | 3.610445276081009e−13 | 1.1566203563346407e−13 | 0.6950707156100173 | 0.4801686202168883 | 0.6950707156100919 | 0.4801686202168736 |
| one mode, 96 kHz | 2.3092638912203256e−14 | 1.2305998157977538e−14 | 0 | 0 | 2.3092638912203256e−14 | 1.2305998157977538e−14 |
| two modes, 48 kHz | 4.0478731477833207e−13 | 1.0358286149180231e−13 | 0.27788504292219685 | 0.17506422673477412 | 0.27788504292242955 | 0.17506422673481076 |
| two modes, 96 kHz | 1.8340884366807586e−13 | 5.608108030037557e−14 | 0 | 0 | 1.8340884366807586e−13 | 5.608108030037557e−14 |

The largest P−S peak was **4.0478731477833207e−13 V** and largest RMS was
**1.1566203563346407e−13 V**, below the new diagnostic's unchanged 1e−10 V peak
and 1e−11 V RMS limits. These compare against harmonic `S`; the earlier worker
029 direct-trigonometric smoke has a separately defined floating-point
reference and remains its own immutable observation.

The largest decomposition closure residual was **2.7755575615628914e−17 V**,
in the two-mode 48 kHz case; the other three closures were exactly zero in
binary64 arithmetic. Emitted pickup voltage matched the derivative applied
to emitted summed motion with zero residual in all four cases.

Both 96 kHz cases had no removed lines, byte-identical S/B arrays, and exactly
zero analytic and samplewise alias arrays. At 48 kHz the one-mode spectrum had
six signed continuous lines, four removed; the two-mode spectrum had 24 signed
continuous lines, eight removed. Folding/coalescence left 16 sampled lines in
the two-mode full spectrum. Complex coefficients were summed before power.

The driver reported maximum direct-polynomial/harmonic discrepancy
2.220446049250313e−15 V, maximum alias-synthesis discrepancy
6.106226635438361e−16 V, maximum alias-DFT coefficient error
9.045963361904676e−17 V, and maximum S/B/alias Parseval gap
4.440892098500626e−16 V. These satisfy the predeclared 1e−12 V bookkeeping
limit. The nonzero 48 kHz aliases are measured quantities, not passed or failed
alias-acceptability decisions.

## Independent retained-evidence verification

Executed once, exit 0, without any producer execution or implementation import:

```sh
python3 -B /workspace/scratch/cc294adc3e2f/w2_036_verify_pack.py
```

This standard-library verifier checked all **27 driver files**, including the
manifest; all **26 manifest-listed artifact hashes**; all **80 array hashes**
covering **135,168 scalar binary64 values**; exact native/retained modal and
voltage values; and all 6,144 samplewise decompositions. Independently formed
P−S, S−B, P−B and closure arrays were bitwise identical to the retained arrays.
All 17 bound source/report/test/compiler/probe/receipt files remained unchanged
through capture and verification. The saved plan matched the prelaunch bytes.

The verifier independently recomputed **32 signal-metric records**, using
`sqrt(fsum(x*x)/N)` for RMS rather than the implementation's `hypot` reduction.
Every sample peak matched exactly. The largest RMS difference was
1.5777218104420236e−30 V, one ulp. The verifier allowed four ulps for reduction
agreement; this is a check on the saved arithmetic, not a changed scientific
acceptance budget. It also checked strict-band line selection, signed folding,
coalesced Parseval values, and **136 normalized direct DFT coefficients** at the
retained CSV bins using independently reduced angles. The largest coefficient
difference was 3.1401849173675503e−16 V. No full FFT or reference resynthesis ran.

The raw driver directory contains **3,831,435 bytes**, including its manifest.
The final archive is **4,040,325 bytes**, below 16 MiB, and contains all 27 driver
files, seven capture/check sidecars, and the two worker scripts. Their original
text totals **3,887,364 bytes**. All 36 texts were reconstructed from the final
archive and compared byte for byte with their originals; no producer rerun was
used. Documentation integrity was checked after writing this report.

## Provenance and handoff

| Input or retained result | SHA-256 |
| --- | --- |
| Driver | `02733c24673d70f80734959a2a2aa1f143c33187d409c2b44605dd4cda6de4b2` |
| Oracle | `2bfed9f0c3e75fce9fed9a866756f9caa4d45728a7150912790b4caf2a71ff6c` |
| Private native probe | `5d4f606d2bc71fd6ec7030250d7e84bf8618b5763fe7e526234393f1340dcb73` |
| Explicit build receipt | `7a3cd81cc91c894115862b0129d5b1558e4ccaac7f72979834bf4ec0e23e55a5` |
| Frozen plan | `62622fe2f1e004b13a4214c30497e213403bee2f4317d57f562e29e0fe1e9688` |
| Raw manifest | `507e7ebc30edea94bf9b4929805b18acad6fd9d08abdd420b0c394b7130ba489` |
| Raw metrics | `ff63441a891d5d29f06f33b2faab7abf017d4be7e94620ad40cd2a00620b9a36` |
| Complete exact-text archive | `2e6bbe63ae26579ad578bb545ea415e252620ce1668e87b886f8c6a8926d3a9d` |

Worker 029 observed the original GCC 14.2.0 `-O2` compilation. This experiment
verified that receipt against the actual compiler, source and probe bytes;
neither the driver nor this worker independently observed or repeated that
compilation. No native Windows/macOS claim follows from this Linux capture.

Root may integrate this report and archive as scoped stationary-primitive
numerical evidence. Whole-engine direct convergence, damping, strike/retrigger/
release transients, finite windows and filters, alias audibility, physical
fidelity, allocation/locking policy and realtime deadlines retain their separate
uncompleted scope. RT-001 and C03 acceptance are not promoted by this result.
