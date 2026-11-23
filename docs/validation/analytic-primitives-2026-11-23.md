# Analytic primitive acceptance — 2026-11-23

Evaluator: `integration-root`; independent numerical regressions by A006/A081 and
clean-checkout execution by A099. This numerical report supports **PHY-001** and
the build/test portions of FND-001. It evaluates synthetic mathematical behavior,
not similarity to a physical Rhodes or a listening-quality claim.

## Frozen definition and source

Target: **One documented 73-key Mark I Stage specimen at its specified loaded
electrical output; identity and measurements pending.**

PHY-001 depends on `FND-001` and requires `numerical` evidence. Definition SHA-256:
`e1f8e12fe8dd221770dcd91afd64d2c58b57dcccdc450eb7c5bb81e77cf53f5e`.
Exact acceptance, dependency, kind and target projections are included in the
[source manifest](foundation-source-manifest-2026-11-23.json).

Source commit: `6cf5f3af25a6c989eb244db070681be069e7fd78`.
Tree: `5950625afe680dcb9b107e5be793a249a681c51f`.
The manifest hashes implementation, test, tool, schema, synthetic input and
maintained contract files from that actual Git object. Relevant evaluator sources
are `tests/cpp/research_engine_test.cpp` and `tests/cpp/review_regression.cpp` in
that commit; no physical input recording or unidentified fitted model is used.

## Criterion-by-criterion decision

| Criterion and exact acceptance text | Supporting evidence | Observed coverage, tolerance and source | Remaining limitation | Decision |
|---|---|---|---|---|
| `PHY-001/C01`: Analytic oscillator propagation agrees with an independent closed-form reference. | `core.exact_solution`, `review.modal_boundaries`; [A006](../agents/reviews/A006.md) | A separately evaluated scalar damped solution agrees after 1739 steps at 48 kHz within `1e-16 m` and `5e-13 m/s`. Boundary tests cover 36 rate/frequency/damping combinations at frames 1, 17 and 257, with `2e-14 + 3e-12*abs(q)` m and `2e-11 + 3e-12*abs(v)` m/s limits. | Linear underdamped modes only; shared mathematical assumptions do not validate a specimen or finite contact. | Pass |
| `PHY-001/C02`: Retrigger retains displacement and velocity; pickup differentiates combined modal flux linkage. | `core.retrigger`, `core.pickup_derivative`, `core.pickup_intermodulation`, `review.engine_combined_pickup`; [A081](../agents/reviews/A081.md) | Retrigger preserves displacement and adds impulse/mass to existing velocity; release preserves both. The flux derivative matches a central-difference reference within `1e-11 V`. An independent integrated oracle snapshots all 219 states, combines weighted motion before evaluating the expanded polynomial derivative and solves the loaded network separately at six pre/post-retrigger observations; tolerance `2e-14 + 2e-12*abs(expected)` V. | The impulse surrogate adds momentum; “retains velocity” means preserving its existing contribution, not forbidding the strike's physical increment. No finite hammer, measured pickup map or reciprocal magnetic force is implemented. | Pass |
| `PHY-001/C03`: Passive network tests include inactive pickup loading; invalid inputs are rejected. | `core.network_loading`, `review.network_extreme_divider`, `core.invalid_input`, `review.rejected_updates`, `review.engine_controls` | All 73 positive-resistance branches remain connected; silent parallel sources and the first four-pickup group have independent divider expectations. Extreme-ratio tests cover representable nonzero outputs for both polarities. Invalid rates, indices, nonfinite values, parameters and overflowing updates reject while preserving valid subsequent trajectories. | Synthetic resistive topology only; no verified coil impedance/parasitics, Stage controls or hostile-input sandbox. Deterministic induced arithmetic-fault recovery is still untested. | Pass |
| `PHY-001/C04`: Synthetic parameters and omitted interactions are explicitly labeled. | README, NUMERICS, ARCHITECTURE, profile/example manifests and detailed specifications; [A097](../agents/reviews/A097.md) | Three modes per physical key, arbitrary observation weights, impulse/switched-loss controls, polynomial linkage and resistive network constants are development assumptions. Finite contact, coupling, magnetic reaction, reactive circuits, calibration, aliasing control and host integration remain explicit future work. | No physical parameter identification or fidelity promotion. | Pass |

The integrated pickup oracle's A081 diagnostic reported maximum absolute error
`1.00323e-17 V` and at least `0.000258287 V` separation from the deliberately wrong
per-mode nonlinear conversion. Those diagnostic values belong to A081's recorded
run; final suite success repeats the assertions without claiming a newly printed
maximum. The fixture demands separation above both one microvolt and one million
times its comparison tolerance.

## Other observed numerical and integration coverage

Unforced damping produces nonincreasing mechanical energy; the lossless two-second
fixture permits relative energy error `2e-11`. Block partitioning is sample-exact
within the tested build, including releases. Startup/reset silence, binary sustain,
all-notes-off, zero-intensity release, empty spans and supported-rate endpoints are
covered. These properties remain properties of the synthetic core.

CLI tests cover float-WAV format and frame counts, finite output, staged publication,
existing-file preservation, forced regular-file replacement and final-symlink
rejection. Benchmark tests parse JSON metadata, escaping, counts, silence,
one-observation statistics and deadline arithmetic. These are correctness fixtures;
they do not measure representative performance or complete RT-001.

The final integrated commands and environment are recorded in the
[foundation report](foundation-acceptance-2026-11-23.md). The Release and local
ASan/UBSan builds each passed **25/25 CTest cases**. Local LeakSanitizer was disabled;
no leak guarantee follows. The complete metadata/tooling suite passed **171 Python
tests**, supporting FND-001 rather than substituting for PHY-001's C++ checks.

[A099](../agents/reviews/A099.md) independently cloned the frozen remote commit
and passed the generic Release configure/build, **25/25 CTest cases**, **171/171
Python tests with zero skips**, and all six repository integrity commands. Its
tracked checkout remained clean. This repeats the assertions on committed source;
it supplies no additional physical or representative performance measurement.

## Decision and remaining work

All four analytic-primitive criteria pass at their stated scope. First complete
FND-001 with its own reviewed documentary and numerical evidence, then claim and
complete PHY-001 using this numerical record. Keep `fidelity_status=unvalidated`.
PHY-002 and RT-001 then become available independently; their higher-scope gates
remain required. Passing these primitives does not justify calibrated note,
73-key accuracy, passive Stage-jack, circuit-family, acoustic or real-time claims.

No listening study, physical measurement, plugin execution or sample-rate-wide
antialiasing/convergence experiment was performed. Source checks find fixed
storage and no explicit core allocation/locking/I/O; runtime interception and
platform-library behavior have not been established. Arithmetic processing
failure may partially advance state and requires reset/reprepare as documented.
Preserve these limits and all earlier reports when subsequent work adds evidence.
