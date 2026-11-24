# W3-49 — Allocation and locking policy review

**Agent:** `/root/w3_49_allocation_policy`  
**Scope:** bounded independent RT-001 source/evidence review.  
**Decision:** no material allocation or locking regression found; RT-001 remains incomplete.

## Findings

`observe_cell` uses fixed arrays and an inline optional result, bounded by six
nodes, three modes and 73 keys. Its reviewed source has no container growth,
allocation call, asset access or lock. The wrapper owns its engine, plan and
FIR ring. Preparation publishes complete fixed storage outside processing;
reset retains the plan. Ordinary note/pedal changes select prepared transitions;
arbitrary third decay settings in the separate primitive can still recalculate
coefficients.

The adapter owns fixed parameter/note/dispatch arrays. Preflight copies a
fixed-capacity tracker; rendering uses spans into existing storage. Construction,
SDK bus/parameter creation and state-stream I/O remain outside the guarded
callback. `StateMailbox` requires lock-free 64-bit atomics at compile time;
begin/end/abort each make at most one compare/exchange attempt with no
source-level retry loop. Concurrent lifecycle or direct mechanical mutation
remains unsupported.

The Q6 fixture calibrates ordinary/array replacement `new` with two direct calls,
then guards all-key events, empty calls, reset and rendering at nine rates.
The actual-processor fixture calibrates eight ordinary/array/aligned/nothrow
forms before each selected scenario. Its allocation scenario covers both output
formats, three rates, repeated rendering, rejection, flush and pending recall.
All three variants compile this same fixture with their respective engine.

I verified the retained archive hash and four log-member hashes. The local
integrated log records the core and three adapter allocation tests passing;
Linux, Windows and macOS VST3 logs each record all three adapter guards passing.
The twelve current source identities below match that evidence's source manifest
at `015767fee5635ff85a717739188ff4af60fe84f2`.

## Limits and next action

Finite traces and source inspection do not cover direct `malloc`, other-thread
allocations or arbitrary foreign getters, which may allocate or block. No lock
probe, stack measurement or worst-case/device deadline bound was added.
Lock-free alone supplies no callback completion bound. Preserve these limits in
RT-001 acceptance; repeat relevant guards if ownership/storage changes. Physical
fidelity remains unvalidated.

## Executed checks

```sh
python3 tools/project.py validate
python3 tools/project.py sync --check
python3 tools/project.py next --json
```

All passed at state revision 27 (21 evidence records, fidelity unvalidated).
Additional read-only `rg`/`sed` inspection and Python `hashlib`/`tarfile` checks
verified the identities and log observations above. No build, runtime test,
benchmark or numerical study was rerun. Only this report was created.

## Examined identities

| Source | SHA-256 |
| --- | --- |
| `src/research_engine.cpp` | `f0facfcb2c28f989c79407d2e0571a868b07ec7a37511bc05f7a33efcdeb0703` |
| `include/rhodes/research_engine.hpp` | `7dca093866c19f18f4403f86daea96ac485d86b3e6aebc4441477bd5a8a16621` |
| `include/rhodes/quadrature_engine.hpp` | `c625a82b61eb3e721061177654c12ffa6aac0049e026b9633ae53ea2abaa773d` |
| `include/rhodes/quadrature_fir.hpp` | `0ddcf691a5d251979a7adf1ea32d471c8fcf1fd6e5a7a2e203c8fb63974b7b6d` |
| `include/rhodes/event_processor.hpp` | `36e4b64f3278b0079b20e94a333a657c900e4118dc24433ec9d8c58442651351` |
| `include/rhodes/instrument_state.hpp` | `c11c9d982887f4228c76c3ee9e10fe5627c2dc0f1053ad531cfd316285a65616` |
| `include/rhodes/note_tracker.hpp` | `76931b326daae3a112b56420f9be9ea5b03b1c30307f1dc68b6c13c710cd830f` |
| `plugins/vst3/processor.cpp` | `97b449dac4e1eb434af8ad4e279bdf7ba07f4668cb174199980eea0266493cf6` |
| `plugins/vst3/processor.hpp` | `0250eaaca92b2aa904b5a05e8aca8cc46cd8a63111d145ddf7d0956f682d1807` |
| `tests/cpp/quadrature_engine_test.cpp` | `dd69ecb8a5880a72d0f46862086f9ea8b52194289bfbb8a71531354d699760ca` |
| `tests/cpp/vst3_host_test.cpp` | `ca4a6302eebefee1f59a17118bb29492cc2c534d6d166631475c028bc6127da4` |
| `cmake/Vst3.cmake` | `e779743e238c5b7ac2632d73764eb19974fcef3f4ae5c8699ac6f188fde783a6` |

Retained evidence: [native-platforms-20261124.json](../../validation/native-platforms-20261124.json) and [log archive](../../validation/native-platforms-20261124.tar.gz). The archive and four inspected log hashes are indexed in that JSON; no extraction or archived command execution occurred.
