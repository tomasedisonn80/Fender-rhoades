# Wave 2 assignment 28 — conservative and forced-clamp domain audit

Date: 2026-11-24. Assigned owner: `/root/w2_contact_implementation/w2_28_closedforms`.
Scope: independent mathematical review of the conservative and forced-clamp
paths in `tools/contact_peak_oracles.py`, using the independently assembled
fixtures in `tools/validate_contact_reference.py` and the frozen analytic slice
and A54 derivations. This report is the only changed path.

## Finding

No concrete formula or domain defect was found in these two paths for their
frozen fixtures. The reduced mass, common conservative force/compression peak,
duration coverage, absence of later contact, forced-clamp endpoint compression
maximum, complete zero-force plateau, and disabled-damper distinctions agree
with the stated equations and fixture definitions.

This is an analytic source review. It adds no executed test result, numerical
certificate, candidate acceptance result, or measured instrument claim.
**PHY-002 remains incomplete**, including its general continuous-extrema gate.

## Inputs read and scope control

Read `AGENTS.md`, `README.md`, `docs/PROJECT_STATE.md`, `docs/ROADMAP.md`, and
`project/state.json` for the operating contract and current research boundary.
Read `docs/specs/CONTACT_ANALYTIC_PEAK_SLICE_20261123.md` and normative
`docs/agents/transient-campaign/w3_a54.md` in full. The current state identifies
PHY-002 as in progress and specimen/acquisition work as blocked.

Reviewed these implementation sections:

- `contact_peak_oracles.py`: `_peak`, `_zero_peaks`, `_free_domain`,
  `_conservative`, `_forced_clamp`, and the independent fixture/definition-hash
  dispatch in `reference_peaks`.
- `validate_contact_reference.py`: `Fixture`, its stiffness/damping methods,
  the free/forced fixture construction, `law`, `observe`, `rhs`, and the two
  corresponding `exact_free` branches and event definitions.
- Existing oracle test names before this derivation review, then the bodies of
  `test_complete_finite_records_and_independent_definition_hash`,
  `test_conservative_closed_form_with_inactive_second_mode`, and
  `test_clamp_enabled_zero_force_and_disabled_damper_are_distinct`.

Those existing tests already address reduced-mass arithmetic, simultaneous
conservative peak times, inactive second-mode projection, the enabled zero
force plateau, and disabled-damper labeling. No duplicate tests were added.
The damped root/time solver and rounded-preload certificate were outside this
assignment's mathematical audit.

## Independent reduction and dimensions

Let `q` be the first modal displacement, `x` the hammer position, and
`delta = x - q` the gap while positive. The frozen free fixture has first modal
mass `m = 0.002 kg`, hammer mass `mh = 0.008 kg`, projection `bh = (1, 0)`,
zero clearance, first frequency zero, zero background damping, and no relaunch.
The damper is disabled. Therefore its only first-mode/hammer forces are the
reciprocal contact pair:

```text
m*q'' = F
mh*x'' = -F
delta'' = -(1/m + 1/mh)*F = -F/mu
mu = m*mh/(m + mh)
```

The reciprocal force signs used in `observe` give this same reduction;
the incident hammer velocity is also the initial relative speed because the
modal velocities are zero. Using the nominal decimal constants gives
`mu = 0.0016 kg`. The implementation computes the reduced mass from the actual
binary64 fixture values, rather than importing the old auditor's precomputed
`MU` constant. That distinction is appropriate: decimal labels and their
binary64 representations need not be identical exact real numbers.

The force law on positive compression is
`F = max(0, delta*(k + c*w))`, with `w = delta'`. Its dimensions are:

| Quantity | SI dimension |
| --- | --- |
| `m`, `mh`, `mu` | kg |
| `delta`, `x`, `q` | m |
| `w` | m/s |
| `k` | N/m |
| Contact coefficient `c` in `c*delta*w` | N s/m² |
| `k + c*w` | N/m |
| `F` | N |
| `omega = sqrt(k/mu)` | 1/s; radians are dimensionless |
| `w0/omega` | m |
| `k*w0/omega` | N |

The contact coefficient is not the modal viscous coefficient. The latter is
`2*m*beta` in the independent definition, with units N s/m; both frozen free
fixtures have `beta = (0, 0)`.

## Conservative maximum and full-duration domain

For `c = 0`, `delta(0) = 0`, and `delta'(0) = w0 > 0`, the positive-contact
solution follows directly from the reduction:

```text
delta(t) = (w0/omega)*sin(omega*t)
w(t) = w0*cos(omega*t)
omega = sqrt(k/mu)
t_separation = pi/omega
t_max = pi/(2*omega)
delta_max = w0/omega
F_max = k*delta_max
```

On the open contact interval `(0, pi/omega)`, compression is positive. Its
derivative has exactly one zero, at `pi/(2*omega)`, and changes from positive
to negative there. Thus the compression maximum is unique. Multiplication by
positive constant `k` gives the same unique force maximum. The time is one
quarter of the *relative contact oscillator's* period `2*pi/omega`; it is not
a quarter period of the 650 Hz second mode.

A54 reports the evaluated targets `0.000282842712474619 m`,
`5.656854249492381 N`, and `0.0004442882938158366 s`. Those numerical values
were read, not independently recomputed in an executable during this review.
The separation time is twice the quoted peak time, approximately
`0.0008885766 s`, strictly inside the fixture's `0.002 s` duration. The
implementation explicitly requires `separation < duration`, so it does not
present a full-impact maximum when the declared window ends before impact
completion.

At separation the relative speed is `-w0`. Once the gap is nonpositive, the
contact force is zero. With no first-mode support stiffness, damping, other
contact, or relaunch, both first-mode and hammer velocities remain constant.
Consequently the gap thereafter is `-w0*(t - t_separation) < 0`; another
contact cannot occur anywhere in the remaining time window. Momentum gives
the independent consistency values `q' = 1.6 m/s` and `x' = 0.6 m/s` for the
nominal constants, matching the coefficients in `exact_free`. This is a
symbolic consistency check, not an executed trajectory comparison.

The complete-domain conclusion depends on the frozen free assumptions. It
would not follow for a supported first mode, nonzero secondary-mode
excitation/projection, an active damper, or a later relaunch.

## Why the second mode remains inactive

The second mode has nonzero frequency `650 Hz`, but `bh[1] = 0`, zero initial
position and velocity, and no damper force. Its scalar homogeneous equation is
therefore `m2*q2'' + k2*q2 = 0` with zero initial data. The unique solution is
`q2(t) = 0`. There is no linear modal coupling in the independent equations.
The nonzero second-mode frequency and pickup projection cannot feed motion
back into the hammer; the pickup calculation here is an observation only.

`_free_domain` checks the zero initial modal state, `bh = (1, 0)`, first-mode
zero frequency, zero background damping, absent relaunch, zero damper
stiffness, and zero clearance. These conditions are sufficient for the two
frozen paths. This review does not interpret the private helpers as a general
validator for arbitrary externally supplied physical configurations.

## Forced-clamp maximum, plateau, and energy convention

The forced fixture retains the free assumptions but sets `x0 = 0.0001 m`,
`w0 = -1 m/s`, `k = 20000 N/m`, and `c = 40000 N s/m²`. Hence
`k + c*w0 = -20000 N/m`. While the gap is positive, the unclipped force is
negative and the unilateral force is exactly zero. Zero force leaves both
initially resting modes at rest and leaves the hammer velocity constant, so
the same negative force factor persists. Thus:

```text
gap(t) = x0 + w0*t
compression(t) = max(x0 + w0*t, 0)
F(t) = 0
```

Compression decreases strictly from `0.0001 m` to zero at `0.0001 s`, then
stays zero through duration `0.0002 s`. Its positive global maximum is unique
at the endpoint `t = 0`. Force is zero at every time including both endpoints
and separation, so its full maximizer set is `[0, 0.0002] s`. A single zero
time would discard valid ties. `_forced_clamp` and `_zero_peaks` retain the
required initial-time compression maximum and whole enabled-force plateau.

After separation the relative speed remains negative, so the gap continues
to decrease and cannot close again. The implementation's domain guard checks
positive initial gap, negative relative speed, and nonpositive force factor;
these are precisely the conditions needed for this monotone solution.

The independent law retains elastic compression energy `k*delta²/2` even on
its clipped-force branch, and assigns loss power `-k*delta*w >= 0` there.
Therefore zero mechanical impulse is compatible with the disappearance of
the nominal initial `0.0001 J` contact energy. The old analytic fixture's
loss expression is consistent with that convention. This is a property of
the declared synthetic clipped law, not evidence about measured hammer-tip
hysteresis or material behavior.

## Disabled damper semantics

Both frozen fixtures use `kd = 0` and `cd = 0`; `law` explicitly returns
zero force, contact energy, and contact loss when stiffness is zero. The
slice contract additionally requires disabled peak records to carry numeric
zeros. `_zero_peaks(..., disabled=True)` reports `kind = disabled`,
`time_status = disabled`, `enabled = false`, and the complete zero interval.

This convention is distinct from the enabled hammer's zero-force plateau,
which reports `kind = plateau`, analytic time status, and `enabled = true`.
Disabled compression zero is a diagnostic convention, not a claim that the
raw geometric damper gap is identically zero. For example, the conservative
first-mode displacement can make the independently observed damper gap
positive even though the damper remains disabled. Neither fixture can obtain
a physical damper force from that gap under the declared law.

## Evidence class and limitations

The arguments above establish the continuous-time extrema for the declared
equations and frozen domains. The module labels their coverage
`independent_analytic_domain_argument`. Its duplicated `[M, M]` values are
analytic targets evaluated in binary64; this review does not reinterpret
them as outward-rounded exact-real interval certificates for `sqrt` or `pi`.
The conservative existing unit test uses independent Decimal arithmetic;
its presence was inspected, but its result was not rerun or claimed here.

No candidate data, C++ trajectories, solver peaks, runtime benchmarks,
NumPy/SciPy integrations, new scalar executable calculation, or test suite
was executed. A54's historical witness results remain that report's evidence.
No measured specimen data were supplied or inferred. The general ODE peak
search, complete PHY-002 acceptance, and calibrated Rhodes fidelity remain
outside this report.

Actual work consisted of read-only `rg` inventory/name searches, `cat`/`sed`
source reads, Python file/JSON display, `sha256sum`, the symbolic derivations
recorded above, and creation/readback of this report. An initial test-name
search anchored at column zero returned no matches; it was corrected to
match the existing indented unittest methods. No tests were run. The bounded
integrator assignment supplied the task scope; no new claim, canonical-state
operation, production edit, old-auditor edit, or Git operation was performed.

Next actionable step: the integrator can retain this as a source-bound domain
review alongside existing oracle tests and the separate candidate diagnostic
evidence. No source fix is requested by this assignment.

## Source identities

SHA-256 values observed during this review:

| Source | SHA-256 |
| --- | --- |
| `AGENTS.md` | `93b35392c4a38468a53b8a97844c941a7a6d92fb8c061439e75f9bac1b99339d` |
| `README.md` | `e025c5714a1fbb5c7bd6b80c495d0bdb5868cf3edef570dcf9bc9edaf349dacc` |
| `docs/PROJECT_STATE.md` | `12f0ac868d8465761fd0fb8ba2a30e8e5da9a6b813d8b366b3dcfa15515f5725` |
| `docs/ROADMAP.md` | `12c95926a49a028e03df94f44abe47ac270fa216c0cce041b70394a887f1dc9c` |
| `project/state.json` | `fd69ef9e6c59a436636f96a7a9295b1a42903ea0c853b51350b5f6ce29eba1e9` |
| `docs/specs/CONTACT_ANALYTIC_PEAK_SLICE_20261123.md` | `56e5caefaebc03596b2ed566d50578b2be2e4dc54e37bf7fdce31dab31243f03` |
| `docs/agents/transient-campaign/w3_a54.md` | `ba47ba3dc750e54c3404e48b99b282d7ffb94f05e9152fb2a62ee083ff610baa` |
| `tools/contact_peak_oracles.py` | `eb9d848a41ff3d101a5b0d86bd49e1d039000103dd5b923295bfaef332bb5c68` |
| `tools/validate_contact_reference.py` | `c910da5da6b6ad957b709a414cef39a08ed6e8b75c6e36c9d73ef535a064dcdc` |
| `tests/python/test_contact_peak_oracles.py` | `9de8d7907b7a68fccf77788d9f04c93d4059d343bb4e6b1bacdc1b26bc2be1e6` |
