# W1-009 — strict supported-impact candidate schema

Date: 24 November 2026. Worker: `w1_009_contact_schema`.

## Outcome and scope

Implemented [contact_supported_schema.py](../../../tools/contact_supported_schema.py)
for exactly `supported_impact` and `supported_impact_scaled`, with the frozen
13 depth-one settings per fixture. The module accepted all 26 actual baseline
producer records. Fourteen focused adversarial test methods and all 22 existing
four-fixture schema tests passed.

This is input validation and matrix accounting. It does not run an independent
reference, compare numerical errors, certify peak coverage, or grant PHY-002
acceptance. The returned collection explicitly records
`numerical_acceptance: "not_evaluated"`.

Authored paths are limited to the new module and this report. The original
four-fixture parser, numerical budgets, producer, fixture definitions, canonical
state, and historical reports were not changed. Git and evidence registration
remain the integrator's responsibility.

## Driver API

The following interface was sent to the integrator before final testing:

```python
from contact_supported_schema import (
    CandidateFormatError, ParsedCandidate, parse_candidate, validate_collection,
    SUPPORTED_FIXTURES, RUN_SETTING_FIELDS, EXPECTED_SETTINGS, EXPECTED_COUNT,
    MATRIX_ID, MATRIX_SETTINGS_SHA256,
    MAX_FILE_BYTES, MAX_COLLECTION_BYTES, MAX_FILES, PEAK_NAMES,
)

candidate = parse_candidate(raw_bytes)
collection = validate_collection(parsed_candidates)
```

`parse_candidate(raw: bytes) -> ParsedCandidate` uses the existing frozen
dataclass, preserving the raw byte SHA-256, immutable normalized run, named
settings tuple, and optional peak records. Off-fixture or off-matrix raw inputs
raise `CandidateFormatError`, as do malformed supplied diagnostics. Messages
retain the existing 384-character bound.

`availability` is `"available"` when a complete supplied peak object passes
all schema and fixture checks, and `"not_available"` when that optional object
is absent. It does not encode producer success. A valid failed native record
retains `run["success"] is False` and its nonzero failure code, including a
partial trajectory. The driver must evaluate that producer status before
attempting numerical comparison. A failed producer is never changed to success
or silently treated as unavailable.

`EXPECTED_SETTINGS` is a tuple of 13 exact value tuples in
`RUN_SETTING_FIELDS` order. Slot matching uses exact binary-float equality,
including the Python multiplication results for the five scaled tolerances.
The independent existing depth-one slot declaration is checked against this
frozen identity at import; policy drift raises `RuntimeError`:

- Matrix: `contact-refinement-20261123-extra-1`.
- Settings SHA-256:
  `802f018edbad9396df0af0099cd1d0bf6b0d0a9621b0a5ae95688a283d31e17b`.
- Fixtures: two; slots per fixture: 13; expected total: 26.

`validate_collection(candidates: Iterable[ParsedCandidate]) -> dict` returns
JSON-serializable data. It consumes at most 64 records. Rows and diagnostics are
deterministic under input permutation, and byte-identical repeats remain
duplicates. No record is chosen to win a duplicate slot.

| Result field | Meaning |
| --- | --- |
| `scope` | `supported-impact-depth-one-input-matrix` |
| `matrix_id`, `settings_sha256` | Frozen declaration identity above |
| `expected_count` | 26 |
| `actual_count` | Number of supplied records, including repeats/unexpected rows |
| `distinct_expected_slots` | Number of represented expected fixture/settings pairs |
| `matrix_complete` | Exactly one record in every expected slot, with no duplicates or unexpected rows; this is coverage only |
| `missing_slots` | Ordered list of stable `refinement_slot_id` strings |
| `duplicate_slots` | Rows containing `slot`, `count`, sorted `raw_sha256` values, and `byte_identical` |
| `unexpected_slots` | Defensive rows containing `fixture`, full `settings`, and `raw_sha256`; the raw wrapper rejects these before collection |
| `failed_runs` | Rows containing `slot`, `raw_sha256`, and the retained nonzero `failure_code` |
| `unavailable_peaks` | Rows containing `slot`, `raw_sha256`, and `availability` |
| `diagnostics` | Ordered human-readable missing, duplicate, unexpected, producer-failure, and unavailable-peak messages |
| `numerical_acceptance` | Always `not_evaluated` |

A matrix may have complete slot coverage while containing producer failures or
missing peaks. Those separate lists and diagnostics remain populated; coverage
must not be interpreted as a passed experiment.

The driver must cap the raw collection at `MAX_COLLECTION_BYTES` (128 MiB)
while reading inputs. A `ParsedCandidate` retains its byte hash but not its
original byte length, so the collection function cannot enforce that byte cap.
The underlying parser retains the 32 MiB per-file, 30,000-sample,
100,000-event, depth-64, and bounded-integer checks.

## Added validation

The implementation follows the specific unsupported-fixture warning in
[W1-001](20261124b-w1-001-contact-extrema.md). Calling the old parser and merely
changing its availability flag is insufficient. The new module first performs
the old parser's strict JSON/envelope/SI-field checks, then verifies:

1. Exactly one of the two named fixtures and one of its 13 exact complete
   eight-field setting tuples.
2. Exact initial values for all exported fields, including rest modal states,
   hammer position `-0.0002 m`, hammer velocity `1 m/s`, initial gap and relative
   velocity, zero cumulative ledgers, and initial energy `0.004 J`. Initial
   observation arithmetic uses the existing independent fixture definition;
   it does not import NumPy/SciPy or execute a trajectory solver.
3. No sample beyond `0.020 s`; a successful run ends at exactly `0.020 s`.
   Failed partial runs are preserved. The final observation receives the same
   finite SI-field and fixture-zero checks as every other sample; its physical
   numerical accuracy remains for the independent comparison.
4. Empty relaunch snapshots; no relaunch, motion-change, or impossible clamp
   event; no action work; no action-origin peaks. Native failure codes are the
   declared integer enum range 0 through 21, with the original strict
   success/failure consistency check retained.
5. The hammer is enabled and has zero contact damping; the damper is disabled.
   Disabled force/impulse/loss and actuator/action ledgers are exact zero.
   Damper sampled maxima have exact zero values and times. Located damper peaks
   must be disabled all-zero records, and hammer peaks must be enabled records.
   Signed geometric damper gaps and relative velocities remain valid nonzero
   observations even though the damper is disabled.
6. Native exact unit keys, metadata, nonnegative peak values, bracket
   containment, duration bounds, and stationary width limits remain enforced.
   Event bracket width is additionally bounded by the declared event tolerance
   with the existing `1 + 1e-9` floating-width allowance.

These are consistency checks on a native record, not producer authentication or
a continuous-time extrema proof. Missing events, peak underestimates, inaccurate
sampled motion, or unresolved reference roots still need the numerical driver.

## Actual verification

Required startup commands were executed successfully:

```sh
python3 tools/project.py validate
python3 tools/project.py sync --check
python3 tools/project.py next --json
```

The first check reported 14 tasks, 26 evidence records, and fidelity
`unvalidated`. Generated state was current. State revision 32 had no unclaimed
available work after the integrator's claims; this was an assigned bounded
implementation and made no claim/state mutation.

A `python3 -S` here-document generated all 26 entries from
`run_contact_sweep.plan(sorted(SUPPORTED_FIXTURES), 1,
locate_peak_candidates=True)` using the baseline executable. Each subprocess
used the following argument mapping, with a 30-second timeout and retained
stdout bytes in the test process:

```python
argv = [
    "build/baseline/rhodes_contact_reference",
    "--fixture", item["fixture"],
    "--max-step", repr(item["maximum_step_s"]),
    "--event-tolerance", repr(item["event_tolerance_s"]),
    "--tolerance-scale", repr(item["tolerance_scale"]),
    "--output-step", repr(item["output_step_s"]),
    "--locate-peak-candidates",
]
```

All subprocesses exited 0. Totals were **24,559,764 raw bytes, 26,026 samples,
and 156 events**. Each run had 1,001 samples. The matrix had exactly 26 distinct
expected slots, no duplicates/missing/unexpected rows, no failed producers,
and no unavailable peaks. `numerical_acceptance` remained `not_evaluated`.

The same process explicitly blocked imports of `numpy` and `scipy` and ran 14
`unittest` methods against copies of those actual generator records:

| Test method | Observed result |
| --- | --- |
| `test_real_matrix` | Passed exact counts, hash, availability, and permutation invariance |
| `test_optional_absence_and_invalid_presence` | Absent peaks retained as unavailable; null/empty/malformed supplied objects rejected |
| `test_failed_partial_producer_remains_failure` | Partial failure retained; inconsistent/literal-type/unknown-code statuses rejected |
| `test_exact_initial_state` | One-ULP change to each of 26 exported initial fields rejected, plus initial diagnostic energy mismatch |
| `test_exact_duration` | One-ULP early/late final time and successful missing final sample rejected |
| `test_exact_all_setting_fields` | One-ULP change to each complete setting rejected; off-fixture name rejected |
| `test_action_snapshots_and_events` | Actions, impossible clamps, disabled-contact events, and event work rejected |
| `test_no_action_peak_origins` | Both action origins rejected for all four peak records |
| `test_disabled_and_enabled_semantics` | Swapped enabled/disabled origins and nonzero forbidden fields rejected; positive signed disabled gap retained |
| `test_units_brackets_and_metadata` | Wrong units, certified metadata, invalid/oversized brackets, and boolean numeric fields rejected |
| `test_deterministic_missing_and_duplicate_records` | Missing plus duplicate, identical repeats, and whitespace-distinct repeats retained deterministically |
| `test_collection_caps_and_entry_integrity` | More than 64 records, wrong entry type, inconsistent metadata, and unpromoted legacy-parser record rejected |
| `test_legacy_defaults_and_immutable_records` | Original four-fixture defaults unchanged; immutability and raw hash preserved |
| `test_envelope_and_lexical_rejections` | Invalid UTF-8, duplicate keys, excessive depth, bad envelope, and overflowed numbers rejected |

Observed result: **14 tests passed**, with 9.361 seconds reported by the unittest
runner after candidate generation. This is schema testing, not a runtime
benchmark. The inline harness was not added to `tests/python`, because the
assignment permitted only the new module and this report.

Existing regression command:

```sh
python3 -S -m unittest discover -s tests/python -p test_contact_peak_schema.py -v
```

Observed result: **22 tests passed**, exit 0. No C++ source changed, and no new
build, CTest, independent Radau experiment, or contact-acceptance run is claimed.
`python3 tools/check_docs.py` also exited 0 with documentation integrity checks
passed after this report was written. Both authored files had no trailing
whitespace in a direct line audit.

After the integrator supplied `build/supported-extrema-candidates`, a separate
read-only hash audit verified that its 26 numbered raw files exactly match the
worker-generated records. The canonical manifest of fixture, complete settings,
raw hash, byte length, sample count, and event count had SHA-256
`cd0275c33ed48eb33b999a915386bc4b72be35761839c4a53fa14b9b3e8efcbc` in both cases.

## Tested identities

Observed implementation-checkout HEAD:
`2a8caab2cfdfa8885899b5eabb00f6545392d583`.
This names the checked-out commit; the new module was uncommitted at testing.

| Input | SHA-256 |
| --- | --- |
| `tools/contact_supported_schema.py` | `42d821bf535d77e7fad29da48c25dc9ac1bf2cba2fb68e85efe92afa4a82c7c7` |
| `tools/contact_peak_schema.py` | `af5cbf87d155ecb40c176b78a7527b2a7bd686c7fd515be621af93d910db86c4` |
| `tools/validate_contact_reference.py` | `c910da5da6b6ad957b709a414cef39a08ed6e8b75c6e36c9d73ef535a064dcdc` |
| `include/rhodes/contact_reference.hpp` | `00b99e61dc73686c22675bedf647c3ad1b27ba13332180431f5a42e485057fda` |
| `build/baseline/rhodes_contact_reference` | `4ed2552430f5de5846adcb1a7b99521b118a12b714ad61b146ede7bab24f8f2d` |

## Next step

The independent driver can now read the retained numbered raw files, apply the
byte/file bounds, call `parse_candidate`, and retain all parse errors and
`validate_collection` findings before comparing with the six independent
reference trajectories. It should keep producer failure, missing optional
peaks, numerical failure, and unresolved coverage as separate outcomes. Add a
permanent test module when the integrator assigns that path; no acceptance
status or existing default needs to be broadened to use this wrapper.
