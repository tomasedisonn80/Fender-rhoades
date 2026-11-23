# ADR 0002: Use a portable C++ core and small project tooling

- Status: Accepted for project foundation
- Date: 2026-11-23
- Basis: Reproducibility, bounded runtime work, and reviewable initial scope

## Context

The project needs executable numerical primitives and a reliable agent workflow
before selecting a plug-in/UI framework. An empty repository does not justify a
large framework stack or a dependency whose source rights have not been checked.

## Decision

Use C++20 for the initial processing/research kernel, CMake/CTest for portable
build and behavioral tests, and Python standard-library tooling for canonical
project state, deterministic generated status, and its tests. Keep model math
separate from host adapters and offline file rendering. Do not introduce a
mandatory third-party audio SDK, GUI framework, or external numerical package in
the initial foundation.

## Consequences

A fresh checkout has a small and inspectable dependency surface. Runtime storage
and work limits are explicit. The C++ library and offline CLI do not imply a
production plug-in or a calibrated physical reference. More capable offline
numerical tooling may be added with documented provenance, versions, licensing,
and reproducibility. Future plug-in formats, UI frameworks, packaging, and
signing are separate decisions with actual host verification.

## Alternatives considered

A plug-in framework from day one would accelerate host scaffolding but would not
resolve physical identification. A Python-only runtime is convenient for
experiments but does not establish the intended callback constraints. Separate
reference tooling and a narrow C++ core allow both investigation and measurable
runtime behavior without conflating their purposes.
