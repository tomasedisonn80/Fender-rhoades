# Specimen and acquisition acceptance contract

## Status and ownership

This contract consolidates reviews A046–A050. It defines work required before
closing `SPEC-001` and `DATA-001`; it is **not an implemented version-2 schema**.
The committed version-1 manifests support synthetic development and structural
checks. Passing them alone cannot establish calibration validity, independent
holdouts, payload integrity, or an authentic instrument identity.

Version the schema, validator, fixtures, migration guidance and evidence gates
together before accepting actual acquisitions. Preserve version-1 records as
historical inputs. Never fill missing fields with invented observations merely to
make a migration pass. The task integrator reviews these acceptance criteria in
addition to the automated evidence-kind checks.

Current discovery scans `profiles/*/specimen.json` and matching component inventories,
rejects duplicate profile IDs, and treats JSON beneath `data` as session manifests
except paths containing a `raw` component. Explicit evidence links do not change
those rules. Do not place calibration records or verifier reports in that session
tree, or archive same-ID profiles as active sibling profiles. The upgrade must
coordinate an explicit discovery/index policy with distinct stable specimen
identity, immutable configuration revisions/hashes and session bindings. Preserve
old bound bytes and their meaning rather than overwriting them.

## 1. Reproducible specimen passport

Bind the passport to a stable specimen ID and a configuration revision. Record
the predecessor revision, change time, observer, reason and evidence for every
repair, replacement, regulation change or electrical modification. Distinguish
observed markings, inferred construction dates, documentary expectations and
unknowns. Store dated inspection records and photographs with resolvable artifact
IDs, byte counts, hashes, attribution and reuse rights.

The current schema requires a serial for an `identified` profile. If the actual
serial is missing, retain null and implement an explicitly reviewed alternative
identity policy supported by distinguishing observations. A string such as
`unknown` is not a serial and does not satisfy scientific identity.

For each key, keep separate fields for:

| Field | Meaning |
|---|---|
| Physical index | Zero-based position, `0..72` for this target |
| MIDI note | Project convention, `28 + physical_index` |
| Normalized piano-key number | `8 + physical_index` under the 88-key convention |
| Observed stamping | Verbatim marking, including missing or inconsistent stamps |
| Installed assembly | Revision, replacement history, condition and inspection evidence |

The primary manual describes both original 1–73 and later 8–80 tonebar markings
([SRC-019](../research/sources.json)). Do not infer the installed assembly or octave
from a normalized number alone.

Freeze the mechanical and electrical boundary: mounting/support condition, cover
position when relevant, action and damper regulation, hammer tips, tine/tonebar
assemblies, tuning springs, pickup pose, wiring membership, controls and repairs.
Name the measured output nodes and the `loaded_harp` or `stage_jack` boundary;
record cable, probe, receiving resistance/capacitance and their uncertainty. A
loaded-harp result does not complete `STG-001`.

## 2. Calibration records and uncertainty

Calibration references must resolve to immutable records with equipment serials,
channel IDs, configuration hash, method/version, reference-standard IDs,
calibration date, validity interval and operating domain. Resolve reference
standards and uncertainty budgets too; prose claiming traceability is insufficient
for an automated match. Record acquisition start/end timestamps and demonstrate
that the applicable calibration covers the entire acquisition interval.

Use declared correction models with named parameters and units. Store standard
uncertainties and the hashed derivation budget. Expanded intervals additionally
need coverage factor, coverage probability, interval method and distribution or
degrees of freedom where used. Do not assume a factor of two always means 95%
coverage. Separate measurement uncertainty from parameter-fit uncertainty and
population variability.

Covariance records specify parameter order and product units. Check dimensions,
finite values, symmetry, positive semidefiniteness and diagonals matching the
squared standard uncertainties within declared numerical tolerance. Missing
covariance means unknown dependence; it does not justify assuming independence.
Shared standards, loads, positioning fixtures and clocks can correlate errors.

For synchronized motion and voltage, identify clocks, reference channel, polarity,
delay, drift and their uncertainty. Retain measured gain/phase response and
analogue filtering over the accepted band. Declare whether response corrections
already include delay so that preprocessing cannot remove it twice. Unknown
synchronization blocks phase-sensitive claims.

Check the signal, frequency and temperature domain against the record's calibrated
range. Preserve saturated or otherwise invalid takes and their exclusion reasons.
When a calibration changes, identify dependent derived assets, fits and acceptance
reports and re-evaluate them; do not overwrite raw observations.

## 3. Frozen partitions and derivation ancestry

Freeze an experiment/partition policy before fitting. Define the intended claim:
interpolation within observed construction zones, extrapolation to a different
load or pickup pose, or generalization to a separate instrument are distinct
experiments. One specimen does not establish population-wide fidelity.

Give acquisitions, phrases, conditions and derivations stable identities. A policy
chooses which grouping keys must remain within one partition. Record all parent
artifact hashes and processing configuration for crops, resampling, filtering,
alignment and feature extraction. Derived artifacts inherit partition restrictions
from their ancestors even when their byte hashes differ. Multiple references to
the same capture do not become independent repeats.

Fit data may select parameters and model structure. Validation data may support
predeclared development choices. Reserve test data for the final frozen comparison;
repeatedly tuning against it turns it into development evidence. Any policy change
creates a new version and invalidates the old independence claim. Shared calibration
references may be partition-independent only under an explicit policy; they must
not contain target responses used to tune the model.

Version-1 checks catch equal condition IDs and identical payload digests crossing
declared splits. They cannot discover undeclared relationships or different-hash
descendants. Add rejection fixtures for same-acquisition records, cropped/resampled
descendants and mixed-condition phrases crossing a forbidden partition.

## 4. Payload verification and immutable import

Manifest validation and byte verification are different operations. Define an
explicit payload root or approved immutable storage locator. The metadata checker
does not fetch URLs or open payloads. A verifier must compare actual byte count and
streamed digest against the manifest, require regular files, contain path traversal,
and reject symlink escapes. Its concurrency model must be documented; a one-time
pathname check cannot protect against an adversarial writer replacing components.

The foundation now includes an opt-in local implementation,
[`tools/verify_payloads.py`](../../tools/verify_payloads.py), with
[usage and limitations](../../data/README.md). A053 verified its corruption,
path, ordinary mutation and exclusive-publication behavior with 18 focused tests.
This covers local byte integrity only; the versioned metadata upgrade, immutable
importer and external-storage protocol below remain future work.

Produce a new immutable report identifying manifest hash, verifier version,
location, observed counts/digests and failures. Include missing, unreadable,
truncated, corrupted and concurrently changed payload fixtures. Payload integrity
proves which bytes were inspected; it does not prove the bytes came from a Rhodes.

An importer stages verified bytes and publishes only after all required checks
pass. It must not overwrite an existing observation or silently salvage a failed
capture. For external storage, define retrieval authentication, immutable object
version and availability policy without putting credentials in the repository.
Use explicit approved storage only; manifests are data, not executable commands.

## 5. Evidence and completion

Bind evidence to the exact profile and session manifest hashes. `specimen_identity`
supports the passport gate; `measurement` supports a calibrated capture; and
`hardware_validation` supports specified held-out comparisons. A passing numerical
test cannot be relabeled as any of these.

Before completing a task, the integrator checks its full acceptance criteria,
including provenance, payload verification, calibration validity, partition policy
and output boundary. Automated consistency checks cannot attest that the reported
experiment occurred or that the operator described it truthfully. Record failed
conditions and unresolved uncertainty alongside successful results.

The next software step is a coordinated ingestion-schema change with independent
negative fixtures. Actual specimen inspection and calibrated captures remain
external inputs. No data in the current repository satisfies these physical gates.

## 6. Pilot experiments and discriminating observations

Reviews A051, A052, A054, A055 and A057 refine the acquisition sequence. These
experiments are proposed, not executed; choose numeric levels and counts from an
actual repeatability pilot before freezing the campaign.

| Experiment | Record together | What it distinguishes |
|---|---|---|
| Native action across strike groups | Key travel, hammer free flight, pre-contact speed, local tine motion, voltage and common timebase | Controller velocity labels, launch speed and relative impact speed; force remains a separate identification problem |
| Repetition before full return | Action state, continuing tine phase, retrigger interval, separation/recontact and loaded onset | Persistent mechanics versus phase-reset or memoryless excitation |
| Pickup pose and return-to-baseline | Gap/orientation, equilibrium, local motion, voltage, branch current and support condition | Sensitivity changes versus magnet-induced mechanical changes and drift |
| Spatial modal observation | Calibrated force-to-motion transfer at contact, pickup, tonebar and support locations | Contact participation versus pickup observability; a weak pickup signal does not imply an irrelevant contact mode |
| Apparatus perturbation | Noncontact baseline, added reflective targets/sensors, attachment and cable forces | Observation-induced mass, stiffness and loss changes |
| Complex impedance | Calibrated voltage/current, real/imaginary response, amplitude, temperature, fixture-plane standards | Coil/load dynamics beyond DC resistance and limits of a small-signal fit |
| Connected Stage controls | Both pot positions/tapers, endpoint and near-endpoint behavior, cable/load, gain/phase and input impedance | Coupled passive behavior versus an independent gain/EQ approximation |

Pair every MIDI label with the actual controller/sensing configuration. If no
such bridge is calibrated, leave it unspecified. Impact speed is in m/s; the
current normalized renderer intensity is neither that speed nor a measured force.

Geometry experiments must declare how modal parameters contain magnetic effects.
Either identify mechanics excluding the modeled contribution or apply a consistent
change relative to the reference geometry/load. Do not add full magnetic stiffness
and damping to effective modes that already include them. An isolated open coil
and an open harp terminal are different electrical conditions; parallel branches
may carry circulating current despite zero terminal load current.

For impedance, calibrate fixtures at an explicit plane and retain both raw and
corrected observations. Remove acquisition hardware effects only to the extent
supported by their calibration; keep the instrument cable/load inside the chosen
model boundary. Check a small-signal amplitude plateau and separately investigate
the observed operating domain. Propagate de-embedding uncertainty and identify
poorly conditioned frequencies.

For Stage controls, acquire a combined volume/bass matrix, corners, endpoint
neighborhoods, forward/reverse travel and held-out resistive/reactive loads.
Avoid assuming exact mute or unity gain. Use absolute residual limits near nulls,
where a relative ratio alone becomes ill-conditioned. Preserve stationary branches
throughout every circuit comparison.

Freeze required cells and exclusions before the final comparison. Randomize order
within feasible blocks, repeat baseline conditions, and reserve a re-established
session. Record uncovered combinations explicitly. Later keyboard claims require
held-out keys around measured construction transitions and register extremes;
one pilot key cannot establish those claims.
