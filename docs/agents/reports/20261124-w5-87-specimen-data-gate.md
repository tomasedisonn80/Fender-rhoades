# W5-87 — Specimen and calibrated-data gates

Evaluator: `/root/w5_87_specimen_data_gate`; 24 November 2026.

**Keep SPEC-001 and DATA-001 blocked; fidelity remains `unvalidated`.**
The inspected state is revision 27, with 23 evidence records and no
`specimen_identity`, `measurement` or `hardware_validation` record. The observed
publication head was `766dd11c73cf9d1dcf6c1960b4333767356f9937`; the exact reviewed
inputs are bound below. This review makes no canonical transition.

## Actual inputs and boundary

An inventory including ignored files found exactly four files beneath `data/`
and `profiles/`: the data README, one synthetic session, one specimen placeholder
and its component requirements. No raw capture was present there. The
[passport](../../../profiles/mark-i-stage-73/specimen.json) remains unidentified:
serial, construction revisions and load values are null, wiring is unverified,
instrument evidence is empty, and 440 Hz is explicitly assumed. All 18 component
entries retain unknown provenance, null values and no measurement references.
They describe mechanism requirements, not a measured bill of materials.

The [session](../../../data/examples/synthetic-session.json) is `synthetic` and
`development`, with no acquired timestamp, operator, calibration or artifacts.
Its 1 MΩ load and 48 kHz rate are fixture metadata. They cannot identify the
physical output boundary or calibrate the plugin. The numerical contract labels
the runtime constants synthetic; the pickup/network API preserves that boundary.

The source registry labels the manual as link-only documentary material requiring
specimen reconciliation. Its numbering convention does not establish installed
assembly identity. [W3-57](20261124-w3-57-public-model-boundary.md) separately retains
the printed 3/8-inch versus 9.925-mm discrepancy and derived 9.525-mm conversion;
neither enters the specimen/component records as a measurement. This review
inspected those annotations and retained report, without reopening external pages
or claiming a new manual inspection.

## Minimal useful next input package

1. **Identity and configuration:** dated inspection photographs and notes for one
   actual Stage 73, observed serial/date markings or explicit unknowns, repairs,
   action/pickup condition, wiring observations, and distinct physical index,
   normalized key number and observed stamping. Identify the loaded-harp nodes,
   pickup pose, cable, probe and receiving load. Retain resolvable files, hashes
   and sharing rights; do not substitute a guessed serial.
2. **One-key calibrated pilot:** immutable synchronized hammer/tine motion and
   loaded-voltage captures, with measured excitation, geometry and loads, repeated
   baseline strikes, retriggers, release and pedal conditions. Supply channel and
   clock calibration records, gain/polarity/filter response, delay/drift, validity
   intervals and uncertainty. Select numerical levels and repeat counts from the
   repeatability pilot before freezing fitting conditions.
3. **Manifest and independence:** specimen/configuration bindings, timestamps,
   equipment/channel identities, SI units, raw byte counts/SHA-256, durable file
   locations, quality flags, rights and raw-to-derived ancestry. Freeze complete
   condition/acquisition groups for fit, validation and final test; reserve unseen
   strike, retrigger, release/pedal, pickup-position and load conditions.

The [ingestion contract](../../specs/DATA_INGESTION.md) also requires the coordinated
schema/validator/discovery/fixture upgrade before accepting measured datasets.
Version 1 checks structure and selected split conflicts; it does not resolve
calibration validity or derived ancestry. The separate payload verifier checks
bytes, not instrument authenticity. The package above supplies useful evidence;
its mere receipt or schema acceptance cannot close either gate automatically.

## Executed checks and limits

Required project validation, `sync --check` and `next --json` passed.
`python3 tools/validate_data.py` ran **once**, exited 0 and reported one profile
and one session manifest, explicitly excluding physical-accuracy certification.
Additional work was read-only inventory, JSON/source inspection and hash checks.
Only this report was written; no build, test suite, acquisition, experiment,
source change, Git mutation or state/ledger update occurred.

## Reviewed input identities

These bind the inspected snapshot; later integration metadata may change.

| Path | SHA-256 |
| --- | --- |
| `project/state.json` | `fd69ef9e6c59a436636f96a7a9295b1a42903ea0c853b51350b5f6ce29eba1e9` |
| `project/evidence.json` | `da43524c226f79fb04bb40444d67a18dde873c44b92868f148240d5be4186444` |
| `profiles/mark-i-stage-73/specimen.json` | `a89cfac5704e566eb01283ae126b8b5a2929b552c6e24eb9daf0543033dd9217` |
| `profiles/mark-i-stage-73/components.json` | `28a22c7efc1799f237d635fdff87fd843b22cd9b6a5cc08f44010556b0ec95d4` |
| `data/examples/synthetic-session.json` | `b9cfed497fff7e3cba9d24acc92b9161e6e96209e474185f1c1b32627b7c5c77` |
| `data/README.md` | `e6e4df63231cbd590200e4b143fad429ab636ed7799d3803107808eccf04fba2` |
| `docs/specs/DATA_INGESTION.md` | `3ece1a0bedc9c53aa7e962bf7c72e19fe1f2d2dfb2e74155d9cdcc6fc51c0aa1` |
| `docs/MEASUREMENT_PLAN.md` | `ac70c6955fa44e23df761830305d983c105fc85e052fb6e6c954e317fd55f624` |
| `docs/research/sources.json` | `906d8fe3a7e6f66a6ee05c353f1810f6c36a9b6f47b3c6c8d9db234cb5b7929f` |
| `docs/NUMERICS.md` | `c33fc0b391fd6d1e78156b5c7cb1d698ec9d2e6c200a06ca4b0aa48f71336fe9` |
| `docs/agents/reports/20261124-w3-57-public-model-boundary.md` | `f5a56d227fb6a84e676384b4ea376e7d0f264588ae443c8a84cbad6f0fa34297` |
| `tools/validate_data.py` | `246f89257c3f201ba3b3d9cf0dce5962f46a56ffe4a2cf637d4a35440ed168f0` |
