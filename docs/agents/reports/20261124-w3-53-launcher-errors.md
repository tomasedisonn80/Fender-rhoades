# W3-53 — Benchmark launcher failure retention

**Agent:** `/root/w3_53_launcher_errors`  
**Assignment:** 53, wave 3  
**Result:** three reproduced failure-path defects repaired with root authorization.

## Scope and findings

Reviewed `tools/run_vst3_matrix.py` and the seven existing verifier tests under
RT-001. Read the operating contract and required project documents. Project
validation, generated-state verification and work selection passed at revision
27 with 21 registered evidence records; the integrator already owned the task.

Added a bounded two-run fixture using one archived raw benchmark output as data.
The fixture substitutes process/affinity/build APIs and writes real temporary
streams and manifests. It does not launch a native benchmark, invoke Ninja,
set affinity, or claim an observed operating-system race.

The initial eight-test run reproduced these defects:

| Failure condition | Observed red result | Repair |
| --- | --- | --- |
| Timeout followed by a zero post-kill return code | Launcher returned 0 and marked the run complete | `timed_out` now prevents completion independently of exit code |
| Truncated affinity JSON | An executed run was counted as unattempted | Retain `affinity_error`, stream hashes and the attempted record before stopping later runs |
| Nonobject affinity JSON | Unhandled `AttributeError` | Require the child affinity record to be an object |
| Final machine snapshot fails after input drift | Secondary `OSError` escaped before `manifest.json` | Retain the original operational error and separate `final_machine_error`; return 3 |

Malformed and nonobject affinity are two forms of the same metadata-retention
boundary. Final snapshot failure also revokes an otherwise complete manifest;
its successful per-run records remain visible. The fixes do not retry, discard
slow samples, relax affinity, or redefine the frozen benchmark.

## Executed checks

```sh
python3 -m unittest discover -s tests/python -p test_vst3_matrix_failures.py -v
python3 -m unittest discover -s tests/python -p test_vst3_matrix.py -v
git diff --check
```

Before repair: **8 tests, 2 failures and 2 errors**, exit 1, in 0.076 seconds.
After repair and the additional success-revocation regression: **9 tests passed**,
no skips, in 0.197 seconds. The **7 existing tests passed**, no skips, in 0.036
seconds. Diff whitespace checks passed. The other focused cases cover empty
stdout, nonzero exit, rejected affinity, input removal/drift, unsupported-platform
rejection, and stale/failed/unavailable Ninja preflight. The existing suite retains
exclusive-directory refusal, exact summaries and slow-callback acceptance.

The `canonical`, `statistics`, `verify_result` and `load_plan` function source
segments are byte-identical to the parent commit. `PLAN_HASH` and the exact plan
file are unchanged. Only failure-path handling changed in the launcher; original
archives and numerical/performance reports were not edited or re-executed.

## Exact identities

Parent source commit: `df3a46153e9fe6b6e693f5564b8538ddabaf09a2`.
The test run used the subsequent working-tree repair below.

| File | SHA-256 |
| --- | --- |
| `tools/run_vst3_matrix.py` | `c91f2ec42bd2934120d63599b4d68c68295a99b2090f8bfc0b358ebe1e9c4159` |
| `tests/python/test_vst3_matrix_failures.py` | `d74bbfec1cd4b54fe66db0b42bd407669354c05dac5f0be68d340b94f024cfaa` |
| `tests/python/test_vst3_matrix.py` | `75000cbb7525aa98a7d3fe39917cddbffed03ac184009d5cd8c91d4401241534` |
| `docs/validation/quadrature-benchmark-plan-20261123.json` | `1a90b748a5eee9c6bec641f4386cbcaa7ab1257f8db2c0100539fb8640832ea8` |
| Launcher before repair | `ebcd2fd1b0246334f6b1e13fc5c665bfd80394c8a0706d7d251fa32c93e50969` |
| Local `build/w3-53-launcher-red.log` | `7222b1bdcec741cf6a53259cc9fb8a9e9fb2523dc21d99176eaaa6c7271de0a2` |
| Local `build/w3-53-launcher-green.log` | `988de46ea79cb27c0f5fbdc51ff6d5e241c36e2363308b16aadd0b649793d66d` |
| Local `build/w3-53-launcher-existing.log` | `9b157eb433e55db83d4297e71c8d53d8c0433b28f771e1182cad7b74f2c9fb03` |

## Limits and handoff

These are controlled local Python 3.12 failure-path checks, not native platform,
affinity-permission or process-scheduling observations. A final manifest still
requires writable output storage; arbitrary I/O exhaustion cannot guarantee full
retention. No C++ build, 72-process capture, audio replay or numerical reference
was run. RT-001 remains incomplete and physical fidelity remains unvalidated.
Root can publish the repair and run normal native Python CI; historical captures
retain their original launcher identities and exact measured scope.
