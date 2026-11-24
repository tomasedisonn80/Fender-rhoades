# W2-36 — retained contact-peak evidence integrity audit

Date: 24 November 2026. Assignment: 36/100, bounded independent evidence review.
Owned path: this report only.

## Finding and boundary

**No material provenance or result mismatch was found** in the retained native
archive, its summary, or the independent damped cross-check at the identities
below. Archive membership, hash links, raw-byte retention, reported case outcomes,
and the checked numerical arithmetic agree. This is a read-only audit of recorded
evidence; no native producer, diagnostic CLI, quadrature, or DOP853 experiment was
rerun. PHY-002 remains **incomplete**.

Required context was read boundedly: `AGENTS.md`, `README.md`,
`docs/PROJECT_STATE.md`, `docs/ROADMAP.md`, the PHY-002 entry in
`project/state.json`, and the frozen
[four-fixture contract](../../specs/CONTACT_ANALYTIC_PEAK_SLICE_20261123.md).
This was an integrator-assigned review within existing work, with no independent
task selection, claim, status change, Git operation, or remote CI work.

## Audited artifact identities

| Artifact | Bytes | Independently computed SHA-256 |
|---|---:|---|
| [Native summary](../../validation/contact-peaks-native-20261124.json) | 55,223 | `649e7c8ad6ad2e0d9e4d0153938bf938ac42a6673486502763d97d78a8533a6e` |
| [Native archive](../../validation/contact-peaks-native-20261124.tar.gz) | 277,064 | `b3ae5198fd610820cb516426e7f84dcc921035dc89ce42282fde530cd7175cec` |
| [Damped cross-check](../../validation/contact-peaks-damped-crosscheck-20261124.json) | 41,472 | `0bc2b5fbd5da891989de8afd595a7055224a80fd271a7aa291d50d487dc841a0` |

The archive contains **104 unique regular-file members**, totaling **1,618,006
uncompressed bytes**. Every name is a normalized relative POSIX path under
`contact-peaks-native-20261124/`, without a parent traversal or backslash. There
are no directory, symlink, hard-link, device, or other nonregular members.
All **103 payload entries** in `artifact-manifest.json` match their exact byte
counts and SHA-256 values, and its entry set equals the archive member set except
for the manifest itself. The enclosing archive hash protects that manifest;
there is no claim of a self-hash or external signature. `summary-core.json`
matches the external native summary after removing its `archive` field.

## Native retention and exact identities

All **7 native invocation records** agree with their retained stdout and stderr
hashes, byte counts, and summarized native results. All **8 CLI raw artifacts**
equal their corresponding native stdout **byte for byte**; the damped input is
used twice. Each raw artifact also matches its case record, comparison, and
invocation-summary hash. All eight case-record files equal their entries in the
respective CLI manifests. Their eight run settings agree with the native data.

All **3 CLI manifest hashes**, **3 plan hashes**, **5 reference-definition
links**, and **20 summary peak records** were checked against the actual retained
members. The plan hashes are:

| Invocation | SHA-256 of retained `evidence/plan.json` |
|---|---|
| `all_four_default` | `9a3313438b9a0074d1f99b78475b262ed4525db2bed2021c9937979c27005192` |
| `damped_with_time` | `a120185f151371e82b6eceed2d8bc45e4d291288e56b39b5264b84bd6e7da191` |
| `partial_missing_unsupported` | `2ac4f058a755876aefb3886c1a7c0281459d7ec9109802cc68a459a06d0af9ba` |

The canonical definition hashes use sorted keys, compact separators, and
`allow_nan=False`. Each retained definition also agrees with an audit-side
reconstruction from literal defaults in the archived `Fixture` class and the
inspected named-fixture overrides, including evaluated stiffness/damping and the
rounded preload calculation. No archived module was executed for this check.

| Fixture | Canonical oracle-definition SHA-256 |
|---|---|
| `conservative_impact` | `d5f5012ad79dbfe3f2209dbadf404d2cf379c93831ffa519109c37eae82c3222` |
| `damped_impact` | `68a1ca404117f2df3e49372c66e237d9f6f1fba0d151937601e0cdc84f0ba0ee` |
| `forced_clamp` | `5f842bd7fa07b89109b90f076330c0c154ed2ebe1ce2413367364438eff32544` |
| `preload_equilibrium` | `c777a0f91e117dc0e6d46704f51b91a1e15ecbe46949615ba25dce2c68e510b1` |

All **10 archived source snapshots** match the native summary's before/after
identities. An additional **48 source-hash links** across the three audited
invocations and three CLI manifests resolve to those same snapshot bytes.
Recorded before/after maps are equal. Significant recorded identities include:

| Recorded source | SHA-256 |
|---|---|
| Frozen analytic-peak contract | `56e5caefaebc03596b2ed566d50578b2be2e4dc54e37bf7fdce31dab31243f03` |
| `tools/contact_peak_oracles.py` | `eb9d848a41ff3d101a5b0d86bd49e1d039000103dd5b923295bfaef332bb5c68` |
| `tools/contact_peak_schema.py` | `af5cbf87d155ecb40c176b78a7527b2a7bd686c7fd515be621af93d910db86c4` |
| `tools/validate_contact_peaks.py` | `ba25e476be93aecc1ccdbedfa020f457369bdad62c0ce16d5f749f79fc42e3ef` |
| `tools/validate_contact_reference.py` | `c910da5da6b6ad957b709a414cef39a08ed6e8b75c6e36c9d73ef535a064dcdc` |

The current oracle and CLI differ from these archived snapshots. Bounded AST/diff
inspection identifies later worker-response validation in the oracle and partial
raw-retention/accounting/error handling in the CLI. This is legitimate source
evolution, **not evidence corruption**. The recorded experiments support the
recorded source, and do not by themselves validate the later hardening.

## Case exits and timing interpretation

| Retained invocation | Per-case exits | Process/manifest exit | Meaning |
|---|---|---:|---|
| `all_four_default` | `0, 0, 0, 0` | 0 | Four fixtures pass values, validity, and required available timing. |
| `damped_with_time` | `0` | 0 | Explicitly requested damped timing passes the supplemental comparison. |
| `partial_missing_unsupported` | `1, 2, 2` | 1 | Producer failure takes precedence over absent diagnostics and an unsupported fixture. |

The retained plans consistently specify precedence `[3, 1, 2, 0]`. The mixed
invocation preserves native `conservative_partial_failure` with native exit 2,
`success=false`, failure code 9, and one sample at time zero; the comparator
correctly reports producer failure/exit 1. The other two cases report
`not_available` and `unsupported_fixture`, each exit 2. No partial trajectory is
promoted to a successful comparison, and no case is dropped from aggregation.
Operational-failure precedence is declared, but no operational-failure case was
exercised by these three successful diagnostic launches.

The **20 populated peak comparisons** have consistent independent-reference
records and value-budget arithmetic; their required timing passes also agree
with the retained candidate times and reference intervals. The default damped
hammer times remain `not_available` and unrequested. Preload's two positive
damper peaks retain `kind=enclosure`, `time_status=unresolved_near_equilibrium`,
empty maximizing-time intervals, and `time_comparison_required=false`; they are
not positive plateau claims. Enabled zero plateaus and disabled dampers remain
distinct. Timing uses the supplemental budget and does not require the reference
time to lie inside a native replay bracket. None of these outcomes amends frozen
acceptance or certifies the native sign-change search's continuous coverage.

## Process observation and preserved failures

All **3 failed `strace` attempts** remain in the archive, with invocation records,
exit 1, empty trace/stdout files, and 283-byte stderr records reporting
`PTRACE_TRACEME: Operation not permitted`. No diagnostic evidence directory is
present for those attempts. They do not establish an observed OS process tree.

The later, separately retained Python audit logs contain **9 events** and match
their declared hashes and invocation summaries. Their guard-start counts are
**1, 2, 1**. Only the timed invocation records a child launch, using the same
Python interpreter for `contact_peak_oracles.py --_damped-time-worker`; that
worker also records its inherited guard. No rejected process request or declared
native-binary execution is recorded in those logs. Inspection of the retained
`sitecustomize.py` confirms its stated scope: Python audit events and a guard
against other child executables/shell launch. This is explicitly narrower than
OS tracing and cannot exclude execution mechanisms outside those audit events.

The producer's retained stable identity is 127,088 bytes,
`4ed2552430f5de5846adcb1a7b99521b118a12b714ad61b146ede7bab24f8f2d`.
The archive does not bundle the executable itself. Its hash, source snapshots,
and recorded native invocations are useful provenance but are not cryptographic
execution or build attestation. The evidence correctly keeps
`candidate_production_attested=false`; the native envelope does not independently
attest arbitrary input bytes' physical parameters.

## Independent damped DOP853 cross-check

The embedded **247-line experiment source** parses and hashes to
`0b32a8c74c5fa6eaa3ae7b261abcea8662448fd06ecf115a2241cd174c918efd`, exactly
its declared script hash. Static inspection confirms that the two relative-motion
ODE solves derive their parameters from `fixtures()` and precede the public
`reference_peaks` call; they do not consume native samples/candidates or oracle
private root/quadrature routines. The source declares the retained DOP853 levels,
20,000-RHS evaluation cap, and active 10-second per-level alarm/callback guards.

All **4 source identities** resolve: three to the native archive's snapshots and
A54 to its unchanged repository file. All **8 recorded backend module hashes**
match the files currently present in the recorded runtime. The cross-check's
plain dataclass definition hash is
`5cdf05b26238c3344603a04247e9b68b3f57adb7034612551a1b4ae9d4133839`.
It differs legitimately from the oracle-definition hash because the latter adds
evaluated stiffness and background-damping arrays. Removing exactly those two
fields yields the same definition; the derived reduced mass, stiffness, damping,
and initial state also recompute exactly. The public reference's definition,
settings, peak values, and peak times agree with the native archive's timed
damped reference.

| Retained result | Coarse | Fine |
|---|---:|---:|
| RHS evaluations / accepted steps | 349 / 16 | 673 / 36 |
| Recorded elapsed time, s | 0.01790224800015494 | 0.019643739000002824 |
| Maximum compression, m | 0.00018988288079196648 | 0.0001898828808852406 |
| Compression time, s | 0.0003450597378882422 | 0.0003450597379600364 |
| Maximum force, N | 6.1918072652091025 | 6.191807278632382 |
| Force time, s | 0.0001776905622746325 | 0.00017769056225740937 |
| Separation time, s | 0.0009562830255752656 | 0.0009562830255102769 |

All **12 value/time comparison and refinement records** recompute exactly from
the retained peaks and references, including signed/absolute differences,
budgets, budget fractions, and the one-tenth-budget booleans. The largest
fraction of a full budget is **1.0029636851456243e-05**, for coarse-to-fine force
value refinement, below the declared 0.1 threshold.

All **6 event records** agree with the retained force law, branch factor,
stationarity residual, filter, step bracket, and probe-time arithmetic. Each run
has one accepted compression, force, and separation event, successful terminal
status 1, and no recorded warnings. Force precedes compression, which precedes
separation and the fixture end. The reported force-to-compression delays recompute
to `0.0001673691756136097 s` and `0.000167369175702627 s`. Compression/force
probe signs straddle their roots as claimed. Separation's right probe is clamped
to the terminal time; the post-separation negative-compression/zero-force result
is the stated analytic continuation, not a retained numerical trajectory beyond
termination. Dense interpolants are not retained, so this audit checks recorded
probe values/signs and event consistency rather than independently resampling
the solved trajectories.

## Actual commands, counts, and remaining limits

Commands were run from the repository root. `tar -tzf
docs/validation/contact-peaks-native-20261124.tar.gz` listed the complete retained
membership. Read-only `python - <<'PY'` invocations used `tarfile`, `hashlib`,
`json`, `pathlib`, `ast`, `math`, and `difflib`; none imported the diagnostic,
old auditor, or SciPy. The main native assertion invocation returned exit 0 with
counts **104/103 members, 10 snapshots, 48 source links, 7 native invocations,
8 byte matches, 5 definition links, 20 peak comparisons, 9 audit events, and
3 preserved tracing failures**. The cross-check assertion invocation returned
exit 0 with **4 reconstructed fixture definitions, 4 source links, 8 backend
hashes, 12 comparison calculations, and 6 event records**.

Exact core assertion excerpts from those executed inline commands:

```python
assert len(a.read_bytes()) == s['archive']['bytes'] and H(a.read_bytes()) == s['archive']['sha256']
assert {r['path'] for r in am['files']} == set(b) - {'artifact-manifest.json'} and len(am['files']) == 103
for r in am['files']:
    assert len(b[r['path']]) == r['bytes'] and H(b[r['path']]) == r['sha256'], r['path']
assert J('summary-core.json') == {k: v for k, v in s.items() if k != 'archive'}
assert H(script.encode()) == x['reproduction']['script_sha256']
assert item == expected, (item, expected)
assert checks == 12 and x['all_value_time_and_refinement_checks_within_one_tenth_budget']
```

These are excerpts, not a standalone replay script; `H` denotes SHA-256 and the
other objects are the loaded retained artifacts. The complete executed inline
checks are recorded in this assignment's tool transcript. No experiment,
whole-project test suite, or remote CI was run. No evidence/source/production/
old-auditor/canonical-state file was edited.

The optional-backend-absence branch is not covered by this retained run, which
used SciPy 1.17.0. Numerical refinement and quadrature error estimates are not
verified continuous-extrema certificates. Source/backend identity checks also
do not establish that recorded elapsed times or executions were independently
observed. No measured instrument or fidelity evidence is supplied. The next
integration action is to retain these experiments under their recorded source
identities and assess later transport/retention hardening through its own focused
verification, without promoting PHY-002 or rewriting these artifacts.
