# W4-69 — Documentation command audit

**Agent:** `/root/w4_69_command_audit`  
**Result:** no material parser/target mismatch; one PowerShell clarification repaired.

## Findings and change

The new archive-verifier examples correctly use POSIX backslash continuations,
but README and DEVELOPMENT only advised Windows readers to substitute `python`
for `python3`. With root's explicit scope expansion, I added the instruction to
put each wrapped command on one line in PowerShell. The earlier W4-61/W4-65
reports remain unchanged. Only these two guides and this report were written.

All five SDK pins agree across CMake, workflow, launcher and dependency guidance.
CMake forces the documented headless options and disables installation links.
The `BUILD_TESTING=ON` aggregate includes three modules, three benchmarks, SDK
validator, metadata tool and retained host tests. Core/SDK CMake minimums,
Release configuration, generator distinctions and CTest selectors match source.

The capture example matches the Linux-only affinity protocol, fixed
`build/vst3` path, Release/Ninja freshness and compile-command requirements,
matching clean SDK, existing output parent and exclusive destination. Renderer
and benchmark example options match their native parsers. Nominal misses do not
make an otherwise verified capture fail. Launcher exits 0/1/3 and argument exit
2, archive-verifier exits 0/1, and optional campaign completion are described
consistently with their implementations.

Standard-library generators and evidence tools are distinguished from optional
NumPy/SciPy numerical work and requested damped timing. No dependency was added.
These are source/help checks, not fresh clone, build, benchmark, PowerShell,
native-platform or DAW execution. RT-001/PHY-002 and fidelity remain unchanged.

## Actual checks

Required project validation, generated-state check and work selection passed at
revision 27 with 22 evidence records. The following five help commands returned
0; source inspection, AST import checks and exact SDK-pin comparisons passed:

```sh
python3 tools/run_vst3_matrix.py --help
python3 tools/verify_vst3_matrix_archive.py --help
python3 tools/validate_development_waves.py --help
python3 tools/validate_contact_peaks.py --help
python3 tools/verify_audio_compatibility.py --help
python3 tools/check_docs.py
```

Documentation integrity and direct whitespace checks passed after the edit.
No tests, builds, rendering, benchmark capture, archive extraction, Git operation,
ledger or canonical-state mutation occurred. Next: root can publish the two
clarifications with this report; no implementation repair is required.

## Examined file identities

These are post-review file identities, including the two authorized edits.
They remained unchanged during report creation.

| File | SHA-256 |
| --- | --- |
| `README.md` | `4e2b446c382c9d171a95b90978400ea8e47c30ae66a87ae1470a4a71853b81a8` |
| `docs/VST3.md` | `ca02322d8a5c9dff5a64321bf6c622c6c78f0ada139e8d3cb28844d0476cd11b` |
| `docs/BENCHMARKING.md` | `02439bddb473c9abeb46714ee48c84030f2e06bafe8144e70a2b61214b82a4e8` |
| `docs/DEVELOPMENT.md` | `7761d929fff1b9f3d5c7f4956c869f9b806dd0793ebc100f44cd8eb71b91b419` |
| `docs/DEPENDENCIES.md` | `2da17d8502587a45f83c900633a0a53f1fcbaf60a901bb589370b61c773b2126` |
| `CMakeLists.txt` | `2a70fba5924a3eeb01bf0dfd7c2a0f6a00726d27ac7535660a4bf267cf81ff3e` |
| `CMakePresets.json` | `a06e2a16dba0b4947352cbbcec2a7668d29b3f777c6f460f9d65f6bd0bb7f0aa` |
| `cmake/Vst3.cmake` | `e779743e238c5b7ac2632d73764eb19974fcef3f4ae5c8699ac6f188fde783a6` |
| `.github/workflows/ci.yml` | `9a0d441f70dc60be6bdf7ec3b3ab5a3c5e046feed103a043633d1bd27911e94a` |
| `.github/workflows/vst3.yml` | `6cf2593353d05a6b122c99736399132b4c813ec8fba5af03b39ed4d5fdd8e3f3` |
| `tools/run_vst3_matrix.py` | `c91f2ec42bd2934120d63599b4d68c68295a99b2090f8bfc0b358ebe1e9c4159` |
| `tools/verify_vst3_matrix_archive.py` | `4fc824ebe5038fcda4170415b002dd844c53a0ea167705cdbee03a9f23a32162` |
| `tools/validate_development_waves.py` | `5a93e12b045ddc7ee617de954f4a9cd8af625bca3bcc245e47d929bca4449e25` |
| `tools/validate_contact_peaks.py` | `5bfc879d0a4a42d48667cc2370bafc85a2bd6c6b08c18071c1d60ab7b85be717` |
| `tools/verify_audio_compatibility.py` | `bf688ea37c6d49a3d388d47c8f47ad996bf60229fd56873ada1fba5a804dd1e4` |
| `tools/validate_filtered_audio.py` | `0d6e5336cd571e873b22333e9ed8f98d70c4aa443831309c68ad1084171d3ca1` |
| `tools/validate_quadrature_audio.py` | `e205f68c0ce0afe145cc1e8af4ee44e0303c6e60998955508789ebafc0821be2` |
| `tools/generate_audio_filter.py` | `ab97a1cc51f75481efb7b3c76a28b07196a1935109a81ab5ff77040cd34bfcee` |
| `tools/generate_quadrature_filter.py` | `58c9f9148334c8c8111b7c9b07773597328d90a39e8ef2ddf595be8417e7fe24` |
| `apps/render.cpp` | `4fe901544c9faa0992f4517ad1958cc2969d6d4b5d0426b101b4fe52aa265fe2` |
| `apps/runtime_benchmark.cpp` | `514955b4245ba5ec6c18fb2379cb790185e029df1d4c387588d2547621c476c0` |
| `apps/vst3_benchmark.cpp` | `2129c52bda0337b4cfa71fc39eab31e66696883977dfbfc3c8d52e69ebccc2ed` |
| `apps/benchmark.cpp` | `f05cefbe22b93274bc51f6b3fedad532d333b8c577196059c04e0559530e5705` |
