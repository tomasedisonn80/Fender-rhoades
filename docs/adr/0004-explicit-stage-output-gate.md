# ADR 0004 — Give passive Stage controls their own acceptance gate

**Status:** Accepted for the development plan; implementation remains future work.

## Context

Review [A021](../agents/reviews/A021.md) found that passive Stage controls appeared
in the architecture and keyboard phase, but their component inventory pointed
only to the optional circuit task. That ambiguity could allow a completed
keyboard claim to hide an unimplemented Stage jack. Review
[A022](../agents/reviews/A022.md) also required explicit configuration/output
identity across profiles.

## Decision

Add `STG-001` after the loaded-harp one-note gate. It identifies and validates the
actual volume/bass graph, pot taper/endpoints, connected load, and Stage-jack
response. It requires measurement, numerical, and held-out hardware evidence.
The mechanism inventory assigns volume and bass control work to this task.

Keep loaded-harp and Stage-jack configurations distinct. Bind assets and presets
to profile, configuration revision, netlist hash, output boundary and parameter
schema version. Reject incompatible assets without silently substituting them.

Optional powered Suitcase work (`CIR-001`) follows Stage completeness and retains
separate Peterson/Janus identities. This ordering does not prevent preparatory
research, but later task completion cannot bypass the defined Stage gate.

## Consequences

The task graph now has an explicit control-circuit acceptance artifact. A
measured loaded-harp keyboard can be described within that boundary while Stage
jack work remains incomplete; it cannot be presented as a complete Stage output
model. No historical component values or specimen wiring were invented.
