# Research provenance

The project starts from the user-supplied [research brief](USER_RESEARCH.txt).
That attachment is an engineering proposal, not experimental evidence for this
repository. The separate PDF mentioned within it was not supplied during setup.
Its private link was removed from the public archive.

The [source registry](sources.json) distinguishes pages opened during setup from
research leads carried over from the brief. `reviewed` means the relevant source
was inspected; it does not validate a specimen, confirm all historical variations,
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

## Source intake checklist

Record a stable URL, author/title, publication revision, date reviewed, relevant
section or figure, allowed use, and which claim it supports. Preserve uncertainty
and contradictions. A derived value needs the equation, input provenance, units,
and sensitivity to those inputs. Never treat a default SPICE device or a fitted
synthetic constant as a measured historical part.
