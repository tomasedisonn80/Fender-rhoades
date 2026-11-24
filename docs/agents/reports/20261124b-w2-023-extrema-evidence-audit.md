# W2-023 — supported-extrema evidence audit

24 November 2026; actual worker `w2_023_extrema_evidence_audit`. Assigned scope:
read-only audit of the initial supported-impact extrema result, reference,
driver, strict schema and retained artifacts. This report is the sole authored
file; no Git, canonical-state, product, test or historical-result changes.

## Findings

The retained evidence supports **26 scoped candidate diagnostic passes and six
completed independent references**. I found no false pass in this observed
collection. This establishes neither continuous-extrema coverage certification
nor general waveform/energy acceptance, the original 0.8 decrease criterion,
PHY-002 completion, or calibrated physical fidelity. Those boundaries remain
explicit in the JSON and narrative.

All 26 retained raw candidates strictly parsed into distinct expected slots;
there were no missing/duplicate slots, failed producers or unavailable peaks.
Canonical serialization of the current declaration matches the retained plan
after excluding invocation paths. Six reference summaries match their original
JSON fields and hashes. After the integrator ended worker 21's timing window,
sequential hashing verified **all 75 manifest artifacts, 115,814,916 bytes**.
All 66 arrays in the six final references match their declared hashes, shapes
and dtypes. Final reference NPZs total **44,195,042 bytes**; including six partial
checkpoint NPZs gives the documented **86,175,359 bytes**.

Independent arithmetic reproduced the reported maxima exactly:

| Comparison | Maximum observed error |
| --- | ---: |
| Compression versus R2 | 3.137935874683584e-14 m |
| Force versus R2 | 6.275868713601085e-10 N |
| Distance to maximizing-time interval | 1.562467931079392e-9 s |

The audit also checked 104 raw-sample lower bounds, 208 retained pointwise-side
comparisons, and 52 coordinate peak pairs without arithmetic discrepancies.
Each reference spans 20 ms, retains seven alternating branch intervals and
three physical peak clusters, and remains within its declared execution caps.
Recomputing compression over every retained candidate state confirms each
reported global maximum and maximizing candidate ID. Every accepted mesh
endpoint is present in that registry; the third impact supplies the largest
peak, rather than the first recontact or an arbitrarily selected endpoint.
Disabled damper values remain exact zeros with `[0,0.020]` maximizing intervals.

## Documentation correction, not an observed numerical failure

The initial narrative says reference refinement must meet one tenth of the
“value/time budgets.” The original design and W1-001/W1-008/W1-011 reports apply
`0.1*B(M_R2)` to **both successive value differences**, while timing uses the
full supplemental scale. The implementation agrees with that distinction;
the plan's `both_reference_refinement_fractions` name is ambiguous.

Minimal in-memory reproduction: deep-copy the three retained unscaled summaries,
add `2e-7` seconds to both endpoints of R0's hammer compression and force
`maximizer_intervals_s`, then call `compare_refinement`. It reports resolved
timing for a `2.0000035328999327e-7` difference against `5.602537499899079e-7`.
That exceeds one tenth but satisfies the actual timing rule. The observed
unmodified references satisfy even the stricter interpretation. I notified the
integrator; preserve the historical result and correct its interpretation.

In-memory negative controls confirmed producer failure stays `fail`, and a
zero claimed hammer peak remains `fail` against nonzero sampled witnesses even
with missing references. Disabled timing accepts interval membership and
rejects an outside time. Physical-versus-dense derivative discrepancies are
nonmonotonic for both fixtures; the narrative explicitly discloses this and
does not claim derivative convergence.

## Identity and applicability

The preserved producer still hashes to
`4ed2552430f5de5846adcb1a7b99521b118a12b714ad61b146ede7bab24f8f2d`.
The initial run binds native source `0f9808442b5ddf6ddc6091c9b6294117b7aea85cd54707b9c3743121ac706100`;
the later grazing change is `70dde1bfab9c2e35457da2ebf66ff4638cb6f56f1f19a84dbe55a1752d5166fd`.
The historical experiment does not automatically validate that newer producer.

| Audited artifact | SHA-256 |
| --- | --- |
| Initial report | `d4111e0f1840fa64b622305fb181af56f6baa5baa3f8fd14874708e572f36646` |
| Reference summaries | `cfc2e9350af473e1a44110ee3ef87f99a1c99e6a07217f5a4dd7bc1c3701f491` |
| Manifest | `e18456d6271601dd3cc9c8c88c977ca077fb382ab2eaa876d025d0b47b0af125` |
| Plan | `6692f89fe6e3e33728dce2f238306fbd7fd0ea2604ed6d38e9566c08cd827bc3` |
| Extrema reference source | `35b1f822cef0da3036faaf8dffcfd37e9091ca3df5098650d3d306fac1e7908b` |
| Driver source | `466d6298eb966bd7b35ba778db551b6a260eef8a27f0ee408a1b173ec8a864cb` |
| Supported schema source | `42d821bf535d77e7fad29da48c25dc9ac1bf2cba2fb68e85efe92afa4a82c7c7` |

Required context was read. `project.py validate`, `sync --check`, and
`next --json` passed. Verification used read-only Python JSON/hash/arithmetic
scripts and retained-array inspection; no SciPy integration, build, trajectory
rerun or benchmark was performed. Next action: add the interpretation correction
and independently bind any native compatibility replay to the later source.
