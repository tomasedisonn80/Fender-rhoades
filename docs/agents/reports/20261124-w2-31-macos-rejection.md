# Wave 2 assignment 31 — macOS rejected-prepare diagnosis

Date: 2026-11-24. Worker: `/root/w2_contact_implementation/w2_31_macos_rejection`.
Parent: `/root/w2_contact_implementation`. Integration owner: `/root`.

## Scope and result

Bounded source review of Foundation job `138240862696`'s reported
`quadrature.qengine_prepare` failure, with implementation and original fixture
pinned to commit `9966cdd2b63dcebe1f4e3865d9e74ea37f081b3a`.
The job failure and passing analytic, scalar/batch, and partition checks are
assignment-provided observations; this worker did not retrieve or rerun that job.

**No source-level rejected-prepare mutation or fixture ownership mismatch was
found. The native failure's cause remains unresolved.** All nine invalid fixture
rates return before the wrapper constructs a candidate or writes any member.
The diagnostic result reported just before handoff narrows the failure to a
one-ULP output difference with equal final mechanics. A no-rejection control at
the same rendering call sites remains a bounded fallback discriminator. Exact
equality should remain the acceptance condition.

Only this report was written. No C++, CMake, workflow, canonical-state, or Git
mutation was performed by this worker; no child agents were spawned.

## Version boundary and evidence provenance

The worktree initially reported HEAD `9966cdd2b63dcebe1f4e3865d9e74ea37f081b3a`
with other contributors' changes present. During inspection the integrator
advanced HEAD to `cfd8ef452c2a053bf41e636668498d13ccf37dc0`. All code conclusions
below use `git show 9966cdd:<path>`. The diagnostic test diff against that commit
was also read; it splits the render result from equality, prints the rejected
rate and first mismatching sample in hexadecimal floating-point format, and
checks final mechanics before retaining the original exact-equality assertion.
The inspected production headers, implementation, CMake, and Foundation workflow
still matched the pinned commit when their hashes were recorded.

The integrator separately reported that optimized Clang 22.1.8 with
`-mavx2 -mfma` passes this fixture both with default contraction and with
`-ffp-contract=fast`, and that the binary contains 72 FMA instructions. This
worker did not reproduce those checks. That evidence weakens a diagnosis based
only on FMA availability; it does not establish that the two macOS FIR call
sites have identical generated reductions.

Immediately before handoff, the integrator reported native reproduction at
diagnostic commit `cfd8ef452c2a053bf41e636668498d13ccf37dc0`, Foundation job
`138242617361`: rate `0x1.f4p+14` (32,000 Hz), rejected positive zero, first
mismatch frame 1, actual `-0x1.7ce971fead695p-8`, expected
`-0x1.7ce971fead694p-8`. The inserted final `same_physics` comparison passed.
Thus this observed failure is a one-ULP output-equality difference, not a false
render result or surviving final mechanical-state divergence. Because the first
mismatch is frame 1, the frame-zero discriminator below does not apply to this
observation. The integrator plans a contraction-off pragma scoped to FIR output
and requires native CI before accepting it; this report does not claim that
experiment has run or that contraction has been proven causal.

SHA-256 of pinned source contents:

| Path | SHA-256 |
| --- | --- |
| `tests/cpp/quadrature_engine_test.cpp` | `006943167dd17d58093d3161b0f4b009b56ab8329377221b8470a97fa7d6702c` |
| `include/rhodes/quadrature_engine.hpp` | `c625a82b61eb3e721061177654c12ffa6aac0049e026b9633ae53ea2abaa773d` |
| `include/rhodes/quadrature_fir.hpp` | `30c9b3e913f6d31020a67105db23464ffa2865c02c269fb1eb0ccc739d3bd1dc` |
| `include/rhodes/research_engine.hpp` | `7dca093866c19f18f4403f86daea96ac485d86b3e6aebc4441477bd5a8a16621` |
| `src/research_engine.cpp` | `f0facfcb2c28f989c79407d2e0571a868b07ec7a37511bc05f7a33efcdeb0703` |
| `CMakeLists.txt` | `2a70fba5924a3eeb01bf0dfd7c2a0f6a00726d27ac7535660a4bf267cf81ff3e` |
| `.github/workflows/ci.yml` | `a1b203c3d6df6b56fbf7cbe3759865e7ffaff10a0a22d256235d40e3ba461a3e` |

## Concrete source findings

1. **Invalid-rate rejection has no member-writing path.** The original fixture
   at `tests/cpp/quadrature_engine_test.cpp:329–355` passes zero, negative zero,
   a negative rate, both infinities, quiet NaN, values immediately outside the
   supported endpoints, and 1,000,000 Hz. The first guard in
   `BasicQuadratureEngine::prepare` rejects all of these before `Engine candidate`,
   plan preparation, assignment, FIR reset, or rate publication. This review
   found no special platform branch in that path.

2. **The fixture clones the intended boundary state.** Both engines continue
   from the baseline after 117 warm frames. `expected_engine` is cloned before
   the baseline's empty `process({})`; each `actual` is cloned afterward. The
   empty wrapper call initializes only local temporaries and executes zero loop
   iterations, so it does not touch the engine, observation plan, or FIR. The
   original fixture's `expected_engine` advances separately; the baseline does
   not accidentally advance along with it.

3. **There is no source-level shared plan/history ownership.** The wrapper,
   `FractionalObservationPlan`, `QuadratureFir`, and `ResearchEngine` store
   arrays and scalar values. Their implicit copies do not retain pointers to
   another engine's state. Observation preparation fills its transition arrays
   before publication. FIR history is initialized, and 117 warm frames exceed
   its 96-frame history. No uninitialized active history cell was identified.

4. **The reported assertion combines two failure classes.** At the pinned
   commit it checks `actual->process(output) && output == expected`. The message
   therefore does not by itself establish sample drift: it can also mean an
   observation, endpoint, or filter arithmetic failure. The integrator's new
   `processed` split is useful. If that exact assertion fails without its
   mismatch line in the diagnostic build, first inspect the false-render path.

5. **Frame zero sharply narrows a sample mismatch.** Wrapper processing obtains
   `filter_.output()` before observing, advancing, or pushing the current cell.
   Continuation output zero therefore uses only the inherited FIR history. A
   frame-zero mismatch cannot be caused by this continuation's newly calculated
   observations or modal advances; it directs attention to preserved history,
   its copy, or the FIR reduction at the two call sites. Later first mismatches
   do not isolate those categories as cleanly.

6. **Mechanical equality has a useful but limited boundary.** `same_physics`
   compares energy plus every modal displacement and velocity. Equal final
   mechanics would exclude a surviving mechanical-state divergence at the
   tested boundary, while leaving FIR history and prepared observations
   uninspected. It does not directly compare held/sustain controls. With sustain
   true and no further events, some held-state changes would be masked, so it
   should not be described as a complete control-state inspection. The early
   return is the stronger source evidence for preservation here.

7. **No local floating-environment state setter was found.** The bounded paths
   contain no rounding-mode, flush-to-zero, or floating-control-register changes.
   Their only test-specific thread-local state is the allocation probe, disabled
   for this fixture. Each CTest entry invokes a separate executable process, so
   other named CTest fixtures do not run earlier in this process. If rejection
   at positive zero is already the first failing case, NaN-specific exception
   behavior cannot explain that first observation.

## Bounded next steps for the integrator

The integrator has now obtained the native diagnostic above and owns the next
targeted native check. The following controls are fallback options if that check
does not resolve the failure; no additional broad matrix is proposed.

- Record `processed`, rate, rejected value, first mismatch frame, hexadecimal
  values, and whether the final mechanical comparison passes. Preserve the
  native compiler/architecture/build-command identity with the failing binary.
- If sample drift remains, temporarily render two baseline clones at the same
  two continuation call sites while omitting the invalid `prepare` call. A
  failure in that control establishes that rejected preparation is unnecessary
  to trigger the discrepancy. A pass is evidence to investigate the effect of
  that call/context, not proof of an actual member mutation.
- If the control implicates rendering context, route both continuations through
  one test-only non-inlined helper and repeat just this fixture on the same
  native toolchain. A changed result localizes sensitivity to compilation or
  call-site context; inspect the two original FIR reductions before claiming
  contraction is responsible. This experiment need not alter production DSP or
  introduce tolerances.
- If rendering returns false, localize the existing failure branches instead:
  FIR output, cell observation, core endpoint processing, or FIR push. The
  mismatch-only diagnostic cannot classify those branches.

These are proposed discriminating experiments, not checks performed by this
worker. A passing partition or scalar/batch observation test does not substitute
for the omitted-prepare paired-clone control: those tests exercise other call
contexts and different boundaries.

## Actual review and limitations

Read `AGENTS.md` in full; read bounded current sections of `README.md`,
`docs/PROJECT_STATE.md`, `docs/ROADMAP.md`, and canonical state metadata. Used
`rg --files`, targeted `rg -n`, `git status --short`, `git rev-parse`, `git show`,
`git diff 9966cdd -- tests/cpp/quadrature_engine_test.cpp`, and read-only Python
hashing of the pinned files. Inspected the original fixture, FIR, wrapper,
research-engine storage/prepare/observation/control/process paths, coefficient
history metadata, and relevant CMake/Foundation workflow sections.

No compiler invocation, build, CTest run, native-job retrieval, sanitizer run,
assembly inspection, remote modification, task claim, evidence registration,
staging, commit, or push was performed. The bounded assignment did not select a
new canonical task; task validation and publication remain with the integrator.
Findings and proposed controls were sent directly to `/root` and copied to the
parent before this report was finalized.
