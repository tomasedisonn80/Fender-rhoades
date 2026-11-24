# W4-71 — Floating-point build guidance boundary

Date: 2026-11-24. Agent: `/root/w4_71_fp_build_boundary`.

**Result: no material misleading statement found.** The inspected DEVELOPMENT and VST3 guides accurately bound the floating-point repairs and the native macOS evidence.

The macOS workflow explicitly selects Unix Makefiles and requests neither `-ffast-math` nor `-ffp-contract=fast`. Project presets introduce no floating-point override. The pinned SDK's Xcode branch adds both flags, so the guides correctly require separate verification when changing generators or external flags. The project target helper supplies MSVC `/fp:precise`; this is not a claim about all SDK targets, whose Windows setup also contains `/fp:fast`.

The control header places its Clang-guarded `float_control` push after includes, surrounds only the helper definitions, and restores the previous settings with pop. The FIR's Clang-guarded `contract(off)` begins the `output()` compound statement. Neither repair requires globally disabling contraction or enabling strict arithmetic. GCC and ordinary MSVC bypass these guards; the guides do not claim every Clang-family toolchain was tested.

Official Clang documentation supports these placement and scope rules. It also says `-ffp-contract=fast` disregards contraction pragmas, while `-ffast-math` enables that setting and additional transformations. Option combinations and ordering still matter. The guides correctly avoid promising that the local pragmas preserve tested behavior under arbitrary user flags; exact control/state/continuation assertions remain the appropriate gate for changed configurations.

Required state checks passed: 14 tasks, 22 evidence records, revision 27, generated state current. This was source, configuration, and primary-documentation inspection only: no build, new flag experiment, full test suite, assembly inspection, or native CI rerun. No compiler-root-cause, DAW acceptance, or physical-fidelity conclusion is added. No documentation correction is required.

## Required checks performed

```text
python3 tools/project.py validate
python3 tools/project.py sync --check
python3 tools/project.py next --json
```

## Primary sources consulted

- [Clang floating-point language extensions](https://clang.llvm.org/docs/LanguageExtensions.html#extensions-to-specify-floating-point-flags)
- [Clang floating-point compiler options](https://clang.llvm.org/docs/UsersManual.html#controlling-floating-point-behavior)

## Examined SHA-256 snapshots

Repository-relative paths except the explicitly labelled sibling SDK file. All listed hashes were rechecked before writing this report.

| File | SHA-256 |
| --- | --- |
| `docs/DEVELOPMENT.md` | `7761d929fff1b9f3d5c7f4956c869f9b806dd0793ebc100f44cd8eb71b91b419` |
| `docs/VST3.md` | `9ac3bf761fc536eade3fdee70eb77117338c6dc879b493081ff22be22ff79dba` |
| `CMakeLists.txt` | `2a70fba5924a3eeb01bf0dfd7c2a0f6a00726d27ac7535660a4bf267cf81ff3e` |
| `CMakePresets.json` | `a06e2a16dba0b4947352cbbcec2a7668d29b3f777c6f460f9d65f6bd0bb7f0aa` |
| `cmake/Vst3.cmake` | `e779743e238c5b7ac2632d73764eb19974fcef3f4ae5c8699ac6f188fde783a6` |
| `.github/workflows/ci.yml` | `9a0d441f70dc60be6bdf7ec3b3ab5a3c5e046feed103a043633d1bd27911e94a` |
| `.github/workflows/vst3.yml` | `6cf2593353d05a6b122c99736399132b4c813ec8fba5af03b39ed4d5fdd8e3f3` |
| `include/rhodes/control_value.hpp` | `d7c282afdcffaef90f7872f97b37bfb34671448da64376d3439649c052d4d48d` |
| `include/rhodes/quadrature_fir.hpp` | `0ddcf691a5d251979a7adf1ea32d471c8fcf1fd6e5a7a2e203c8fb63974b7b6d` |
| `docs/agents/reports/20261124-w3-47-clang-scope.md` | `7eaddb4ef314b42094b75bf016865e6f9e5169eaa9bf7643ee7092e766767e28` |
| SDK: `../vst3sdk/cmake/modules/SMTG_PlatformToolset.cmake` | `57f94d9fc125aae7b1e5c790a62a740ec7d843c485251761cea008cc5c6d6ef2` |
