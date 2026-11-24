# Expression and output-layout development campaign

The user authorized development on `main`, frequent commits/pushes, and 100
actual workers in five waves. This session starts at
`6c91dab00855b67a559e92edb79ae49e567be469`. The integrator is
`continuation-d-root`; it alone owns Git publication and canonical task/evidence
changes. Three scoped workers may run alongside it. Each wave contains twenty
distinct assignments, executed in batches within that limit.

The [assignment ledger](../../project/continuation-d-20261124.json) records
actual spawned workers and hashes of completed reports. A planned assignment
is not a completed worker. The ledger is updated at integration checkpoints;
it does not authenticate execution or replace engineering evidence.

## Scope

The [reviewed feature contract](../specs/HOST_EXPRESSION_MONO_20261124.md) is
currently under first-wave review. It adds global MIDI Expression (CC11) and
optional mono output to all three synthetic instruments while retaining
unchanged unity-expression stereo audio, class identities and latency. An
adaptive state codec preserves legacy bytes at unity and saves non-unity
expression explicitly. Public MIDI Association and Steinberg documentation
support the host design; no physical measurements are inferred from them.

The prior HOST-002 macOS blocker is being checked independently using its frozen
definition. Prior inconclusive observations remain immutable. PHY-002 and
RT-001 retain their separate ownership; this campaign does not replace their
numerical work or promote the project's unvalidated fidelity status.

## Five waves

1. Audit the current code, compatibility, public contracts and independent
   acceptance methods; freeze the scoped development task.
2. Implement expression, state migration and mono negotiation with independent
   regression fixtures and relevant documentation.
3. Exercise integration, malformed transactions, actual loaded modules,
   concurrency, floating-point modes and legacy audio compatibility.
4. Resolve findings, retain native platform results and review source-bound
   acceptance evidence.
5. Independently review final behavior, evidence, limitations and continuation;
   finish the actual-agent record and publish the tested checkpoint.

## Baseline observation

A new local Linux/GCC Release build against the pinned SDK passed 327 of 327
CTests, with no skips. The integrator retained original module binaries for
later comparisons. Native macOS VST3 and core job logs have been obtained;
the complete current macOS Python result is still pending at this checkpoint.
Final platform conclusions require the later source-bound evidence, not this
progress note. No new feature acceptance or physical fidelity is claimed here.

## Recovery

Read `AGENTS.md`, the live task graph, the feature contract, assignment ledger
and current Git status. Preserve all other contributors' work. Completed worker
reports under `reports/continuation-d/` are immutable once their hashes are
recorded. New observations or corrections receive new report paths. Local
build outputs are outside the repository; all source changes and final
engineering evidence are committed to this repository.
