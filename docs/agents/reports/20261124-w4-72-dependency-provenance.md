# Wave 4 assignment 72 — dependency and provenance audit

## Findings

No material mismatch found between inspected dependency declarations and build/source imports.

- The inspected changes add no third-party C++ runtime library. Default targets link repository libraries; platform/toolchain requirements remain. The optional VST3 path links the public SDK and, on Apple, CoreFoundation. It defaults off, uses a separately supplied checkout, verifies explicit pins, and contains no compiler/package download requirement.
- The local SDK root and four initialized headless submodules match the commits below. Their five license files are byte-identical MIT notices naming Steinberg Media Technologies GmbH, copyright 2026. All three local Linux bundle license/notice pairs exactly match their declared source files. This does not inspect uninitialized GUI, documentation or tutorial submodules.
- Python AST/import and lazy-import inspection found NumPy/SciPy only in offline numerical audit paths; other non-standard-library import roots in the new helpers resolve to repository modules. Installed versions are NumPy 2.3.5 and SciPy 1.17.0. Neither becomes a C++ callback or bundle dependency. Exact audio replay still uses the declared numerical environment without recomputing continuous references.
- The Clang reports identify local Clang 22.1.8 through `ziglang` 0.17.1 as verification tooling. Metadata confirms that package version; build files do not require it. This audit did not rerun compiler experiments or infer native AppleClang results.
- `RIGHTS.md`, `docs/DEPENDENCIES.md`, and `plugins/vst3/THIRD_PARTY_NOTICES.md` consistently separate SDK notices from the still-absent owner-selected project license. No root LICENSE/COPYING file exists. This records provenance, selects no license, and grants no redistribution approval.

## Verification and limits

Required state checks passed: `project.py validate` reported 14 tasks, 22 evidence records and unvalidated fidelity; `sync --check` was current; `next --json` returned revision 27. Inspection used source reads, ASTs, metadata and byte hashes; no package installation, build or DSP suite was needed. No implementation, canonical-state or ledger file was edited. Recheck declarations/notices if dependency scope or packaging changes. Measured fidelity and release authorization remain outside this audit.

## SDK commits and notice hash

| Checkout | Commit |
| --- | --- |
| root | `3cdf9ca5d1f5b1b21e0a86832aa4abe55607bd96` |
| base | `fcf9da0bd27a16f7f03773a3a39822f28f5c8477` |
| cmake | `054c9143cbb8d47fc4694e473f2ee3b4d951a8f5` |
| pluginterfaces | `4f547e8e102b47de4a8b8aaf343c73b700786372` |
| public.sdk | `586dc5e6c8012c3e4b01c79389375cbe96bdb1da` |

Common SDK LICENSE.txt: 1,094 bytes; SHA-256 `e86d79e19e4a33ebc442d2e0d083887dfc75a2a8de9fe17eeddedb84810ad74e`.

## Inspected source SHA-256

| Repository path | SHA-256 |
| --- | --- |
| `CMakeLists.txt` | `2a70fba5924a3eeb01bf0dfd7c2a0f6a00726d27ac7535660a4bf267cf81ff3e` |
| `cmake/Vst3.cmake` | `e779743e238c5b7ac2632d73764eb19974fcef3f4ae5c8699ac6f188fde783a6` |
| `RIGHTS.md` | `b9ceeec2a73487a305be1c0fb579a52ec3b25efd81301983f53d08b3f16562af` |
| `docs/DEPENDENCIES.md` | `2da17d8502587a45f83c900633a0a53f1fcbaf60a901bb589370b61c773b2126` |
| `plugins/vst3/THIRD_PARTY_NOTICES.md` | `171196ff6346500d42093d0aa0729d960944fc607cb83de415a92f1ee1a51570` |
| `src/research_engine.cpp` | `f0facfcb2c28f989c79407d2e0571a868b07ec7a37511bc05f7a33efcdeb0703` |
| `include/rhodes/control_value.hpp` | `d7c282afdcffaef90f7872f97b37bfb34671448da64376d3439649c052d4d48d` |
| `tools/contact_peak_schema.py` | `af5cbf87d155ecb40c176b78a7527b2a7bd686c7fd515be621af93d910db86c4` |
| `tools/contact_peak_oracles.py` | `e691093a11ef34ea68cbbe25614d42a2dbd4f4576535c2c35b289133e8fc2c50` |
| `tools/validate_contact_peaks.py` | `5bfc879d0a4a42d48667cc2370bafc85a2bd6c6b08c18071c1d60ab7b85be717` |
| `tools/verify_audio_compatibility.py` | `bf688ea37c6d49a3d388d47c8f47ad996bf60229fd56873ada1fba5a804dd1e4` |
| `tools/verify_vst3_matrix_archive.py` | `4fc824ebe5038fcda4170415b002dd844c53a0ea167705cdbee03a9f23a32162` |
| `tools/run_vst3_matrix.py` | `c91f2ec42bd2934120d63599b4d68c68295a99b2090f8bfc0b358ebe1e9c4159` |
| `tools/validate_development_waves.py` | `5a93e12b045ddc7ee617de954f4a9cd8af625bca3bcc245e47d929bca4449e25` |
| `tools/validate_contact_reference.py` | `c910da5da6b6ad957b709a414cef39a08ed6e8b75c6e36c9d73ef535a064dcdc` |
| `tools/validate_filtered_audio.py` | `0d6e5336cd571e873b22333e9ed8f98d70c4aa443831309c68ad1084171d3ca1` |
| `tools/validate_quadrature_audio.py` | `e205f68c0ce0afe145cc1e8af4ee44e0303c6e60998955508789ebafc0821be2` |
| `docs/validation/control-clang-fenv-20261124.md` | `739e0cdce002e36b3e4af273a99b7be61fcf436b426308be5ad5116c2b8f3224` |
| `docs/validation/quadrature-clang-contraction-20261124.md` | `b788b6c4d7a8453954cfd7310d83cdfb28714936c975dc51bc458dc21c68266e` |
