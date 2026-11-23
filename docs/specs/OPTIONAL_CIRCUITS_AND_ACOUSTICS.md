# Optional circuits and acoustic boundaries

**Status:** future `CIR-001` and `ACO-001` requirements, consolidated from reviews
A023–A025, A056, and A058–A060. None of these powered, optical, thermal or acoustic
subsystems is implemented by the current research kernel. They follow the
identified passive Stage gate and require separately named profiles.

## 1. Revision and connected circuit

Freeze the board revision, corrected graph, component designators, ground/rail
nodes, port orientations, source impedance, controls, cable and load. Preserve
the original documentary graph and every correction's before/after connectivity,
reason, inspection evidence and review status. Hash the resulting netlist and
device models. An unverified correction stays a hypothesis.

Do not mix Peterson, Janus or other revisions into a historical hybrid. Record
device-law versions, SI parameters, provenance, uncertainties and voltage/current,
temperature and frequency domains. The detailed reference solves connected
nonlinear stages, their source ports, rail currents and relevant feedback together.
A reduced runtime representation must preserve that identified port behavior.

The combined network signal drives shared nonlinear stages. For a synthetic
counterexample, let `f(v)=v+beta*v^3` and apply tones at 700 and 1100 Hz. Then
`f(v1+v2)-f(v1)-f(v2)=3*beta*(v1^2*v2+v1*v2^2)`. Independent per-note distortion
omits cross products. This algebra specifies a numerical topology fixture; it
does not identify a historical nonlinear law. Finite shared supply impedance
can also couple branches through stored energy when their signal paths differ.

## 2. Initialization and numerical acceptance

State variables include capacitor charge, inductor flux, relevant optical and
thermal histories and the variables required by the chosen device equations.
Distinguish a solved quiescent operating point, an intentional power-on transient
and a periodic oscillator initialized with a declared phase. Musical retriggers
must not reset shared circuit history. Specify warm-up, reset and restoration.

Compare solved equilibrium with an independent long warm-up and signed small
state perturbations. Record scaled KCL/KVL residuals, iteration counts, branch
selection, stored energy, dissipated energy and supply work. Independently refine
timestep, nonlinear tolerances and multirate steps. Exhausted solver budgets
produce an explicit failed state; an unconverged iterate is not accepted output.
Use the [reference solver contract](REFERENCE_SOLVERS.md) for error allocation.

## 3. Circuit acquisition and holdout matrix

Use synchronized, calibrated input/output voltage and current, rail voltage,
aggregate current and observable bias states. Preserve actual source impedance
and load. Define amplitudes from a pilot within identified device domains.

| Experiment | Required distinction |
|---|---|
| Each tone alone and shared multitone | Local distortion versus cross products from shared nonlinear processing |
| Sparse coherent multisine with multiple phase vectors and levels | Complex excited-bin response, empty-bin products and level/phase sensitivity |
| Sustained small probe plus a strong gated tone | Probe gain/phase modulation linked to rail or bias history |
| Unequal-energy paired bursts in both orders and varied spacing | Recovery time and path dependence beyond a memoryless transfer |
| Crossed source impedances, loads and control settings | Device behavior versus source/loading artifacts |
| Load transition during a burst | Coupled transient and stable recovery under a defined change |

Select coherent integer-period windows for settled spectra. Analyze onset and
recovery separately with original timing. Calibrate polarity, delay, drift,
filtering and noise floors. Compare against a converged higher-rate result with
documented filtering and alignment: a single spectrum cannot distinguish true
intermodulation from folded products, clock errors or an unconverged solver.

Reserve complete phase/level/load/history combinations and a re-established
session. Freeze candidate, evaluator, partitions and uncertainty-based thresholds
before final test. Report complex residuals, observed floors and failed conditions
individually; do not conceal one failed configuration in an aggregate score.

## 4. Optical and thermal histories

Identify the installed emitter before selecting its dynamics. Incandescent
heating/light behavior and LED drive-to-light behavior require different evidence.
Keep emitter response separate from each LDR's conductance, unequal response
directions, illumination history and mismatch. Shared drive/supply effects remain
connected. Do not import filament lag into an LED profile by assumption.

Record full dark/illuminated preconditioning, warm-up, steps, pulses, interruptions,
temperature and recovery. Synchronously observe drive voltage/current, light at
each LDR, conductance, channel gains and supply. Characterize sensor wavelength
response, geometry and bandwidth; a scalar optical reading may not capture a
changing illumination spectrum. Include probe effects and valid calibration.

Compare drive-to-light with light-to-conductance trajectories to separate causes.
Reserve entire preconditioning/temperature/drive histories; derived crops inherit
the original partition. Compare bounded multirate runtime updates and interpolation
against a converged coupled reference using envelope, phase, sidebands and
recovery metrics. Optical tracking of hammer/tine motion is a different acquisition
and does not identify modulation-emitter or LDR memory.

## 5. Acoustic extension

An acoustic profile names amplifier, driver, cabinet, reference planes and every
receiver's coordinates/orientation. Use reciprocal force/back-EMF signs; include
terminal loading, mechanical/radiation impedance and relevant suspension/thermal
history without counting any effect twice. A downstream acoustic response alone
cannot establish amplifier–driver interaction.

Mechanical action noise is future identified work. Admit hammer, key, damper or
pedal noise only through a documented acoustic or microphonic transmission path
to the selected receiver/output boundary, with measured coupling and held-out
checks. Do not inject arbitrary clicks or noise directly into the electrical
output to suggest realism. Distinguish direct acoustic radiation from pickup or
circuit microphony and avoid counting the same mechanism twice. This preserves
the supplied research's action-noise boundary without claiming its implementation.

For stereo, specify every driver-to-receiver transfer, including cross paths,
polarity and propagation delay relative to one time origin. Excite inputs both
separately and together while retaining inactive electrical branches. A proposed
fixture uses calibrated sweeps/two-tone bursts and synchronized terminal V/I,
driver velocity and two surveyed microphones. Predict reserved levels and receiver
positions with frozen parameters. Separate acceptance for impedance, mechanical
transfer, acoustic gain/phase/delay, nonlinear residuals and recovery. Independent
per-path alignment must not erase a real delay or stereo error.

Record the runtime response policy explicitly. The baseline excludes recorded-note
playback. If an optional profile permits an impulse response, establish reuse
rights and a linear operating domain; it does not establish excursion-dependent
or thermal accuracy. A stricter no-recorded-responses policy needs procedural or
fitted causal paths. Offline recordings remain identification evidence in either
case. This future scope choice does not change the current Stage target.
