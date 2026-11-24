# Wave 2 assignment 26 — bounded adversarial peak-schema review

Date: 2026-11-24. Worker: `/root/w2_contact_implementation/w2_26_adversarial`.
Assignment: independent review of `tools/contact_peak_schema.py`; no source,
solver, old-auditor, Git, or canonical-state changes. **PHY-002 remains incomplete.**

## Result and scope

One deterministic, stdlib-only probe executed **85 synthetic payload variants**
through `parse_candidate`: **69 bounded format rejections, 16 accepted records,
zero unexpected outcomes, and zero uncaught exceptions**. Accepted records had
the expected availability and producer status. No concrete parser defect was
established in this bounded review, so no regression test file or source change
was added. This is input-validation evidence, not physical or numerical peak
acceptance evidence and not an exhaustive parser certification.

Read the repository operating contract, README, project-state document, roadmap,
canonical JSON, analytic peak slice, A53 input contract, schema source, and the
existing author-test names and relevant implementations. Existing ordinary
duplicate-key, malformed scalar, basic depth, and partial-failure tests informed
the case selection; this probe concentrated on additional representations and
nested action/event types. The assigned bounded review did not select another
task or change project state.

## Source and input identities

The following SHA-256 identities were computed before and after the probe and
were unchanged throughout its execution:

| Input | SHA-256 |
| --- | --- |
| `tools/contact_peak_schema.py` | `af5cbf87d155ecb40c176b78a7527b2a7bd686c7fd515be621af93d910db86c4` |
| `tools/validate_contact_reference.py` | `c910da5da6b6ad957b709a414cef39a08ed6e8b75c6e36c9d73ef535a064dcdc` |
| `tests/python/test_contact_peak_schema.py` | `8556d104c5492257c2be33e022040c66429dd447b832ea116d51dd571ac2edec` |
| `docs/specs/CONTACT_ANALYTIC_PEAK_SLICE_20261123.md` | `56e5caefaebc03596b2ed566d50578b2be2e4dc54e37bf7fdce31dab31243f03` |

The in-memory ordered case manifest hash was
`c856efef3467e974908348a12c1200a5cc14e60660ddb2bb2cb242040de64c92`.
For each case, the manifest hashed `label UTF-8`, NUL, raw payload, NUL,
expected-result UTF-8, and NUL, in execution order. This is an identity of the
executed in-memory cases, not a claim that a separate corpus artifact was saved.
The largest payload was **6,545 bytes**.

## Actual execution

Working directory: repository root. Command: `python3 - <<'PY'` with the bounded
inline harness described below. Python reported **3.12.14**. The command exited
**0**, and the execution tool reported **0.075013866 seconds** of wall time
(approximately **75 ms**; orchestration call: 0.4 seconds). UTC start/end
timestamps were not captured. Possible overlap between this 75 ms probe and
the root agent's final benchmark startup is uncertain. No new probes ran after
the pause request.
No whole test suite, compiler build, native producer, oracle calculation, CLI
comparison, backend import test, or unbounded fuzzing was executed.

The harness imported only `contact_peak_schema` and the existing test helpers
`payload`, `encode`, and `event`, plus standard-library modules. It constructed
all cases deterministically, asserted a maximum of 300 cases, checked each
expected outcome, asserted every format-error message length was at most 384,
and verified the four source hashes before and after execution. The helper's
synthetic zero observations are not mechanically valid trajectories and were
used solely to exercise the input schema.

| Case family | Variants | Actual outcome |
| --- | ---: | --- |
| Valid unsupported fixture with one relaunch snapshot, coincident relaunch and damper-motion events, and an action-origin peak | 1 | Accepted as `unsupported_fixture`; successful producer status retained |
| Before/after snapshot nested modal fields and whole sample-container substitutions | 16 | Rejected |
| Snapshot status, failure-code, requested-velocity, and work substitutions | 12 | Rejected |
| Event contact/kind replaced with null, boolean, array, object, or array containing `hammer` | 10 | Rejected |
| Each of five event numeric fields replaced with a nested object | 5 | Rejected |
| Unsupported partial failed run containing a failed action snapshot | 1 | Accepted as `unsupported_fixture`; run failure code 9 and action failure code 8 retained |
| Supported partial run crossed with success/failure and located peaks present/absent | 4 | Both successes rejected; failures retained as `available` and `not_available`, with failure code 9 |
| Decoded duplicate keys: escaped root key, escaped sample modal key, escaped event contact key, and raw astral character versus escaped surrogate pair | 4 | Rejected specifically as duplicate JSON keys |
| A single escaped spelling of the required root `schema_version` key | 1 | Accepted as `available` |
| Overlong UTF-8, encoded surrogate, lone continuation, truncated multibyte sequence, and non-JSON Unicode whitespace | 5 | Rejected |
| Brackets, escaped quotes/backslashes, and a supplementary Unicode character inside an unsupported fixture name | 1 | Accepted as `unsupported_fixture`; quoted brackets did not count as nesting |
| Raw array nesting at depths 63, 64, and 65 | 3 | Depths 63/64 reached ordinary envelope rejection; depth 65 rejected specifically by the nesting guard |
| Ordinary scalar numeric lexical boundaries, detailed below | 18 | 9 accepted, 9 rejected as expected |
| Counter spellings `1e0`, `1.0`, `1.00000000000000001`, and `-0` | 4 | Three floating spellings rejected; integer `-0` accepted as zero |
| **Total** | **85** | **16 accepted; 69 rejected; no mismatches** |

The escaped sample-key case was emitted under the harness label
`duplicate_escaped_nested_snapshot`, but its first-occurrence byte replacement
actually targeted `samples[0].q_m`. The table records the actual target. This
probe therefore does **not** establish escaped duplicate detection specifically
inside a snapshot. The 28 snapshot structural mutations independently exercised
the nested before/after samples and action metadata.

For the unsupported-action cases, the fixture name was
`adversarial_future_fixture`; the snapshot and both events were at `0.0005 s`.
Snapshot before/after times matched the action time. The initial successful
sample envelope ended at `0.002 s`; the failed envelope ended at `0.001 s`.
Both `success` and failure codes were explicitly checked after parsing, including
the nested failed action. Availability indicates evidence presence/support;
`available` on a failed run did not change its literal false producer status.
Downstream comparison behavior was outside this probe.

## Numeric boundaries actually observed

The 18 ordinary-scalar cases replaced the first `pickup_voltage_v` token, leaving
the mechanical identity fields unchanged.

| Token(s) | Expected and observed schema result |
| --- | --- |
| `18446744073709551615`, `-18446744073709551615` | Accepted integer-token magnitude boundary |
| `18446744073709551616`, `-18446744073709551616` | Rejected by integer magnitude limit |
| `18446744073709551616.0`, `1.8446744073709551616e19` | Accepted finite floating tokens |
| `1.7976931348623157e308` | Accepted finite binary64 boundary |
| `1.7976931348623159e308` | Rejected as nonfinite/overflowed |
| `4.9406564584124654e-324` | Accepted subnormal value |
| `1e-9999` | Accepted after floating underflow to zero |
| `-0`, `-0.0` | Accepted zero spellings |
| `+1`, `01`, `1.`, `.1`, `0x1`, `1e` | Rejected malformed JSON numeric spellings |

The integer cap is applied to JSON integer tokens; finite decimal/exponent
tokens in ordinary numeric fields use the floating-number domain. Underflow to
zero is accepted in an ordinary scalar. The reviewed slice requires rejection
of nonfinite conversions, and does not specify rejection of floating underflow
or a universal magnitude cap on finite floating scalars. These observations are
recorded explicitly and are not classified here as defects. Counter fields
retain stricter integer typing even when a floating token rounds to an integral
value.

## Limits and handoff

This exercise did not exhaust all nesting placements, Unicode strings, numeric
lengths, native emitter failures, output-size limits, collection limits, or
unsupported fixture action semantics. It did not test the maximum 32 MiB input,
30,000-sample, or 100,000-event allocations. Depth 64 was tested as a lexical
boundary using an invalid envelope; no valid schema document needs that depth.

Only this report was created. No source edits are requested from the integrator
on the evidence above. Preserve the numeric lexical distinctions and the
separate producer-success/availability checks when integrating downstream
comparison logic. Completion or physical-fidelity promotion requires the
remaining independent acceptance work; **PHY-002 remains incomplete**.
