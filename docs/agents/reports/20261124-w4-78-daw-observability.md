# W4-78 — DAW observability review

## Outcome and scope

No material experimental-design or attribution defect found in the reviewed
[protocol](../../DAW_VALIDATION.md). This is documentary review, not an executed
DAW result, source-contract verification, or acceptance evidence for HOST-001,
RT-001 or physical fidelity.

## Observations

- MIDI: the guide conditions holder/release conclusions on distinct delivered
  events, flags rewriting and chasing, and marks unsupported delivery inconclusive.
  Piano-roll intent alone cannot establish plug-in input or restored ringing.
- Gain: the factor-of-two comparison requires identical reset/input and excludes
  clipping or processing. Recording capture point, routing and pan law prevents
  post-fader channel differences being assigned automatically to the instrument.
- Latency: advertised frames, waveform onset and peak location remain distinct.
  The guide does not use cross-variant correlation to establish group delay;
  compensation settings and render timestamps remain part of the observation.
- Timing: requested device buffer size does not establish callback size, and
  export depth does not establish processing precision. Offline renders,
  live-device captures and underrun observations have separate attribution.
  Zero observed underruns is not a deadline bound.
- Identity: the integrator accepted this review's narrow recommendation to record
  fresh DAW/separate-host-process starts when comparing builds. The inspected
  revision now distinguishes on-disk hashes from already loaded module bytes;
  inaccessible identity remains uncertainty, not inferred verification.
- Recall and coexistence: configuration recall is distinguished from a mechanical
  checkpoint; chasing and ambiguous lifecycle operations are explicit confounders.
  Coexistence requires separate MIDI/stems and records host grouping/linking;
  observations remain confined to the named host and conditions.

## Checks and recommendation

Required repository metadata checks passed: `python3 tools/project.py validate`,
`python3 tools/project.py sync --check`, and
`python3 tools/project.py next --json`. Revision 27 has 14 tasks and 22 evidence
records; fidelity remains unvalidated. No DAW, browser, installation, build or
test execution occurred. Only this report was written by this agent.

Next: retain this as protocol review. A future operator must supply the named
session artifacts and classify each observation; follow the
[acceptance review](../ACCEPTANCE_REVIEW.md) before any milestone decision.

## Reviewed source identity

| File | Bytes | SHA-256 |
| --- | ---: | --- |
| `docs/DAW_VALIDATION.md` | 10288 | `18c7362ae9c42f054268efe613b21aed70161ea43c96965b1a13f1a0acfc12af` |
| `docs/specs/HOST_PROTOTYPE_20261123.md` | 9364 | `d22d1b4757c6a37655f40ae5d1b6b2b41d6d033504cf613526e3827b47a1cd46` |
| `docs/agents/ACCEPTANCE_REVIEW.md` | 3960 | `11bbc5feef1017fb99a60ae1967d8c8e775ddf029d981b6af37bed22b75eee85` |
