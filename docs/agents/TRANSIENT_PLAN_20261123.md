# Transient audio and portability development

Authorized continuation on `main`, starting from
`60be432d2353accd753226356476db47be7335fd` on 23 November 2026.
The integrator recovered the existing PHY-002, RT-001 and HOST-001 assignment
leases and renewed them. Scientific acceptance criteria remain unchanged.

## Five requested waves

The requested campaign is 100 actual worker assignments in five waves of 20.
Only three workers can run alongside the integrator, so each wave uses smaller
batches. Planned assignments are not completed work; the final campaign ledger
will identify actual workers and their reports.

1. Diagnose the inherited failures, repair proven portability defects, compare
   independent transient approximations, and freeze implementation contracts.
2. Implement the selected bounded audio path and its separately identified VST3
   integration, maintaining compatibility with the existing two instruments.
3. Verify independent numerical behavior, event timing, state recall, allocation
   policy, and build/host contracts; correct discovered defects.
4. Review stress cases, performance and cross-platform evidence, improving only
   behaviors supported by observed failures or measurements.
5. Integrate the final evidence, audit compatibility and claims, verify published
   commits/CI, and leave actionable continuation instructions.

## Findings at the starting point

The fresh Linux core build passed 133 CTests. Its Python suite ran 376 tests,
with one environment-dependent skip. Hosted logs established Windows C++ name
shadowing, nonportable provenance keys, rooted-path classification, and symlink
fixture assumptions. The four fixes retain existing validation policies.

The preceding full audio study passed all 2,097 discrete-convolution comparisons
but failed 2,085 continuous-output comparisons. Independent review confirmed
the reference's timing and event boundaries. Instantaneous synthetic strikes
cause voltage jumps; point sampling gives those endpoints excessive weight.
The new work compares event-aware quadrature and endpoint corrections, without
changing the old studies or treating their failures as passing evidence.

## Compatibility and evidence boundaries

The direct and filtered plug-in identities, model-1/model-2 saved states and
their rendering semantics remain supported. A different audio approximation
requires its own model and plug-in identity. Candidate performance and numerical
results must be recorded before making claims about them.

No physical specimen or calibrated measurement set has appeared. Parameters
remain synthetic; numerical improvement is not a measured Rhodes fidelity
result. Native platform and real DAW results must be reported separately.

Only the integrator stages, commits, pushes and updates canonical state. Worker
reports live under `docs/agents/transient-campaign/`.
