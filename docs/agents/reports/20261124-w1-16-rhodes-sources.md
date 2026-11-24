# Wave 1, assignment 16 — factory-manual physical boundary

**Agent:** `/root/w1_02_contact/w1_16_rhodes_sources`  
**Sources accessed:** 24 November 2026. Only two original-manual chapters,
hosted by the Fender Rhodes archive, were consulted. No third-party source,
figure or recording was copied into the repository.

## What the manual establishes

[1979 manual, chapter 2](https://www.fenderrhodes.com/service-manuals/1979/ch2.html)
describes a common pedal-operated damper release bar and push-rod linkage.
It distinguishes bass, middle and treble damper modules and felt dimensions,
and explains that linkage play affects pedal response. These are construction
and regulation facts supporting a shared linkage with per-key contact geometry;
they do not establish one uniform damper constant across the keyboard.

[1979 manual, chapter 4](https://www.fenderrhodes.com/service-manuals/1979/ch4.html)
relates hammer motion to damper clearance through the bridle strap, identifies
adjustable leaf-spring tension, and documents register-dependent hammer-tip
height/hardness groups. It also permits changed group boundaries and custom
escapement. Key-dip and tonebar-height changes require associated regulation
checks. Consequently, factory settings are documentary priors tied to this
manual's construction, not observations of the unidentified target specimen.

## Missing calibration and one concrete data requirement

Neither inspected chapter supplies a dynamic force–compression–velocity law,
effective hammer mass, restitution curve, modal damping measurement or calibrated
pedal-to-contact trajectory. The hardness table does not identify a durometer
scale. Therefore it cannot determine the repository's synthetic `p=1` stiffness,
damping coefficients or a MIDI-to-hammer-speed mapping. Nothing inspected closes
`SPEC-001`, `DATA-001` or measured-fidelity acceptance.

**Recommended next data-validation requirement:** preserve original quantity,
unit, source location and evidence kind separately from normalized SI values,
then flag inconsistent documentary conversions instead of silently treating
them as measurements. There is a concrete source example: chapter 4 prints
`3/8 inch` beside `9.925 mm` in the hammer-tip table, whereas the exact conversion
is `3/8 * 25.4 = 9.525 mm`. A fixture should retain both printed values, record
the discrepancy, and require an explicit reviewed normalization choice.
This identifies an inconsistency in the inspected transcription; the original
printed page was not separately inspected, so its origin is undetermined.

## Repository and verification boundary

Read `AGENTS.md`, required state/roadmap/README files, the research registry and
`docs/CONTACT_REFERENCE.md`. The latter correctly labels all current contact
parameters synthetic and keeps the offline solver outside VST3 processing.
`project.py validate` and `sync --check` passed; `next --json` showed existing
claims and the two specimen/data blockers. This was a source review, with no
candidate run, build, source/state change or hardware experiment. Only this
report was written.
