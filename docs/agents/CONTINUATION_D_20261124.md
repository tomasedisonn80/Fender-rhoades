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

The [reviewed feature contract](../specs/HOST_EXPRESSION_MONO_20261124.md) was
frozen after twenty first-wave reviews. It adds global MIDI Expression (CC11) and
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
the previously missing macOS Python execution has now completed with 888
reported tests, including 51 skips. These older-source observations still
require a supplemental criterion review before closing HOST-002.
Final platform conclusions require the later source-bound evidence, not this
progress note. No new feature acceptance or physical fidelity is claimed here.

The first integrated HOST-003 run passed 32 new CTests: seven state-codec
selectors, four stream selectors, and seven audio selectors across three
variants. It used the current uncommitted implementation on the published
`cba5eeb9faf106c38efa530e94036dc2db3836b2` base. Broader legacy, mono, concurrent,
loaded-module and native verification remains in progress.

## Worker boundaries

Read the operating contract and frozen feature specification before starting.
Each assignment owns only its named source, tests or report paths. The
integrator alone changes CMake registration, Git/index/branches, the campaign
ledger, canonical task/evidence state and final acceptance decisions. Workers
do not spawn children. Report actual commands and results, including first
failures; do not infer a native, sanitizer, DAW or physical pass from source
inspection. A completed report becomes immutable when its hash is recorded.

The active checkout is `/workspace/scratch/68ec02e60e7e/Fender-rhoades`.
The pinned read-only SDK is `/workspace/scratch/d6370337c6a4/vst3sdk`.
Only the integrator mutates `/tmp/rhodes-d-build`; workers may read its archives
for private focused builds. Temporary worker files belong in a unique `/tmp`
or `/dev/shm` directory. Do not remove files from other workspaces. Current
production code implements the contract; additions should target concrete
unverified behavior rather than repeat passing fixtures.

## Recovery

Read `AGENTS.md`, the live task graph, the feature contract, assignment ledger
and current Git status. Preserve all other contributors' work. Completed worker
reports under `reports/continuation-d/` are immutable once their hashes are
recorded. New observations or corrections receive new report paths. Local
build outputs are outside the repository; all source changes and final
engineering evidence are committed to this repository.
