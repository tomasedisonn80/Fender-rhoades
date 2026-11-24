# W3-59 — Source applicability and retained executable identities

**Agent:** `/root/w3_59_source_applicability`  
**Observed HEAD:** `3175e3dc8c01d1bd57c80d9684168a408cee66a2`  
**Decision:** no material source-attribution defect found within the stated limits.

The compatibility record names capture-time HEAD `2f578915`, when batching was
uncommitted. All **25** recorded source hashes match their later publication in
`9966cdd2`. The final matrix archive's exact three-file production diff also
equals that published commit delta. Therefore the earlier HEAD alone is not the
tested implementation; the explicit hashes and retained diff supply that binding.

I verified the final matrix archive hash and all **298** indexed members, its
equal before/after input maps, and every current file in its **1,148-entry** map.
Of these, **1,143 remain byte-identical**. Differences are `build.ninja`, metadata
test registration in `cmake/Vst3.cmake`, the Clang-only FIR guard, failure-only
wrapper diagnostics, and the later benchmark launcher. Compile commands, retained
objects/libraries and the three benchmark executables still match. No input is
missing. This inspection does not establish build freshness or rerun timings.

The current probe and renderer match the compatibility record, and all **five**
executables below match the native evidence. All **74** native source/test hashes
match both `015767fe` and the current files. The only compatibility-manifest
differences now are the guarded FIR, wrapper diagnostics and offline replay tool.
The guard leaves GCC's compiled path unchanged; exact GCC executable identity
does not itself validate Clang. Native Clang confirmation belongs to the separately
retained platform record. No compiler-defect diagnosis is inferred here.

All **69 artifact references across 21 evidence records** match their registered
digests. The five new records distinguish publication from capture: the matrix
record's `1b2e4fc0` identifies its later verifier, not a benchmark rerun. The contact
record's `--help` command is explicitly documentary; its numerical procedures are
the actual argument vectors inside the retained replay artifacts. The UTF-8 record
and later offline-tool repairs do not retroactively relabel older source revisions.

Required project validation, generated-state checking and work selection passed
at revision 27. Read-only Python `hashlib`/`json`/`tarfile` checks and `git show`/
`git diff` established the comparisons above. No archive extraction, build, test
suite, benchmark, audio replay or reference integration ran. Only this report was
created. Hash consistency does not authenticate acquisition or prove a binary's
construction history. Preserve these source distinctions in the final acceptance
review; RT-001 remains incomplete and physical fidelity remains unvalidated.

## Verified executable hashes

Files are under `build/vst3/`; these are existing local GCC artifacts.

| Executable | SHA-256 |
| --- | --- |
| `rhodes_quadrature_probe` | `8771c48e2a2d47aebdfc3ece6a124ceb2864188bbd7d580b5919122be7dadb48` |
| `rhodes_render` | `74222242fb288d456846e335d7f59ada5ad5bf77963164610243c41ba9cfa2d9` |
| `rhodes_vst3_benchmark` | `0ba93d04add7c19a52d1c727a3ed973114efee254a1caab557edcac604fe86c7` |
| `rhodes_vst3_filtered_benchmark` | `c9135f574e0cde55e8a09e7ed7cb9299373415f4b7845edafeb780ec42eb844a` |
| `rhodes_vst3_quadrature_benchmark` | `f6ffbc20500a173528d911c58997fb0be1569f0fb072dfd66c57fc8716518b31` |

## Examined source and evidence identities

| File | SHA-256 |
| --- | --- |
| `src/research_engine.cpp` | `f0facfcb2c28f989c79407d2e0571a868b07ec7a37511bc05f7a33efcdeb0703` |
| `include/rhodes/research_engine.hpp` | `7dca093866c19f18f4403f86daea96ac485d86b3e6aebc4441477bd5a8a16621` |
| `include/rhodes/quadrature_engine.hpp` | `c625a82b61eb3e721061177654c12ffa6aac0049e026b9633ae53ea2abaa773d` |
| `include/rhodes/quadrature_fir.hpp` | `0ddcf691a5d251979a7adf1ea32d471c8fcf1fd6e5a7a2e203c8fb63974b7b6d` |
| `tests/cpp/quadrature_engine_test.cpp` | `dd69ecb8a5880a72d0f46862086f9ea8b52194289bfbb8a71531354d699760ca` |
| `tools/verify_audio_compatibility.py` | `bf688ea37c6d49a3d388d47c8f47ad996bf60229fd56873ada1fba5a804dd1e4` |
| `tools/run_vst3_matrix.py` | `c91f2ec42bd2934120d63599b4d68c68295a99b2090f8bfc0b358ebe1e9c4159` |
| `project/evidence.json` | `070f3ba7f34869d80a4d89ac50f45927ffc56854ae610fa9e26add10c0ec4ffc` |
| [Compatibility record](../../validation/batched-quadrature-compatibility-20261124.json) | `2ecedc02e1fb11628cfa98017035f0e3e92b7a428e72ed6ec281ce6f1b6b40d4` |
| [Final matrix summary](../../validation/vst3-matrix-batch-final-20261124.json) | `e0c00b865e9a274be69e8d384ba9c31001e9bc27c3958ada00e1c5ffe78b54a9` |
| [Native platform record](../../validation/native-platforms-20261124.json) | `3b28ae2ee831786e1f69c8a340f07eb888e6be23743a155f59a014621dbc5f7c` |
