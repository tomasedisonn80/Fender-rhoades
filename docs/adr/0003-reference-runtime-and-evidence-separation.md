# ADR 0003: Separate the physical reference, runtime, and evidence

- Status: Accepted for project foundation
- Date: 2026-11-23
- Basis: Supplied research, sections 3–8, 10, and 12

## Context

An efficient implementation can remain stable while changing decay, phase,
tuning, or nonlinear interaction. An accurate numerical solution can still solve
the wrong physical model. A measured output fit can be non-unique. These are
separate failure mechanisms and need separate evidence.

## Decision

Maintain a detailed offline reference with documented physical assumptions,
operating-point solution, energy/residual accounting, and convergence. Derive
runtime reductions and model assets with explicit domains and error budgets.
Compare runtime behavior against the converged reference, and validate the
reference against independent physical observations. Classify documentary,
synthetic, numerical-reference, measured, held-out, perceptual, and performance
evidence separately.

Persistent per-key state, local modal summation before pickup conversion, all
pickups in the electrical graph, and shared nonlinear stages are architectural
invariants. Any runtime omission of coupling or state requires an explicit,
validated reduction instead of an undocumented optimization.

## Consequences

Initial analytic/synthetic tests cannot promote scientific maturity to measured
fidelity. The project can develop useful software while hardware is unavailable,
with the limitation visible. Generated state reflects reviewed evidence; it does
not autonomously prove an experiment or create a measurement. Numerical,
physical, perceptual, and performance claims remain scoped to actual results.

## Alternatives considered

A single plausible-sounding engine is easier to demonstrate but cannot expose
whether an error comes from physics, numerics, or optimization. A full expensive
reference in the audio callback avoids one reduction step but provides no
bounded-work guarantee. The split makes the tradeoff explicit and testable.
