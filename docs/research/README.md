# Research provenance

The project starts from the user-supplied [research brief](USER_RESEARCH.txt).
That attachment is an engineering proposal, not experimental evidence for this
repository. The separate PDF mentioned within it was not supplied during setup.
Its private link was removed from the public archive.
The [input manifest](input-manifest.json) records both text hashes and the exact
archival transformations.

The [source registry](sources.json) distinguishes sources opened during setup and
development from research leads carried over from the brief. `reviewed` means the
relevant source was inspected; it does not validate a specimen, confirm all historical variations,
establish data rights, or approve reuse of an implementation.

## Decisions grounded in the brief

- Start with a documented Mark I Stage 73 and a declared load/output boundary.
- Use persistent mechanical states, physical excitation and damping, and nonlinear
  magnetic conversion. Modal reduction is permitted when its error is measured.
- Reconstruct local motion before applying pickup nonlinearity. Flux linkage
  already includes coil turns; do not apply the winding count twice.
- Include every pickup branch in the electrical network, including stationary
  tines. Keep shared nonlinear circuits shared.
- Develop an accurate offline reference before optimizing a runtime reduction.
- Resolve component values, circuit revision, identification ambiguities, and
  measurement uncertainty before claiming an exact historical model.

## Evidence boundaries

The initial C++ implementation deliberately supplies small analytical primitives
and synthetic integration tests. It has no measured hammer law, identified field
map, verified specimen wiring, calibrated per-key parameters, or complete Stage
control circuit. Analytical tests establish implementation properties only.

The service manual distinguishes historical construction and action revisions.
The firsthand pickup service account describes changes in wiring and magnets.
These are reasons to identify the actual specimen rather than silently combine
different revisions. Falaize's author page and Pfeifle's DAFx paper establish
relevant prior physical-model work; their results cannot be transferred as this
repository's accuracy or CPU measurements.

## Development references and implementation scope

`SRC-027` records [Bilbao, Torin and Chatziioannou's author preprint](https://arxiv.org/abs/1405.2589v1),
version 1 submitted on 11 May 2014. Sections 2.1–2.4 connect contact storage and
dissipation to finite-difference energy balances. This motivates checking the
complete balance and considering an energy-compatible comparison method. The
preprint's draft journal header is not used as its publication date.

`SRC-004` records [Pfeifle's DAFx-17 paper](https://www.dafx.de/paper-archive/2017/papers/DAFx17_paper_79.pdf).
Its hammer-contact and pickup sections inform separate questions about excitation
and conversion from motion to voltage. Neither paper identifies this project's
future specimen or supplies its calibration and runtime measurements.

The [current contact contract](../specs/CONTACT_ACCEPTANCE_20261123.md) selects
synthetic, two-mode, clipped **p=1** contact with adaptive RK4 and step doubling.
It does not implement the Bilbao paper's difference scheme or inherit its
stability proofs. The outer force clamp and corresponding loss ledger are
repository model choices, as explained in the
[initial contact-law review](../agents/development-20261123/W1-A001.md).
The [offline reference](../CONTACT_REFERENCE.md) remains separate from the
impulse/switched-loss audio engine. The
[retained contact audit](../validation/contact-audit-summary-20261123.json)
still reports 26 failed refinement checks and missing continuous extrema;
public literature does not waive those acceptance gates.

Both records are link-only citations. This update imports no third-party code,
figures, recordings or numerical parameter sets and establishes no physical
fidelity result.

## Source intake checklist

Record a stable URL, author/title, publication revision, date reviewed, relevant
section or figure, allowed use, and which claim it supports. Preserve uncertainty
and contradictions. A derived value needs the equation, input provenance, units,
and sensitivity to those inputs. Never treat a default SPICE device or a fitted
synthetic constant as a measured historical part.
