# Experimental VST3 acceptance — 2026-11-23

Evaluator: `development-root`, with independent criterion review by
[A094](../agents/development-20261123/W5-A094.md). **HOST-001 passes within its declared synthetic
Linux prototype scope.** This decision covers software behavior, the optional
module build and the available SDK validator. It does not complete PHY-002,
RT-001, specimen identification, a native DAW qualification or a release.

## Frozen definition and tested source

The [frozen definition](development-task-definitions-20261123.json) retains the
exact four criteria below. Definition SHA-256:
`b5e43a5c4bfccd246d49bf60783b320a1629741489023757d01cb50207e48336`.
Dependency `PHY-001` is complete. Required evidence kinds are `numerical` and
`documentary`. Target: **One documented 73-key Mark I Stage specimen at its
specified loaded electrical output; identity and measurements pending.**

The [final integration record](development-final-validation-20261123.json) binds
103 project inputs to source commit
`3dceefcce823bd9a9d331739b76446c8d1d39d4f`, tree
`236e9fc097dfb94f82b639863b2791d1ac1604c6`. Every recorded file matches its Git
object and remained unchanged through the test run. All five SDK checkout pins
and 1,005 tracked SDK file hashes remained unchanged. The artifact includes the
complete observed commands, build flags, test logs, JUnit XML, package hashes
and collection procedure. Artifact SHA-256:
`69fca59fc991cd8a722ecab562c4d6eae3ab251a2b363a0c9db59f530a0a200d`.

Canonical task/evidence/campaign records are explicitly excluded from the
immutable tested-source claim. Their later integration decisions and generated
dashboards are metadata changes, not a claim that those decisions existed in
the source checkpoint. No final-report hash is circularly included in its own
source identity.

## Criterion-by-criterion decision

| Criterion and exact text | Evidence and observed coverage | Remaining limitation | Decision |
|---|---|---|---|
| **HOST-001/C01:** Bounded sample-offset event processing preserves producer order and physical key state; invalid blocks reject without mutation. | A015's independent scalar/partition tests and fault-injection adapter checks; A063 buffer rejection; A073 independent automation model; A074 independent note-ownership model; the final 138-case suite. The physical API bounds frames/events, preserves explicit producer order, and preflights whole blocks. The host preserves list order and its declared parameter-before-note ties; rejected blocks retain mechanics, ownership and configuration. | EventProcessor fault injection uses a compile-time substitute engine. Tests cover specified cases, not every possible foreign host implementation. Rejection silence requires valid accessible host storage. | Pass within the declared prototype domain |
| **HOST-001/C02:** An optional VST3 instrument builds against an explicitly identified public SDK and passes the available SDK validator with bounded audio processing and documented output scaling. | The pinned public VST 3.8.1 SDK builds the actual module; final SDK validator passes 47/47. A071 exercises pin/content rejection, A086 verifies SDK-OFF isolation, A089 verifies testing-OFF packaging, A091 checks target/CI coverage, and A092 audits accepted-domain bounds and calibrated allocation interception. Gain conversion is explicitly `32 * normalized_gain * synthetic_harp_volts`. | Accepted-domain bounds exclude arbitrary malformed dimensions and foreign getter behavior. Rejected oversized output clearing scales with host-declared storage; this exclusion already existed in the host contract at the frozen-definition source. No universal callback-time or device guarantee. | Pass within the predeclared accepted host domain |
| **HOST-001/C03:** Host configuration round-trips through a versioned validated state format; MIDI range, sustain, overlapping notes, reset and unsupported host conditions are explicit and tested. | A053/A055 cover control and wire behavior under host floating-point modes; A066 covers fragmented/failed streams and transactional recall; A073/A074 cover automation and independent ownership models; A081 covers concurrent actual-processor state callbacks. The final suite includes all retained selectors. The sixteen-byte RHST v1/model1 codec validates version, length, gain and sustain before recall. | Recall restores configuration and resets notes/mechanics; it is not a ringing-state checkpoint. Direct concurrent lifecycle/engine mutation is unsupported. CC64 is mapped; pitch bend, note expression, half pedal and CC120/123 mappings are not advertised. | Pass within the documented configuration/MIDI scope |
| **HOST-001/C04:** Document the actual tested platform and synthetic sound boundary without claiming measured fidelity or an untested release. | README, VST3/DEVELOPMENT/DEPENDENCIES, host contract, runtime specification and the final integration artifact explicitly name Linux/GCC, synthetic impulse/switched-loss/polynomial/resistive synthesis, optional SDK provenance and open physical/runtime gates. Public research attribution is link-only and does not transfer published measurements to this implementation. | No specimen acquisition, calibrated output, measured fidelity, native Windows/macOS or actual DAW/device result. No custom editor, installer, signing, owner-selected project license or released product. | Pass within the documentary criterion |

The criterion references above are immutable reports in the
[development campaign](../agents/DEVELOPMENT_CAMPAIGN.md). They retain their
original observed revisions and limitations. The final source-bound execution
rechecks their retained CTest behavior in one integrated build; it does not
rerun their separate timing, sanitizer or full contact experiments.

## Actual integrated checks

Environment: Linux x86_64, GCC 14.2.0, CMake 4.4.5, Python 3.12.14, Release
`-O3 -DNDEBUG`, with project warnings treated as errors. Commands recorded by the
independent final verifier include:

```sh
cmake -S . -B build/vst3 -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON \
  -DRHODES_BUILD_VST3=ON -DRHODES_VST3_SDK_ROOT=/workspace/scratch/af088a9f3cc9/vst3sdk \
  -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
cmake --build build/vst3 --config Release --parallel 2
ctest --test-dir build/vst3 -C Release --output-on-failure --no-tests=error \
  --output-junit /workspace/scratch/af088a9f3cc9/Fender-rhoades/build/a093/ctest-results.xml
python3 -S -m unittest discover -s tests/python -v
```

The artifact's argument arrays are the exact execution record; the SDK path is
the separate local checkout used for this run. **138/138 CTest cases passed**,
comprising 99 default and 39 optional host cases, with zero skips. The SDK
validator's **47/47** checks are inside one of those CTest cases. **275/275 Python
tests passed**, with zero skips. Python `-S` made NumPy/SciPy unavailable; genuine
backend numerical self-checks are separate A076/A083 observations, not implied
by this standard-library test run.

The collection driver initially rejected the SDK-generated metadata's trailing
commas while serializing the report. The artifact preserves the exception and
recovery, original logs and exact metadata text/hash. No build, test, source or
bundle was changed or rerun to repair that report-collection issue.

## Bounds, binary and supporting checks

Accepted host calls permit at most 8,192 frames, two queues with 8,192 raw points
each, 256 raw notes and 256 combined physical dispatches at one sample. A092
derives at most 65,536 ownership inspections and 8,448 total physical note/pedal
dispatches per accepted block. Mailbox operations make at most two CAS attempts
per process call with no retry loop. A092's current-source allocation guard
passes eight counter calibration paths and observes zero C++ allocation
attempts in 19 actual process calls. These are bounded plugin-owned observations,
not proofs about host getters, arbitrary `malloc` or every library operation.

The built Linux module SHA-256 is
`0c02ac1e0203680714387d957755904feacd4fbf999e03dcfd9c54148fc1a38e`;
metadata SHA-256 is
`992e019340507a84f7ba9c2f8928fcdaf466f917fc2fbff90912bfe7e1337d17`.
Both match A089's fresh testing-disabled build. The bundle also contains the
exact SDK license and project third-party notice. Symbol visibility retains
the three required platform/factory entry points without exporting Rhodes
implementation symbols. No binary distribution or minimum operating-system
support promise follows from these hashes.

A049's mailbox and A081's actual-processor ThreadSanitizer runs passed their
executed cases. A081 instruments project sources but not the prebuilt SDK
archives. A059's targeted ASAN/UBSAN result has leak detection disabled after its
reported environment failure. These separate observations do not turn this
Release run into a sanitizer run or establish leak freedom.

## Integration decision

All four frozen HOST-001 clauses pass in their documented engineering-prototype
scope. Register this criterion review as documentary evidence and the final
source-bound run as numerical evidence, then complete HOST-001 with both records
and its valid integrator claim. The evidence registry checks metadata and hashes;
it does not independently certify scientific relevance or execute commands.

Keep fidelity `unvalidated`, SPEC-001/DATA-001 blocked, and PHY-002/RT-001 open.
Their [separate gate review](research-gates-20261123.md) preserves failed contact
refinements, missing extrema, incomplete reference-error/antialiasing budgets and
uncontrolled timing conditions. Native platform/DAW qualification and product
release work require new evidence.
