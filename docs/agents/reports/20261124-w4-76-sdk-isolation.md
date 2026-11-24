# Wave 4 assignment 76: SDK isolation

## Finding

No unintended SDK coupling or misleading Python dependency claim was found in the inspected paths.

The default CMake 3.24/C++20 build leaves `RHODES_BUILD_VST3=OFF`. It builds the core, renderer, native benchmarks, and separate offline contact reference; `BUILD_TESTING` gates native tests and probes. The SDK integration is included only when explicitly enabled; the default build neither locates nor downloads it.

Optional VST3 configuration requires CMake 3.25, Git, the supplied SDK root, three required SDK files, and exact HEAD/content checks for the SDK plus four required submodules. Tracked staged/unstaged changes fail configuration; untracked build directories and unused submodules remain permitted. VSTGUI, plugin examples, hosting examples, automatic installation links, and automatic validator execution are forced off. Metadata generation remains on. The Apple CoreFoundation dependency belongs to the optional SDK base archive.

The three bundles and callback benchmarks exist independently of `BUILD_TESTING`; `rhodes_vst3_checks`, retained host tests, and explicit SDK/moduleinfo CTests require it. That aggregate does not build every core test, so VST3 CI deliberately selects `^vst3\\.`. Complete bundles are archived only after those tests; packaging is a workflow step, not installation.

Repository contracts, coefficient generation, candidate sweep, and default peak comparison use Python's standard library. Peak timing adds optional SciPy; general contact audits, full audio reference studies, and exact audio replay need optional NumPy/SciPy. Neither package is a core/ordinary CTest dependency or bundled VST3 runtime.

## Evidence and limits

At `240170673983f9992e4657a2d96c2172ece1ac30`, independently fetched job records show all seven [Foundation jobs](https://github.com/baabi-boo/Fender-rhoades/actions/runs/45340923223) and three [VST3 jobs](https://github.com/baabi-boo/Fender-rhoades/actions/runs/45340923158) succeeded. Ubuntu logs independently confirm default configuration and 150 core passes, versus explicit SDK configuration, the aggregate target, and 138 VST3 passes. Foundation checks out no SDK; optional CI initializes only the four pinned headless dependencies.

Executed project validate, sync check, and next commands: valid 14 tasks/22 evidence records, generated state current, no available tasks. Documentation integrity passed after report creation. No configure, build, SDK download, or implementation change was performed. Source inspection establishes untested configuration boundaries; these CI results do not establish DAW, distribution, or specimen fidelity.

## Source SHA-256

| Source | SHA-256 |
| --- | --- |
| `CMakeLists.txt` | `2a70fba5924a3eeb01bf0dfd7c2a0f6a00726d27ac7535660a4bf267cf81ff3e` |
| `CMakePresets.json` | `a06e2a16dba0b4947352cbbcec2a7668d29b3f777c6f460f9d65f6bd0bb7f0aa` |
| `cmake/Vst3.cmake` | `e779743e238c5b7ac2632d73764eb19974fcef3f4ae5c8699ac6f188fde783a6` |
| `.github/workflows/ci.yml` | `9a0d441f70dc60be6bdf7ec3b3ab5a3c5e046feed103a043633d1bd27911e94a` |
| `.github/workflows/vst3.yml` | `6cf2593353d05a6b122c99736399132b4c813ec8fba5af03b39ed4d5fdd8e3f3` |
| `docs/DEPENDENCIES.md` | `2da17d8502587a45f83c900633a0a53f1fcbaf60a901bb589370b61c773b2126` |
| `docs/DEVELOPMENT.md` | `7761d929fff1b9f3d5c7f4956c869f9b806dd0793ebc100f44cd8eb71b91b419` |
| `tools/contact_peak_oracles.py` | `e691093a11ef34ea68cbbe25614d42a2dbd4f4576535c2c35b289133e8fc2c50` |
| `tools/validate_contact_reference.py` | `c910da5da6b6ad957b709a414cef39a08ed6e8b75c6e36c9d73ef535a064dcdc` |
| `tools/validate_quadrature_audio.py` | `e205f68c0ce0afe145cc1e8af4ee44e0303c6e60998955508789ebafc0821be2` |
| `tools/verify_audio_compatibility.py` | `bf688ea37c6d49a3d388d47c8f47ad996bf60229fd56873ada1fba5a804dd1e4` |
| `tools/run_contact_sweep.py` | `81b45563f7409b4c6378a7867d7b8d33394676482c14e165f9919c1bc40e627a` |
