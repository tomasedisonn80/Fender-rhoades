# Offline hammer and damper reference

`rhodes::contact::Reference` is a synthetic two-mode experiment with a finite-mass
hammer and a prescribed compliant damper face. It is separate from the
[real-time ResearchEngine](NUMERICS.md): it allocates history, copies transactions
for rollback, and is not used in the VST3 audio path. No constants are measured
Rhodes properties. The [API](../include/rhodes/contact_reference.hpp),
[implementation](../src/contact_reference.cpp), and
[frozen acceptance contract](specs/CONTACT_ACCEPTANCE_20261123.md) define its scope.

## Coordinates, signs and energy

Modal positions $q$, hammer position $x_h$, face position $\ell(t)$ and clearance
$g$ are in metres; velocities are in metres per second. With dimensionless
hammer/damper projections $b,d$, positive compression is

$$\delta_h=x_h-b^Tq-g,\qquad \delta_d=d^Tq-\ell(t).$$

For either enabled contact, only exponent **$p=1$** is implemented:

$$F=\begin{cases}\max(0,\delta(k+c\dot\delta)),&\delta>0,\\0,&\delta\leq0.\end{cases}$$

$k$ is in N/m and $c$ in N·s/m². The modal and hammer equations use reciprocal
force signs:

$$M\ddot q+C\dot q+Kq=bF_h-dF_d,\qquad m_h\ddot x_h=-F_h,$$

where $M_{jj}=m_j$, $K_{jj}=m_j(2\pi f_j)^2$ and $C_{jj}=2m_j\beta_j$.
Unlike the audio modal API, this reference permits zero frequency and is not
restricted to underdamped motion.

Contact potential is $U=\tfrac12 k\max(0,\delta)^2$. At positive compression,
physical loss power is $c\delta\dot\delta^2$ on the active-force branch. When
$k+c\dot\delta\leq0$ clips the force to zero, loss is
$-k\delta\dot\delta$; dropping that term would lose the disappearing stored
contact energy. Separated or disabled contacts contribute zero. Background
modal loss is $\dot q^TC\dot q$, and signed damper work power is
$P_d=-F_d\dot\ell$. These quantities are evaluated directly from the constitutive
equations, not inferred from a numerical energy discrepancy.

Total energy includes modal and hammer kinetic energy, modal spring energy,
and both contact potentials. The ledger reports

$$R=E-E_0+D_{\mathrm{modal}}+D_h+D_d-W_d-W_{\mathrm{relaunch}}.$$

`relaunch_hammer(v)` preserves every position and modal velocity, changes only
hammer velocity, and records exact kinetic work
$\tfrac12m_h[(v_h^+)^2-(v_h^-)^2]$. Constant, linear and sinusoidal face trajectories
derive position and velocity from the same function. Replacing a trajectory
requires exactly continuous face position at the action time. The
`fixed_damper_equilibrium` helper assembles the constant-face preload using
$K+k_dd d^T$ in unshifted coordinates; it requires nonsingular modal stiffness
and a separated hammer.

## Integration and explicit limits

The general path uses adaptive RK4 step doubling, accepting the two half-step
trajectory. Mass-weighted mechanical errors and separately integrated loss,
work and impulse errors control steps. An additional estimated contact-gap
error limit of `0.002 * event_gap_tolerance_m` protects event resolution; it
does not widen root brackets or project positions. Interior samples, extrema
checks, replay and root refinement identify onset, separation and clamp changes.
Residuals are evaluated separately from the integrated physical quantities.

Default maximum internal step is **20 µs**, relative tolerance `1e-8`, and
absolute tolerances are `1e-12 m`, `1e-9 m/s`, `1e-13 J` and `1e-13 N s`.
Default event time and gap tolerances are **10 ns** and **5e-13 m**. These local
controls are distinct from the frozen observable acceptance budgets.

| Quantity | Accepted finite domain or cap |
| --- | --- |
| Modal and hammer mass | `1e-6..1 kg` |
| Modal frequency; decay rate | `0..20000 Hz`; `0..1e5 /s` |
| Contact stiffness; contact damping coefficient | `0..1e8 N/m`; `0..1e8 N s/m²` |
| Projection coefficients | `-100..100` |
| Stored modal/hammer positions, clearance and face position | `-1..1 m` |
| Stored modal/hammer and face velocities | `-1000..1000 m/s` |
| Stored time; each `advance(dt)` | `0..1000 s`; `1e-12..1 s` |
| Derived scalars and ledger accumulators | Finite, magnitude at most `1e24` |
| Default/max work limits | 100,000 trials; 1,200,000 RHS evaluations; 64 consecutive rejections; 64 iterations per root; subdivision depth 32 |
| Persistent history | At most 100,000 event rows and 100,000 interval rows |

The API also validates trajectory envelopes and all settings. Its minimum step
and representable-progress guard prevent endless subdivision. `Result` separates
`success`, `invalid_input`, `budget_exhausted` and `numerical_failure`, with a
specific failure reason. Initialization, advancement and actions commit only
after full success. Failure preserves prior state, configuration, ledgers and
diagnostics; attempted work/time remains available in the returned result.
Inspect failure before using a run as acceptance evidence.

### Certified force-free grazing path

A narrow analytic specialization avoids accumulated amplitude drift when all
modes are undamped, the hammer is stationary, the damper is disabled, and at
most one oscillating mode contributes to the hammer projection. Projected
zero-frequency modes must be stationary. The computed harmonic gap envelope
must be nonpositive in extended precision; positive envelopes are never rounded
down to certify no contact. Exact anchored harmonic motion then has zero contact
force, impulse and physical loss. A grazing maximum is reported only under the
additional public-precision tangency condition and a resolved derivative-root
bracket. Actions recompute or disable the certificate. Unsupported or ambiguous
tangencies can still fail explicitly; this is not a general tangency proof.

## Run and inspect fixtures

The ordinary CMake build supplies the CLI and retained tests. From the repository
root on Linux or macOS:

```sh
cmake -S . -B build/release -DCMAKE_BUILD_TYPE=Release
cmake --build build/release --config Release --parallel 2
./build/release/rhodes_contact_reference --help
./build/release/rhodes_contact_reference --fixture evolved_relaunch \
  --max-step 2.5e-6 --event-tolerance 2.5e-9
ctest --test-dir build/release -C Release -R '^(contact|grazing)\.' --output-on-failure --no-tests=error
```

With Visual Studio's multi-configuration generator, use
`build/release/Release/rhodes_contact_reference.exe`; see [DEVELOPMENT](DEVELOPMENT.md)
for compiler and generator setup.

The CLI prints JSON to stdout. `--help` lists 26 fixtures covering free/supported
impact, damping and force clipping, preload, prescribed face motion, relaunch,
event order, grazing and signed coordinate scaling. `--tolerance-scale` varies
local integration tolerances; `--output-step` controls observation spacing, not
the maximum internal step. The required grazing outer interval is handled
explicitly by the fixture scheduler.

Each run includes samples, event brackets, loss/work/impulse ledgers and
before/after relaunch snapshots, including time-zero actions. Diagnostics expose
analytic evaluations and **sampled maxima**, explicitly not continuous-time
extrema. Successful execution returns 0; a solver failure emits `success=false`
and returns 2; CLI/setup exceptions return 1 with an error on stderr.

The optional [independent auditor](../tools/validate_contact_reference.py) uses
NumPy/SciPy, analytic limits, Radau integration and matrix exponentials. Candidate
JSON is data input; the auditor does not link the candidate solver. The
[contact tests](../tests/cpp/contact_reference_test.cpp) and
[generalized grazing tests](../tests/cpp/contact_grazing_test.cpp) are retained
regressions. Their passing results do not complete PHY-002 by themselves: the
full independent step/event/local-tolerance matrix and independently located
continuous-time peaks remain acceptance gates. Consult the
[project state](PROJECT_STATE.md) and frozen acceptance for the current decision.
