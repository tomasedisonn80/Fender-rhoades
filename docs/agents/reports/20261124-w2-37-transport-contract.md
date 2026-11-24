# Wave 2 assignment 37 — Private timing transport contract review

Date: 2026-11-24. Worker:
`/root/w2_contact_implementation/w2_37_transport_contract`.
Bounded, independent static review delegated by the contact implementation owner
at the root's explicit request. Only this report was written.

## Result

**No remaining contract-relevant transport gap was found in the reviewed
post-repair source.** The all-level validation repair described by W2-32 addresses
the identified timing-status, numeric-overflow, and strict-serialization failures.
The frozen contract does not require extending the native candidate's duplicate
key and exact-field rejection rules to this private worker transport.

This conclusion is scoped to the module's own fixed Python worker and the frozen
four-fixture diagnostic. It does not claim validation of arbitrary replacement
programs or hostile worker implementations. No such broader trust model is
needed to assess the assigned contract.

## Contract boundary

The [slice specification](../../specs/CONTACT_ANALYTIC_PEAK_SLICE_20261123.md)
puts duplicate rejection, exact field sets, unknown-field rejection, bounded
integers, and native-envelope constraints under **Strict native input**.
[A53](../transient-campaign/w3_a53.md), specifically **Required native fields**
and **Zero semantics, limits and API**, binds these rules to
`parse_candidate(raw: bytes)` and supplied emitter records. They remain
requirements of the candidate parser; this review does not relax them.

The separate **Independent references and bounded numerics** section and
[A54](../transient-campaign/w3_a54.md), **Bounded damped numerics**, require the
declared quadrature settings, retained numerical evidence, finite results,
refinement agreement, actively enforced limits, and appropriate failure status.
Neither freezes an exact private-worker JSON field set or requires duplicate-key
rejection there. `_run_damped_time` selects the current module through
`sys.executable` and `Path(__file__).resolve()`; it has no candidate-executable
argument. Its worker serializes its own dictionaries with `allow_nan=False`.
Those dictionaries cannot emit duplicate JSON object keys.

Consequently, the parser's acceptance of additional JSON-safe fields, or normal
`json.loads` duplicate-key handling for hypothetical substituted stdout, does not
by itself establish a defect in this frozen private interface. A new exact
transport schema is not proposed.

## Implementation assessment

- `_validate_time_refinements` now inspects both observables and all three
  refinements. Each level requires finite, non-boolean, nonnegative timing and
  error estimates, the exact frozen tolerance pair and subinterval limit,
  integer evaluation/subinterval counts within their caps, matching observed
  and reported evaluation counts, and an empty convergence message. Later
  levels must retain the recomputed difference and budget and satisfy that
  budget. Aggregate evaluations must equal the per-level sum and stay within
  10,000. Worker elapsed time must be finite and nonnegative.
- `_time_worker` uses the declared 128-term series, three tolerance pairs, and
  `limit=128`. The aggregate integrand counter and worker elapsed-time guard
  enforce its limits. The parent's `subprocess.run(timeout=10.0)` also covers
  startup and SciPy import, with termination recorded on timeout. These are
  enforcement paths, rather than reliance on the worker's elapsed-time field.
- `_run_damped_time` rejects nonfinite JSON constants and float conversions
  throughout the decoded object. Its integer conversion rejects numbers that
  cannot be used by the binary64 interface; `OverflowError` is caught locally
  through `ArithmeticError`. Malformed replies, invalid success records,
  decoding/launch failures, and timeouts become `inconclusive_reference` inside
  the optional timing path.
- `_damped` computes scalar values and root evidence before optional timing.
  Timing failure retains those values and gives empty timing intervals. Missing
  SciPy retains `not_available`. An otherwise resolved time beyond the fixture
  duration is downgraded while retaining the numerical evidence. Actual worker
  nonfinite results are represented by null numeric fields and textual evidence;
  failed integrations retain completed refinements and a reason. Successful
  records retain settings, counts, estimates, convergence fields, and differences.

The retained records distinguish declared numerical refinement from rigorous
certification. No acceptance-budget or status change is needed for this review.

## Verification scope and identities

Read `AGENTS.md`, project context, the frozen specification and relevant A53/A54
clauses, the oracle implementation, W2-23/W2-32 reports, and their test coverage.
W2-23 covers actual optional-backend operation, missing SciPy, timeout/evaluation
limits, nonfinite/convergence results, and malformed/out-of-duration timing.
W2-32 adds launch/encoding/process failures, malformed earlier levels, and
unrepresentable JSON numbers, asserting preserved values/root evidence and
strict JSON serialization. These are existing reported checks, not new runs.

Actual work used read-only `rg`, `cat`/`sed`, JSON state inspection, Git revision
inspection, and SHA-256 calculation. **No numerical probe or test suite was run.**
The report received a local trailing-whitespace check. No source, production,
old-auditor, canonical-state, staging, commit, or push changes were made.

Starting HEAD was `015767fee5635ff85a717739188ff4af60fe84f2`; a subsequent read
observed concurrent integration at `448c863f4a5396eac0ba7f0977206b6b84e6bc8d`.
The reviewed oracle SHA-256 was unchanged across those observations.

| Reviewed artifact | SHA-256 |
| --- | --- |
| `tools/contact_peak_oracles.py` | `74030851ce9511cd50c6952a11b43bfdf5e8da5d7fd10428c009be0210060789` |
| `docs/specs/CONTACT_ANALYTIC_PEAK_SLICE_20261123.md` | `56e5caefaebc03596b2ed566d50578b2be2e4dc54e37bf7fdce31dab31243f03` |
| `docs/agents/transient-campaign/w3_a53.md` | `f708133e8b1611d15056084b542e44d57012ec81340d1e3ce8c656677ea7369a` |
| `docs/agents/transient-campaign/w3_a54.md` | `ba47ba3dc750e54c3404e48b99b282d7ffb94f05e9152fb2a62ee083ff610baa` |
| `docs/agents/reports/20261124-w2-23-oracles.md` | `adb00d2ac4074cf7106142007ffd1c7f4dc6a87a8adb85dc7833bb133bc24a65` |
| `docs/agents/reports/20261124-w2-32-backend-boundaries.md` | `efb5810f6981fe7da5dacff5e16a9ed793fcc1a15e1a73ea0eb6bddd52078e8c` |
| `tests/python/test_contact_peak_oracles.py` | `9de8d7907b7a68fccf77788d9f04c93d4059d343bb4e6b1bacdc1b26bc2be1e6` |
| `tests/python/test_contact_peak_backend_boundaries.py` | `5868ec0c2428c2105beeec80b116efb4b3b0c4f4a664801c7ee65e50039d1166` |

Next action: retain the W2-32 repair and its focused regression evidence during
integration; this review requests no further transport change. It supplies no
new physical, platform, or continuous-extrema evidence. **PHY-002 and its
continuous-extrema acceptance gate remain incomplete.**
