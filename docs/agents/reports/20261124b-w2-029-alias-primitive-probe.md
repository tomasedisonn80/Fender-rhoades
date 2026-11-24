# W2-029 — Bounded stationary primitive producer

2026-11-24; actual worker 29/100, wave 2; bounded RT-001 assignment from the
integrator. **Implemented the four-case offline C++ producer and passed focused
Linux/GCC smoke checks.** This is producer evidence, not a completed stationary
alias experiment, whole-engine convergence result, or C03 promotion.

## Scope and observed starting state

Read `AGENTS.md`, `README.md`, `docs/PROJECT_STATE.md`, `docs/ROADMAP.md`,
`project/state.json`, the public primitive declarations/implementation, the
existing audio probe, and the [worker 025 design](20261124b-w2-025-alias-diagnostic-design.md).
The required commands actually run from the repository root were:

```sh
python3 tools/project.py validate
python3 tools/project.py sync --check
python3 tools/project.py next --json
```

They returned 0: 14 tasks, 26 evidence records, fidelity `unvalidated`; generated
state current; revision 32, no unclaimed available work. This worker acted under
the integrator's existing assignment and made no state, evidence-registration,
CMake, core DSP, Git, dependency, or shared-build changes.

Changed paths are exclusively `apps/alias_primitive_probe.cpp` and this report.
Private build/capture files are under
`/workspace/scratch/cc294adc3e2f/w2-029-alias-smoke/`; they are local smoke
artifacts, not the future durable diagnostic archive.

## Fixed CLI and fixtures

The exact accepted forms are:

```sh
rhodes_alias_primitive_probe --case ID
rhodes_alias_primitive_probe --help
```

There is no stdin protocol, output-path argument, physical-parameter argument,
or default case. Repeated flags, combinations with `--help`, unknown flags,
trailing arguments, unsupported IDs, and IDs longer than 64 bytes reject with
exit 2 and a stderr diagnostic. `--help` alone returns human-readable text;
`--case` success returns one JSON document on stdout and exit 0.

The IDs and schema below were explicitly agreed with worker 028 before smoke
verification. Modes are ordered as listed; unused storage is never emitted.

| Case ID | Sample rate (Hz) | Frames | Frequencies (Hz) | Amplitudes (m) | Phases (rad) |
| --- | ---: | ---: | --- | --- | --- |
| `one_mode_48000` | 48000 | 1024 | 13500 | 0.0001 | pi/7 |
| `one_mode_96000` | 96000 | 2048 | 13500 | 0.0001 | pi/7 |
| `two_modes_48000` | 48000 | 1024 | 7500, 13500 | 0.0001, 0.00006 | pi/7, -pi/5 |
| `two_modes_96000` | 96000 | 2048 | 7500, 13500 | 0.0001, 0.00006 | pi/7, -pi/5 |

Every mode has mass 0.001 kg and decay 0 per second. Pickup coefficients are
`a1=0.4` Wb-turn/m, `a2=800` Wb-turn/m², and `a3=400000` Wb-turn/m³. All
constants are synthetic development fixtures, not specimen measurements.

For each mode, public `ModalOscillator::configure` and `set_state` initialize
`q=A*cos(phi)` and `v=-2*pi*f*A*sin(phi)`. At each index `n`, public `state()`
is observed at `t=n/Fs`; local modal q and v are summed in fixture order before
one public `PolynomialPickup::voltage(q,v)` call. Public `advance()` occurs
only between observations; there is no unobserved final transition. The producer
does not instantiate an engine, harp, filter, gain stage, strike, or event path.

## Frozen wire contract: version 1

All numeric output uses the classic locale and
`std::numeric_limits<double>::max_digits10` (17 significant digits on the
observed binary64 implementation). JSON keys and string values come only from
fixed source strings. No NaN or infinity is emitted: configuration values,
initial states, observed modal states, sums after each addition, and pickup
voltages receive explicit finite checks, and every fallible primitive result is
checked.

| Top-level key | Type and exact meaning |
| --- | --- |
| `schema_version` | Integer `1` |
| `schema` | String `rhodes.alias_primitive_probe.v1` |
| `provenance` | String `synthetic_development_fixture` |
| `case_id` | One of the four exact IDs above |
| `settings` | Object described below |
| `modes` | Array of one or two mode objects, in summation order |
| `frames` | Array of exactly `settings.frame_count` frame objects |
| `status` | String `ok`, meaning producer computation completed |
| `success` | Boolean `true`, meaning producer computation completed |

| `settings` key | Value and units |
| --- | --- |
| `sample_rate_hz` | 48000 or 96000 |
| `frame_count` | Integer 1024 or 2048 |
| `modal_mass_kg` | 0.001, common to each configured mode |
| `decay_rate_per_second` | 0, common to each configured mode |
| `observation` | String `before_advance` |
| `boundary` | String `single_pickup_voltage` |
| `pickup_coefficients` | Object with exactly `a1_wb_turn_per_m`, `a2_wb_turn_per_m2`, `a3_wb_turn_per_m3`, holding 0.4, 800, 400000 |

Each mode object has exactly numeric `frequency_hz`, `amplitude_m`, and
`phase_rad` keys. Phases are serialized from the actual double constants used to
initialize the public primitive.

| Frame key | Type and meaning |
| --- | --- |
| `index` | Integer, sequentially 0 through frame_count minus 1 |
| `modal_q_m` | Array of current modal displacement doubles, one per mode |
| `modal_v_m_per_s` | Array of current modal velocity doubles, same mode order |
| `q_m` | Sum of `modal_q_m`, in metres |
| `v_m_per_s` | Sum of `modal_v_m_per_s`, in metres per second |
| `P_volts` | One pickup voltage from the summed motion, in volts |

The complete case is rendered into bounded 2048-frame/two-mode storage before
serialization. The entire document is then serialized and checked against a
1 MiB output limit before stdout is touched. Preflight, computation, and
serialization failures therefore leave stdout empty. The output writer checks
both write state and explicit flush. Where `SIGPIPE` exists, it is ignored so
a closed pipe produces the ordinary exit-2/stderr write failure.

A failed write can leave partial bytes, or bytes whose delivery cannot be
confirmed. A driver must retain those bytes, require exit 0, parse a complete
document, and validate dimensions/metadata before treating the producer as
successful. `status`/`success` are not alias-error acceptance results and never
override a failing process return code. No failure JSON is appended to a partial
document.

## Actual focused verification

Compiled directly in a private directory; shared `build/vst3` was untouched.
Compiler: `/usr/bin/x86_64-linux-gnu-g++-14`, Debian GCC 14.2.0-19. The actual
compiler invocation, launched with Python `subprocess.run`, was:

```sh
/usr/bin/x86_64-linux-gnu-g++-14 -std=c++20 -O2 \
  -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Werror \
  -I include apps/alias_primitive_probe.cpp src/research_engine.cpp \
  -o /workspace/scratch/cc294adc3e2f/w2-029-alias-smoke/rhodes_alias_primitive_probe
```

It returned 0 with empty stdout/stderr. An inline `python3 - <<'PY'` smoke
harness then invoked each of the four exact `--case ID` commands once with
captured stdout/stderr. Each child had a 30-second wall timeout, 30-second CPU
limit, and 256 MiB address-space limit; one child ran at a time. All four exited
0 with empty stderr. Captured documents were parsed and checked for exact
top-level/settings/frame keys, expected metadata, expected row/column counts,
sequential frame indices, finite numbers, exact serialized modal sums, and
the pickup derivative of that sum.

The smoke additionally compared every emitted modal state against elementary
Python `math.cos`/`math.sin` motion at `2*pi*f*n/Fs+phase`, and compared P with
`-(0.4+x*(1600+1200000*x))*v` evaluated on that analytic motion. No fitting,
gain correction, or time alignment was used. This elementary smoke is distinct
from worker 028's independent signed-frequency/coalescence oracle.

| Case | JSON bytes | Peak P minus direct analytic (V) | RMS P minus direct analytic (V) |
| --- | ---: | ---: | ---: |
| `one_mode_48000` | 186097 | 1.2029821583325884e-12 | 1.912019346995204e-13 |
| `one_mode_96000` | 372498 | 1.5204504322241519e-12 | 2.1957553949775122e-13 |
| `two_modes_48000` | 230608 | 8.921752225887758e-13 | 1.447285116684419e-13 |
| `two_modes_96000` | 461694 | 1.0053069487980792e-12 | 1.4744977610999506e-13 |

All passed the design's proposed primitive smoke limits of 1e-10 V peak and
1e-11 V RMS. These checks do not establish the alias amplitude or an alias
acceptability threshold.

The same inline harness ran `--help` successfully and 16 invalid forms. The
invalid argv lists were: empty; `--case`; unsupported ID; empty ID; a 65-byte ID;
duplicate same-case flags; duplicate different-case flags; duplicate help;
help followed by a case; case followed by help; `--sample-rate 48000`; case plus
`--output unused.json`; `--case=one_mode_48000`; bare case ID; case plus a trailing
positional argument; and `--unknown one_mode_48000`. Every invalid form exited
2, emitted a stderr diagnostic, and emitted zero stdout bytes.

Three write-failure checks also passed: `--case one_mode_48000` with stdout
opened on `/dev/full`; `--help` with stdout on `/dev/full`; and
`--case two_modes_96000` with stdout connected to an already closed pipe.
Each returned 2 and stderr contained `Cannot write stdout`. Fault checks are
separate from the four successfully captured cases.

Private artifacts include `build.json`, `smoke.json`, each `CASE_ID.json`, each
empty `CASE_ID.stderr`, `help.stdout`, and the binary. At completion they totaled
1,303,197 bytes, below the 16 MiB smoke artifact cap. Recorded smoke wall time
was 0.107161501 seconds; this is execution bookkeeping, not a benchmark or
realtime claim. No broad pre-existing test suite, shared CMake build, native
Windows/macOS run, independent FFT experiment, or durable regression test ran
as part of this assignment.

## SHA-256 binding

The following source/design/compiler/binary hashes were captured before the
smoke and checked unchanged afterward:

| Input | SHA-256 |
| --- | --- |
| `apps/alias_primitive_probe.cpp` | `e1cba5cd3b31e07954afae6b6740ab6fd22914becea50eede959506c2a41c808` |
| `src/research_engine.cpp` | `01c2a9f7f9f7cfa4f759be56770178f32a71a28667d4ac6ab390e03ff7a95814` |
| `include/rhodes/research_engine.hpp` | `2a6ba0483a010d9b7a44517c5ed7c215f05d57ed6e96bd6609127322092929e4` |
| `include/rhodes/control_value.hpp` | `d7c282afdcffaef90f7872f97b37bfb34671448da64376d3439649c052d4d48d` |
| Worker 025 design report | `fdab006f37b3aa0bda66f9277eb37fddc2c9216ef86aa0db8d2fe9b3050127de` |
| Resolved GCC executable | `6b3696e4dcb85e1c949c732a02befa50e3983ecf94ce7e8e58d9d503b954b79d` |
| Private probe executable | `5d4f606d2bc71fd6ec7030250d7e84bf8618b5763fe7e526234393f1340dcb73` |

| Successful stdout artifact | SHA-256 |
| --- | --- |
| `one_mode_48000.json` | `0704e9c91153d51c593bdd60cd04b12dcca95b864d7c39b6449dbf7a48e0b80d` |
| `one_mode_96000.json` | `441851a02ce576cd7a004649666cdd36a774caeda62a4c7f19d4f7623dae8c4d` |
| `two_modes_48000.json` | `6e4fa6f1441d6626eb3b944ba62c9d426412e5168e916d5ffa13eb8c43e45fba` |
| `two_modes_96000.json` | `1ef99fb45081eff80cfad3f7ecfb510b6f1876abfcef2d7d919a5ab1d3bfa8f8` |

## Integration handoff and remaining work

The integrator owns registering executable target `rhodes_alias_primitive_probe`
from `apps/alias_primitive_probe.cpp`, linking `rhodes_core`, and applying
`rhodes_target_options`. Worker 028 has received the exact IDs, schema, source
hash, binary hash, and private capture location. Its oracle fixture fields use
`sample_count`, `mass_kg`, and `decay_per_s`; the future driver must explicitly
map those to producer `frame_count`, `modal_mass_kg`, and
`decay_rate_per_second`, while independently checking all frozen constants.

Next separate assignments: durable CLI/producer regression tests; a bounded
exclusive-output driver that preserves raw bytes and failures, writes the frozen
plan before execution, records hashes before/after, and compares primitive P
against independent S/B arrays and phase-coalesced lines. Stationary aliases,+damping, transients, finite-window/kernel effects, audibility, physical fidelity,
whole-engine direct convergence, C03, and realtime deadlines retain their
separate uncompleted scope.
