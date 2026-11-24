# W3-57 — Public manual and physical-model boundary

**Agent:** `/root/w3_57_public_model_boundary`  
**Decision:** no material documentary-to-calibration boundary violation found.  
**Public sources inspected:** 24 November 2026; HTML republications of the factory manual.

## Supported construction and adjustment claims

[Chapter 2](https://www.fenderrhodes.com/org/manual/ch2.html), damper-release,
push-rod and module sections, describes common pedal linkage and register-specific
damper construction. It supports considering shared actuation with local geometry,
not assigning identical physical damping to every key.

[Chapter 4](https://www.fenderrhodes.com/org/manual/ch4.html), sections 2–5 and
timbre/volume adjustment, relates escapement, hammer-tip groups, bridle-strap
damper clearance, strike position and pickup alignment. It permits customized
settings and group boundaries. These are documentary construction/regulation
constraints; applying this manual's configuration to the unidentified target
would require specimen/revision evidence.

The inspected chapters do not supply measured modal masses/losses, a dynamic
force–compression–velocity law, or a calibrated magnetic linkage map. Hammer-tip
hardness labels alone cannot identify contact stiffness, dissipation, restitution
or MIDI-to-impact-speed calibration. This is an inference from the information
provided, not a claim that no such measurements exist elsewhere.

## Dimensional contradiction

Chapter 4's hammer-tip row 41–50 prints `3/8 inch` alongside `9.925 mm`.
The unit calculation is `3/8 × 25.4 = 9.525 mm`; the same chapter uses
9.525 mm for other 3/8-inch dimensions. This independently confirms
[W1-16's transcription finding](20261124-w1-16-rhodes-sources.md).
Retain both printed values and the derived conversion; do not silently promote
either to a measured hammer dimension. No original printed-page image was
inspected, so the discrepancy's origin remains unresolved.

## Repository consistency and next research step

`NUMERICS.md` labels the impulse, switched-loss, polynomial-pickup and resistive
network constants synthetic. `ARCHITECTURE.md` distinguishes target mechanics
and reciprocal transduction from implemented primitives. The contact guide
limits passing results to four synthetic fixtures and leaves PHY-002 incomplete.
The specimen identity remains unknown. Batched observation does not change these
physical assumptions.

Next bounded step: define one-key identification observables before fitting.
Once a specimen/configuration is established, acquire synchronized hammer/tine
motion and loaded pickup voltage across repeated measured strikes, with pickup
position and load recorded. Reserve whole strike/release conditions for
validation. Until then, keep algorithmic contact/extrema work synthetic and
retain the dimensional discrepancy as unresolved documentary evidence.

## Checks and limits

`project.py validate`, `sync --check`, and `next --json` passed at revision 27,
with 21 evidence records and both specimen/data blockers intact. Read the required
operating/state documents and inspected the files below. Only this report was
created; no model, state, ledger, build, test suite, hardware experiment or Git
publication changed. No figures, recordings or parameter sets were imported.

## Examined local identities

| File | SHA-256 |
| --- | --- |
| `docs/NUMERICS.md` | `5307150655f5044d19a4ceae2aa357254cc0209130f0a1a6a4a553a4a054bc86` |
| `docs/ARCHITECTURE.md` | `65116bc34c4beea134acbff8094f1b2c7dca5949ad33334d966e23e332abce51` |
| `docs/CONTACT_PEAK_DIAGNOSTIC.md` | `37825a75518ddc7baa212b302af4fe403666f055deb54bb3d92dd2b9d6c3cde4` |
| `docs/MEASUREMENT_PLAN.md` | `ac70c6955fa44e23df761830305d983c105fc85e052fb6e6c954e317fd55f624` |
| `profiles/mark-i-stage-73/specimen.json` | `a89cfac5704e566eb01283ae126b8b5a2929b552c6e24eb9daf0543033dd9217` |
| `include/rhodes/research_engine.hpp` | `7dca093866c19f18f4403f86daea96ac485d86b3e6aebc4441477bd5a8a16621` |
| `src/research_engine.cpp` | `f0facfcb2c28f989c79407d2e0571a868b07ec7a37511bc05f7a33efcdeb0703` |
| `docs/agents/reports/20261124-w1-16-rhodes-sources.md` | `d2e932870b589de577a80e2da8309d41c33fa0721376d869a944943c6a70675b` |
