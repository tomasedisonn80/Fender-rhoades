# W1-017 — durable supported-impact schema regressions

Date: 24 November 2026. Worker: `w1_017_supported_schema_tests`.

## Outcome and scope

Added [test_contact_supported_schema.py](../../../tests/python/test_contact_supported_schema.py)
with **28 passing standard-library test methods** for
[contact_supported_schema.py](../../../tools/contact_supported_schema.py).
No implementation defect surfaced in the exercised cases. Only the new test
file and this report were authored; schema implementations, producer, canonical
state, historical reports, and Git were not changed.

The test records independently spell out the published wire fields, rest
initialization, and thirteen frozen setting combinations for each of
`supported_impact` and `supported_impact_scaled`. The builder does not obtain
its records from `observe()`, `fixtures()`, the module's expected settings, or
local native output. This makes the suite runnable without
`build/supported-extrema-candidates`, a compiled executable, or optional
numerical packages. The existing W1-009 report supplied context, not imported
test expectations.

## Regressions covered

The tests exercise exact initial physical observations in both coordinate
systems, one-ULP duration and setting changes, absent setting fields, forbidden
cross-axis combinations, depth-two settings, and unsupported fixture names.
Interior and final observations must retain disabled-damper and action-free
zero ledgers. Signed geometric damper observations remain allowed.

Additional mutations cover relaunch snapshots, disabled-contact events,
impossible clamps, action work and peak origins, event/peak bracket widths,
peak units and coverage metadata, disabled extrema, unknown/missing nested
fields, boolean numeric values, invalid array shapes, NaN/infinity/overflow,
duplicate JSON keys, invalid UTF-8, malformed envelopes, and excessive depth.

Collection tests retain identical and whitespace-distinct duplicates, missing
slots, unexpected settings, failures, and unavailable peaks deterministically.
A deliberate 26-slot matrix with a partial producer failure and absent peaks
remains coverage-complete while reporting those outcomes separately and
`numerical_acceptance: "not_evaluated"`. Empty and 64/65-record boundaries,
bounded iterable consumption, metadata mismatch, immutable raw-hash identity,
and unchanged four-fixture legacy scope are also exercised. A fresh interpreter
explicitly rejects attempted NumPy/SciPy imports while parsing and collecting.

## Observed verification

Commands ran from the repository root:

```sh
python3 tools/project.py validate
python3 tools/project.py sync --check
python3 tools/project.py next --json
python3 -S -m unittest discover -s tests/python -p test_contact_supported_schema.py -v
python3 --version
sha256sum tests/python/test_contact_supported_schema.py tools/contact_supported_schema.py tools/contact_peak_schema.py tools/validate_contact_reference.py
```

All exited **0**. State validation reported 14 tasks, 26 evidence records, and
unvalidated fidelity; generated state was current. Revision 32 had no unclaimed
available tasks. The focused suite reported **28 tests in 0.274 seconds, OK**
under **Python 3.12.14**. This duration is test execution, not a performance
benchmark.

| Tested source | SHA-256 |
| --- | --- |
| New test file | `b56db910038e67fad3d5f53d238e504d137e5b900363d1c2d524e0399bef7fe2` |
| Supported schema | `42d821bf535d77e7fad29da48c25dc9ac1bf2cba2fb68e85efe92afa4a82c7c7` |
| Legacy peak schema | `af5cbf87d155ecb40c176b78a7527b2a7bd686c7fd515be621af93d910db86c4` |
| Independent contact definitions | `c910da5da6b6ad957b709a414cef39a08ed6e8b75c6e36c9d73ef535a064dcdc` |

## Limits and handoff

Constructed records repeat the initial state at the final endpoint deliberately;
they are structural witnesses, not physical trajectories. These tests do not
establish final-state accuracy, continuous extrema, numerical acceptance, or
producer authenticity. No native build, CTest, full Python suite, reference
integration, or existing native-candidate replay was run. The collection byte
budget remains the caller's responsibility. The integrator can include this
durable suite in its focused checks while continuing the separately bounded
native/reference comparison; PHY-002 acceptance remains unchanged.
