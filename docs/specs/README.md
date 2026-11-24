# Implementation contracts

These specifications make future tasks concrete. They do not mark those tasks
complete; use [the generated project state](../PROJECT_STATE.md) for status.

| Contract | Scope |
|---|---|
| [Contact reference](CONTACT_REFERENCE.md) | PHY-002 hammer, damper, pedal, normalization and energy fixtures |
| [Transduction and network](TRANSDUCTION_NETWORK.md) | MAG-001/ELEC-001 signs, reciprocal energy, smooth map assets and complete pickup graph |
| [Reference solvers](REFERENCE_SOLVERS.md) | Equilibrium, convergence, grazing events, aliasing and bounded failure |
| [Runtime and assets](RUNTIME_AND_ASSETS.md) | Host events, prepared data, lifecycle, serialization and timing evidence |
| [Original VST3 prototype](HOST_PROTOTYPE_20261123.md) | HOST-001 direct prototype contract, with later MIDI/control overrides identified |
| [MIDI interoperability](HOST_MIDI_20261124.md) | HOST-002 channel CC123, validated pressure no-op, strict control text and acceptance gates |
| [Specimen and acquisition](DATA_INGESTION.md) | Passport revisions, calibration validity, uncertainty, partitions and payload verification |
| [Optional circuits and acoustics](OPTIONAL_CIRCUITS_AND_ACOUSTICS.md) | Revision-specific shared circuits, optical/thermal histories and receiver paths |
| [Acceptance metrics](ACCEPTANCE_METRICS.md) | Level/timing, modal/transient metrics, identifiability, uncertainty and listening gates |

The current implementation is described separately by [NUMERICS](../NUMERICS.md).
Proposed fixtures and tolerances are clearly distinguished from executed tests.
