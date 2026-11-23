# Measurement data contract

There are **no measurements of a physical Rhodes in this repository yet**.
`examples/synthetic-session.json` is a metadata-validation fixture, not an acquired
session or a calibration source. No measured fidelity gate can use it.

Raw recordings and motion/impedance captures stay in `data/raw/` (ignored) or an
owner-approved immutable store. Commit compact manifests containing exact hashes,
units, calibration, instrument identity, rights, acquisition conditions, and split
assignment. Do not rename synthetic fixtures to make them appear measured.

Read [the measurement plan](../docs/MEASUREMENT_PLAN.md). Fit/validation splits
separate whole acquisition conditions and sessions, not adjacent windows from one
recording. Loading, pickup geometry, mechanics, temperature, and calibration form
part of the observation. Hashes establish identity; they do not establish accuracy.

Run `python3 tools/validate_data.py` for committed contracts and fixtures. An
unidentified profile may contain null values. A measured manifest requires an
identified specimen and complete acquisition/calibration metadata.
