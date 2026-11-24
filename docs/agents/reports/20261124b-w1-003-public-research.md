# W1-003 — Public evidence for extrema and continuous audio references

Date: 2026-11-24. Scope: documentary review supporting PHY-002 and RT-001; no
implementation, parameter, registry, canonical-state, or fidelity changes.

Read `AGENTS.md`, `README.md`, `docs/PROJECT_STATE.md`, `docs/ROADMAP.md`,
`project/state.json`, `docs/research/{README.md,sources.json}`, contact-reference
and peak-diagnostic documentation, and relevant Python oracle/audio code.
Public sources below were opened directly. SciPy's live documentation identified
itself as v1.18.0; that is **not** a claim about the installed audit backend.

## Evidence-to-implementation map

| Primary source and exact location | Applicable result | Implementation consequence and boundary |
| --- | --- | --- |
| [Pfeifle, DAFx-17, §5.2, Eq. (1), §5.3](https://www.dafx.de/paper-archive/2017/papers/DAFx17_paper_79.pdf), SRC-004 | On contact, the local viscoelastic force has the form `F = k*x^alpha + lambda*x^alpha*xdot`; the distributed contact criterion uses a weighted contact-area sum. §5.3 models the tine as a cantilever with two polarizations, large deflection, and added tuning-spring mass. | Supports testing velocity-dependent contact and retained resonator motion. The paper describes moderate impact velocities and simple contact geometries as a validity context. It does **not** establish this repository's outer force clamp, two-mode reduction, p=1 parameters, or specimen accuracy. |
| [Bilbao, Torin, Chatziioannou, arXiv:1405.2589v1, Eqs. (1)–(6), (20)–(24)](https://arxiv.org/pdf/1405.2589v1), SRC-027 | `Phi = K*[u]_+^(alpha+1)/(alpha+1)`; `F = Phi'(u) + udot*Xi(u)`, `Xi >= 0`; `dH/dt = -udot^2*Xi(u)`. The conservative limit obeys `H = M*udot^2/2 + Phi` and `u <= ((alpha+1)*H/K)^(1/(alpha+1))`. | Grounds storage/loss and conservative upper-bound checks. The general presentation uses `alpha > 1` and separately notes p=1 as a penalty choice. Its finite-difference stability result belongs to its discrete energy scheme; adaptive RK4 with force clipping does not inherit it. |
| [1979 Rhodes manual, chapter 2, damper module/release bar](https://www.fenderrhodes.com/service-manuals/1979/ch2.html), SRC-026; [chapter 4, escapement/hammer tips](https://www.fenderrhodes.com/service-manuals/1979/ch4.html), SRC-025 | Pedal linkage releases all dampers; damper arms/felts differ by register. Escapement and hammer-tip height/hardness vary, with adjustable/customizable transition points. | Supports distinct hammer, tine, damper, and prescribed pedal/action coordinates. These revision-specific service descriptions supply neither calibrated contact stiffness/damping nor finite hammer mass. Durometer readings alone do not identify the force law. |
| [SciPy `solve_ivp`, methods/events](https://docs.scipy.org/doc/scipy/reference/generated/scipy.integrate.solve_ivp.html); [SciPy `brentq`, parameters](https://docs.scipy.org/doc/scipy/reference/generated/scipy.optimize.brentq.html) | Radau has fifth-order integration and cubic dense output. Event detection uses stepwise sign changes and can miss multiple crossings. `brentq` requires a continuous function and opposite endpoint signs. | An independent Radau trajectory is useful, but dense sampling plus `brentq` cannot certify completeness of extrema. Analytic enumeration, valid interval bounds, or an explicitly limited/refined search contract is needed; report unresolved coverage honestly. |
| [SciPy `roots_legendre`, definition and interval mapping](https://docs.scipy.org/doc/scipy/reference/generated/scipy.special.roots_legendre.html) | For `x_j,w_j` on `[-1,1]`, `integral_a^b f(t) dt ~= (b-a)/2 * sum_j w_j*f((a+b)/2+(b-a)*x_j/2)`. An n-node rule is exact for polynomials through degree `2n-1`. | Independently generated Q6/Q16/Q32/Q64 nodes support the current convolution study. Q6's degree-11 exactness does not make the sinusoidal-source × Kaiser/sinc integrand exact. Preserve refinement and independent normalization checks. |
| [SciPy `quad`, tolerances/outputs/points](https://docs.scipy.org/doc/scipy/reference/generated/scipy.integrate.quad.html) | Requested accuracy is `abs(I-result) <= max(epsabs, epsrel*abs(I))`; `abserr` is an **estimate**. `points` identifies integration breakpoints; `limit` bounds subdivisions. | Use adaptive integration as a separate normalizer/timing cross-check. Inspect convergence messages and budgets; do not relabel an error estimate as a certified enclosure. |
| [Bilbao et al., 2017 author publication record, abstract](https://www.research.ed.ac.uk/en/publications/antiderivative-antialiasing-for-memoryless-nonlinearities/), SRC-015 | The method applies antiderivatives to memoryless nonlinear functions to reduce aliasing. | This is an alternative method-class lead, not evidence that the current coupled contact ODE or complete pickup trajectory is antialiased. No ADAA implementation is proposed here. |

## Exact next-work equations derived from the repository contract

For its active p=1 branch, let `v = delta_dot` and `a = delta_ddot`:

`F = delta*(k+c*v)` and `F_dot = k*v + c*v*v + c*delta*a`.

Compression candidates satisfy `v=0`; force candidates satisfy `F_dot=0` while
`delta>0` and `k+c*v>0`. Evaluate interval endpoints and both sides of contact,
clamp, relaunch, and prescribed-velocity breakpoints. Retain disabled/separated
zero plateaus separately. These derivative equations are direct differentiation
of **the repository's** clipped law, not equations claimed from Pfeifle.
Clipped-branch loss is `-k*delta*v`, distinct from active-branch `c*delta*v*v`.

For continuous audio, define `v_f(r)=v(r/fs)` and the selected finite kernel
`kappa(s)`. The target is

`y[n] = integral_0^96 kappa(s)*v_f(n-s) ds / integral_0^96 kappa(s) ds`.

Split at known source/action discontinuities; a hard reset also resets convolution
history. Compare the independently assembled same-Q6 implementation separately
from the refined continuous integral. Their difference includes quadrature and
kernel realization effects and is not an alias-only bound or measured fidelity.

## Checks and handoff

`python3 tools/project.py validate`, `sync --check`, and `next --json` all exited
0 at revision 32; no available unclaimed task was reported because the integrator
owns current work. This assignment is a bounded review, not a competing claim.
No numerical experiment or new physical measurement was performed. Source URLs
were opened successfully. Next: implement independent stationary-point coverage
with stated unresolved cases, preserving the frozen budgets and existing
continuous-audio refinement protocol. Only this report was written.
