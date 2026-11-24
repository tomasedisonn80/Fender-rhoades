# Wave 2 assignment 29 — independent damped contact cross-check

## Outcome and scope

The planned two-level event-based DOP853 experiment agrees with the public
`reference_peaks("damped_impact", damped_time=True)` values and times. All eight
ODE/reference comparisons and four successive-refinement comparisons are within
one tenth of the applicable frozen value or supplemental time budget. There is
no substantive discrepancy requiring a source change.

**PHY-002 remains incomplete.** This is an independent numerical cross-check of
one synthetic fixture, not continuous-extrema certification or measured Rhodes
fidelity. No native trajectory, native peak candidate, production solver, oracle
private routine, or independent auditor integration routine supplied the ODE
results. Only this report and the accompanying JSON were added to the repository.

The governing sources are the
[frozen analytic-peak slice](../../specs/CONTACT_ANALYTIC_PEAK_SLICE_20261123.md)
and [normative A54 derivation](../transient-campaign/w3_a54.md). The complete
settings, counts, event records, differences, public response, source/backend
hashes, and executable experiment source are retained in the compact
[JSON observation](../../validation/contact-peaks-damped-crosscheck-20261124.json).
The JSON contains no exported trajectory or native candidate data.

## Independent construction and event treatment

Parameters and initial conditions came only from
`validate_contact_reference.fixtures()["damped_impact"]`. The coupled contact
projection has zero modal stiffness and damping; the damper is disabled and there
are no relaunches. Therefore the independent relative-state reduction uses

```text
mu = 1 / (1 / hammer_mass + sum(bh[i]^2 / mass[i])) = 0.0016 kg
k = 20000 N/m; c = 40000 N s/m^2
delta(0) = 0 m; w(0) = 1 m/s; duration = 0.002 s
delta' = w
w' = -max(delta, 0) * max(k + c*w, 0) / mu
F = max(delta, 0) * max(k + c*w, 0)
```

The compression event is `w = 0`, with direction `-1`; accepted events require
positive compression and a positive constitutive factor. On the active branch,
the independently differentiated force is
`F' = (k+c*w)*(w-c*delta^2/mu)`. Its downward-crossing event is accepted only when
`delta > 0`, `w > 0`, and `k+c*w > 0`, so it identifies a loading force maximum.
The smooth active-branch expression serves as the event continuation; the clipped
force is used in the ODE itself. Each accepted peak also has positive/negative
event probes at `t - 1e-9 s` and `t + 1e-9 s`, respectively.

The downward `delta = 0` event terminates each solve. Its direction excludes the
initial upward onset; its filter requires negative relative velocity. Stopping
there avoids treating the later zero-force interval as force-stationarity roots.
The fine run separated at `0.0009562830255102769 s` with
`w = -0.41071968606113773 m/s` and positive constitutive factor
`3571.2125575544924 N/m`. The separated continuation has constant negative
relative speed and zero force. Terminal interpolation leaves a compression
residual of `2.25e-20 m`; that is recorded rather than presented as an exact
floating-point zero. Stored accepted-step intervals are numerical diagnostic
intervals, not certified brackets for exact dynamics.

The public reference was imported and called once, after both independent ODE
runs finished. Its scalar invariant, bisection, and quadrature are not used in
the independent event experiment. The public response is retained in full,
including its own three prescribed quadrature levels and noncertification
status; no additional ODE refinement levels were run.

## Declared settings and actual work

Both solves used SciPy `solve_ivp(method="DOP853")`, first step `1e-6 s`, dense
output for event probes, and no sampling grid. Absolute tolerances are ordered
as compression in metres and relative velocity in metres per second.

| Level | Relative tolerance | Absolute tolerances | Maximum step | Accepted steps | RHS evaluations | Event evaluations: compression / force / separation |
| --- | ---: | --- | ---: | ---: | ---: | --- |
| Coarse | `1e-8` | `[1e-12, 1e-10]` | `1e-4 s` | 16 | 349 | 24 / 25 / 22 |
| Fine | `1e-11` | `[1e-15, 1e-13]` | `5e-5 s` | 36 | 673 | 44 / 44 / 41 |

Each solve had a 20,000-RHS-evaluation cap and an active 10-second wall cap
implemented with `SIGALRM` plus callback deadline checks. Both returned success
with terminal-event status, no warnings, zero Jacobian evaluations, zero LU
decompositions, and exactly one accepted candidate for each of the three event
types. No root was rejected by the filters. Measured ODE times were about
`0.0179 s` and `0.0196 s`; the public reference call took `1.2250 s`. These are
local experiment timings, not runtime-performance claims.

The environment was Linux x86-64, Python 3.12.14, NumPy 2.3.5, SciPy 1.17.0.
The installed backend was used without installing dependencies. The JSON hashes
the SciPy IVP dispatcher, Runge–Kutta implementation, DOP853 coefficients/common
code, event-root backend, and public-reference quadrature backend files.

## Peak values, times, and agreement

| Observable | Coarse ODE | Fine ODE | Public reference |
| --- | ---: | ---: | ---: |
| Maximum compression, m | `0.00018988288079196648` | `0.0001898828808852406` | `0.00018988288088523308` |
| Compression-peak time, s | `0.0003450597378882422` | `0.0003450597379600364` | `0.00034505973795999833` |
| Maximum force, N | `6.1918072652091025` | `6.191807278632382` | `6.191807278604624` |
| Force-peak time, s | `0.0001776905622746325` | `0.00017769056225740937` | `0.00017769056225746204` |

The fine force maximum precedes the compression maximum by
`0.000167369175702627 s`. At the force peak, compression is
`0.00014788343811525173 m` and relative speed is
`0.5467377817194865 m/s`. At maximum compression the force has decreased to
`3.797657617704806 N`. These observations independently resolve the distinction
between maximum force and maximum compression for the damped fixture.

| Comparison quantity | Fine/reference absolute difference | Coarse/fine absolute difference | One-tenth budget |
| --- | ---: | ---: | ---: |
| Compression, m | `7.50810e-18` | `9.32741e-14` | `2.89883e-9` |
| Compression-peak time, s | `3.80555e-17` | `7.17942e-14` | `1.34506e-8` |
| Force, N | `2.77582e-11` | `1.34233e-8` | `1.33836e-4` |
| Force-peak time, s | `5.26651e-17` | `1.72231e-14` | `1.17769e-8` |

The value budgets are `1e-8 m + 1e-4*abs(M)` and
`1e-4 N + 2e-4*abs(M)`. The supplemental time budget is
`1e-7 s + 1e-4*abs(t_ref)`. One tenth of these was declared for this cross-check
and its refinement comparison. The largest coarse/reference difference as a
fraction of a full budget is `1.00089e-5` (force); the largest fine/reference
fraction is `2.07405e-8` (force). Agreement is a numerical observation and does
not convert either method into a rigorous error enclosure.

## Provenance, commands, and handoff

The experiment ran once from `2026-11-24T05:33:36.528487+00:00` to
`2026-11-24T05:33:39.063152+00:00`, against Git HEAD
`9966cdd2b63dcebe1f4e3865d9e74ea37f081b3a`. Hashes of both relevant Python
sources and both normative documents were identical before and after the run.

| Identity | SHA-256 |
| --- | --- |
| Independent fixture source | `c910da5da6b6ad957b709a414cef39a08ed6e8b75c6e36c9d73ef535a064dcdc` |
| Public peak-oracle source | `eb9d848a41ff3d101a5b0d86bd49e1d039000103dd5b923295bfaef332bb5c68` |
| Canonical independently obtained fixture definition | `5cdf05b26238c3344603a04247e9b68b3f57adb7034612551a1b4ae9d4133839` |
| Retained JSON observation | `0bc2b5fbd5da891989de8afd595a7055224a80fd271a7aa291d50d487dc841a0` |

Actual experiment command, exit 0:

```sh
python3 /workspace/scratch/d6370337c6a4/w2-29-damped-crosscheck.py /workspace/scratch/d6370337c6a4/Fender-rhoades /workspace/scratch/d6370337c6a4/Fender-rhoades/docs/validation/contact-peaks-damped-crosscheck-20261124.json
```

The JSON's `reproduction.script` preserves the exact executable source and
`reproduction.script_sha256` binds it. To reproduce, extract that string to a
temporary Python file, verify its hash, and pass the repository root plus a new
output filename as its two arguments. The output is created exclusively.

Only JSON consistency/source-hash inspection and scoped whitespace verification
followed this experiment. No unit suite, native solver run, oracle-author tests,
extra numerical refinement, project-status update, commit, or staging operation
was performed. The integrator can register these two artifacts as bounded
numerical evidence. The next acceptance work remains the broader independent
contact/refinement/continuous-extrema gates; PHY-002 is incomplete.
