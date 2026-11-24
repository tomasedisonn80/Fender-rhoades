# Dependency provenance

## Default research core

The default core has no third-party runtime library dependencies and downloads
no packages during its build. Its build requirements remain a C++20 compiler
and CMake 3.24 or later; repository tooling uses Python 3.11 or later. See
[development instructions](DEVELOPMENT.md) and the [root build](../CMakeLists.txt).

No third-party source is vendored into this repository by the optional SDK integration
below. The project-wide software license remains an owner decision; recording
the SDK's license does not select a license for this project's original code.
See [rights and attribution](../RIGHTS.md).

## Optional official VST3 SDK

The experimental VST3 adapter uses a separately supplied local checkout
of Steinberg's official SDK. It is optional and must remain outside the default
core's dependency graph. This document records the inspected dependency and
packaging requirements. Actual build and validator observations, along with the
remaining platform and release limits, are recorded in [VST3.md](VST3.md).

| Item | Pinned value |
|---|---|
| Official repository | [steinbergmedia/vst3sdk](https://github.com/steinbergmedia/vst3sdk) |
| Release tag observed locally | [`v3.8.1_build_84`](https://github.com/steinbergmedia/vst3sdk/tree/v3.8.1_build_84) |
| Exact root commit | [`3cdf9ca5d1f5b1b21e0a86832aa4abe55607bd96`](https://github.com/steinbergmedia/vst3sdk/tree/3cdf9ca5d1f5b1b21e0a86832aa4abe55607bd96) |
| SDK build requirement | CMake 3.25.0 or later, as specified in the pinned [SDK CMakeLists.txt](https://github.com/steinbergmedia/vst3sdk/blob/3cdf9ca5d1f5b1b21e0a86832aa4abe55607bd96/CMakeLists.txt) |
| SDK/core notice inspection | 2026-11-23, rechecked 2026-11-24 |
| VSTGUI/editor dependency inspection | 2026-11-24 |

The [root build options](../CMakeLists.txt) and
[SDK integration](../cmake/Vst3.cmake) distinguish three configurations:

| Build selection | Required SDK checkouts |
|---|---|
| `RHODES_BUILD_VST3=OFF` (default core build) | None |
| `RHODES_BUILD_VST3=ON`, `RHODES_BUILD_VST3_EDITOR=ON` (editor option default) | `base`, `cmake`, `pluginterfaces`, `public.sdk`, and `vstgui4` |
| `RHODES_BUILD_VST3=ON`, `RHODES_BUILD_VST3_EDITOR=OFF` (headless adapter) | `base`, `cmake`, `pluginterfaces`, and `public.sdk`; no VSTGUI or its GUI system dependencies |

The editor option controls `SMTG_ENABLE_VSTGUI_SUPPORT`. Both adapter modes
disable SDK plug-in and hosting examples. VSTGUI standalone examples, unit
tests, tools and UI scripting (`VSTGUI_UISCRIPTING=OFF`) are disabled; Tiny-JS
is not linked. The project requests `VSTGUI_STANDALONE=OFF`, but the pinned
[SDK helper](https://github.com/steinbergmedia/vst3_cmake/blob/054c9143cbb8d47fc4694e473f2ee3b4d951a8f5/modules/SMTG_VstGuiSupport.cmake)
sets it to `ON` within SDK configuration. That helper links the modules through
`vstgui_support` to `base`, `vstgui` and `vstgui_uidescription`; the standalone
framework is not linked into the modules. The project forces
`SMTG_ENABLE_WAYLAND_SUPPORT=OFF`, selecting the native X11 backend on Linux.
Neither mode needs the SDK documentation or tutorial checkouts. The SDK
validator remains a separate verification requirement for every instrument.

Configuration verifies the root commit and each required submodule's commit,
and rejects changed tracked contents. The editor pin is checked only when its
option is enabled. See [VST3 build instructions](VST3.md#build-the-optional-adapter)
for checkout and configuration commands.

### Exact submodule revisions

The following gitlinks were read from the pinned root commit. The five entries
marked **inspected** were initialized locally and their checked-out commits
matched these gitlinks on 2026-11-24. The other entries were uninitialized: their revisions
are recorded for completeness, without a claim that their contents or notices
were inspected. Repository names follow the pinned
[.gitmodules](https://github.com/steinbergmedia/vst3sdk/blob/3cdf9ca5d1f5b1b21e0a86832aa4abe55607bd96/.gitmodules).

| SDK path | Official source at pinned commit | Local inspection |
|---|---|---|
| `base` | [vst3_base · `fcf9da0bd27a16f7f03773a3a39822f28f5c8477`](https://github.com/steinbergmedia/vst3_base/tree/fcf9da0bd27a16f7f03773a3a39822f28f5c8477) | Inspected |
| `cmake` | [vst3_cmake · `054c9143cbb8d47fc4694e473f2ee3b4d951a8f5`](https://github.com/steinbergmedia/vst3_cmake/tree/054c9143cbb8d47fc4694e473f2ee3b4d951a8f5) | Inspected |
| `pluginterfaces` | [vst3_pluginterfaces · `4f547e8e102b47de4a8b8aaf343c73b700786372`](https://github.com/steinbergmedia/vst3_pluginterfaces/tree/4f547e8e102b47de4a8b8aaf343c73b700786372) | Inspected |
| `public.sdk` | [vst3_public_sdk · `586dc5e6c8012c3e4b01c79389375cbe96bdb1da`](https://github.com/steinbergmedia/vst3_public_sdk/tree/586dc5e6c8012c3e4b01c79389375cbe96bdb1da) | Inspected |
| `doc` | [vst3_doc · `8bfca19d3b76a61d093951ba9297047f544caea1`](https://github.com/steinbergmedia/vst3_doc/tree/8bfca19d3b76a61d093951ba9297047f544caea1) | Gitlink only |
| `tutorials` | [vst3_tutorials · `33b73dfbb87f3fde3bce8c0a10cae934dc66ad34`](https://github.com/steinbergmedia/vst3_tutorials/tree/33b73dfbb87f3fde3bce8c0a10cae934dc66ad34) | Gitlink only |
| `vstgui4` | [vstgui · `5db272256172557818b6158cf0bb2c4410bddb25`](https://github.com/steinbergmedia/vstgui/tree/5db272256172557818b6158cf0bb2c4410bddb25) | Inspected; editor builds only |

### Observed official license notices

The five files below were read from the local pinned checkout. Each identifies
the **MIT License**, with copyright **2026, Steinberg Media Technologies GmbH**.
All five files have the same SHA-256:
`e86d79e19e4a33ebc442d2e0d083887dfc75a2a8de9fe17eeddedb84810ad74e`.

| SDK-relative file | Official pinned source |
|---|---|
| `LICENSE.txt` | [SDK root notice](https://github.com/steinbergmedia/vst3sdk/blob/3cdf9ca5d1f5b1b21e0a86832aa4abe55607bd96/LICENSE.txt) |
| `base/LICENSE.txt` | [Base notice](https://github.com/steinbergmedia/vst3_base/blob/fcf9da0bd27a16f7f03773a3a39822f28f5c8477/LICENSE.txt) |
| `cmake/LICENSE.txt` | [CMake notice](https://github.com/steinbergmedia/vst3_cmake/blob/054c9143cbb8d47fc4694e473f2ee3b4d951a8f5/LICENSE.txt) |
| `pluginterfaces/LICENSE.txt` | [Plug-in interfaces notice](https://github.com/steinbergmedia/vst3_pluginterfaces/blob/4f547e8e102b47de4a8b8aaf343c73b700786372/LICENSE.txt) |
| `public.sdk/LICENSE.txt` | [Public SDK notice](https://github.com/steinbergmedia/vst3_public_sdk/blob/586dc5e6c8012c3e4b01c79389375cbe96bdb1da/LICENSE.txt) |

Every binary bundle built with this SDK must include these unmodified
official copyright and permission notices in its third-party notice resources,
with their source paths identifiable. Packaging verification must compare the
bundled notices against the pinned files. The optional target currently copies
the common SDK notice into its resources; the five inspected notices are byte
identical. A distributable package still requires its own packaging review.

The observations above apply to the five named SDK files. VSTGUI has a separate
license and additional component notices, recorded below. Example-specific code
and disabled optional components remain outside the integration scope. The SDK
pin and notice inventory must be updated together if the dependency changes.

The [resource notice index](../plugins/vst3/THIRD_PARTY_NOTICES.md) identifies all
five source paths and revisions for packaging beside the common `LICENSE.txt`.
On 2026-11-23 the existing Linux bundle's
`build/vst3/VST3/Release/RhodesResearch.vst3/Contents/Resources/LICENSE.txt`
was compared byte-for-byte with all five pinned source files: all six copies
were identical, 1094 bytes, with the SHA-256 above. The optional target also
copies `THIRD_PARTY_NOTICES.md` through its resource list. The subsequent
[Linux bundle audit](agents/development-20261123/W3-A058.md) verified that the
bundled index exactly matches the project source; the
[visibility update](agents/development-20261123/W4-A062.md) preserved the bundle
metadata and required entry points. These are local bundle observations, not
verification of a distributable package on every platform.

### VSTGUI and packaged editor notices

The pinned `vstgui4/LICENSE` is the **VSTGUI LICENSE**, copyright **2022,
Steinberg Media Technologies**. Its three-clause BSD-style terms require
retention of the copyright, conditions and disclaimer, and restrict use of
Steinberg's or contributors' names for endorsement. It is a separate notice
from the SDK's MIT license. The following complete files were read and hashed
from the same pinned VSTGUI checkout on 2026-11-24:

| SDK-relative source and official pinned notice | Bundled resource | Bytes | SHA-256 |
|---|---|---:|---|
| [`vstgui4/LICENSE`](https://github.com/steinbergmedia/vstgui/blob/5db272256172557818b6158cf0bb2c4410bddb25/LICENSE) | `VSTGUI-LICENSE.txt` | 1858 | `605bcd2152176b365cf0ba6cca331a988fa885b4f43f20ceff4e264e6576ca66` |
| [`vstgui4/vstgui/thirdparty/rapidjson/license.txt`](https://github.com/steinbergmedia/vstgui/blob/5db272256172557818b6158cf0bb2c4410bddb25/vstgui/thirdparty/rapidjson/license.txt) | `RapidJSON-LICENSE.txt` | 5152 | `a140e5d46fe734a1c78f1a3c3ef207871dd75648be71fdda8e309b23ab8b1f32` |
| [`vstgui4/vstgui/thirdparty/miniz/LICENSE`](https://github.com/steinbergmedia/vstgui/blob/5db272256172557818b6158cf0bb2c4410bddb25/vstgui/thirdparty/miniz/LICENSE) | `miniz-LICENSE.txt` | 1162 | `6f20fa7672b00e2e975c291df737cf227addf3ad32e36fef3fe0f416e4664d3d` |

RapidJSON's file identifies its MIT terms and also retains notices for
msinttypes (BSD) and JSON.org/JSON_checker (JSON license). Preserve that complete
file; describing it only as an MIT notice would omit its component terms.
The file identifies `bin/jsonchecker/` as the JSON-licensed component, and that
directory is absent from the pinned VSTGUI copy. miniz's notice contains
MIT-style permission terms and the copyrights of RAD Game Tools, Valve
Software, Rich Geldreich and Tenacious Software LLC.

[Vst3.cmake](../cmake/Vst3.cmake) copies these three files unmodified into
`Contents/Resources` of each editor-enabled bundle, alongside the SDK
`LICENSE.txt` and project `THIRD_PARTY_NOTICES.md`. On 2026-11-24, all four
SDK/VSTGUI/RapidJSON/miniz license resources in each local Linux editor bundle were
compared byte-for-byte with their pinned sources and matched. That observation
covers the built Linux bundles; native Windows/macOS editor packaging and
named-DAW operation require their own results. Current module and initial
native-window observations are recorded in the
[editor checkpoint](VST3.md#current-photo-editor-checkpoint--24-november-2026).

VSTGUI's deprecated XML parser uses system Expat when found, otherwise its
embedded Expat source; see the pinned
[UIDescription build](https://github.com/steinbergmedia/vstgui/blob/5db272256172557818b6158cf0bb2c4410bddb25/vstgui/uidescription/CMakeLists.txt).
The inspected Linux editor cache selected system `libexpat` 2.9.0. The embedded
[expat.h](https://github.com/steinbergmedia/vstgui/blob/5db272256172557818b6158cf0bb2c4410bddb25/vstgui/thirdparty/expat/expat.h)
identifies version 2.1.1. Its copyright headers refer to `COPYING`, which is
absent from that SDK directory. The project therefore retains the complete
official Expat 2.1.1 [COPYING](https://github.com/libexpat/libexpat/blob/ee0adc0d9d245f4f18863c997be9a1bfeeb0a070/expat/COPYING)
as [Expat-LICENSE.txt](../plugins/vst3/resources/Expat-LICENSE.txt). The upstream
tag `R_2_1_1` resolves to commit `ee0adc0d9d245f4f18863c997be9a1bfeeb0a070`;
the copied file is 1208 bytes, with SHA-256
`f6eeb2310d0b00e5cecac39c73a0696a5bdbfb754b26325ed5a502fc1a6c338c`.

That unmodified MIT-style notice retains the Thai Open Source Software Center,
Clark Cooper and Expat maintainers' copyrights. All 19 embedded Expat source
and header files were inspected for copyright and license statements; their
notices name Thai Open Source Software Center or Clark Cooper and refer to
`COPYING`, with no conflicting license found. Editor bundles include
`Expat-LICENSE.txt` regardless of which Expat configuration was selected.
This records the embedded fallback's notice; a redistributed system Expat
library still needs the notices for its actual version. The earlier four-file
Linux byte comparison does not claim a rebuilt Expat resource was verified.

The photograph is a separate project asset. The owner authorized committing
and using the supplied original for this editor, as recorded in the
[reference provenance](../assets/reference/README.md). The packaged
`rhodes-suitcase.png` is its lossless conversion. That instruction does not
assert image authorship or grant a broader public license; neither the SDK nor
VSTGUI license covers the photograph or the project's original code.

### Native editor system dependencies

The [CI workflow](../.github/workflows/vst3.yml) configures all three editor
variants on Linux, Windows and macOS. The entries below describe source/build
requirements, not successful execution on every configured platform. The
headless adapter avoids this GUI dependency set.

| Platform | Dependency boundary |
|---|---|
| Linux | Threads, X11, FreeType, PkgConfig; XCB, xcb-util, xcb-cursor, xcb-keysyms, xcb-xkb; xkbcommon and xkbcommon-x11; GLib, Cairo, Pango (`pangocairo`, `pangoft2`) and Fontconfig. The pinned configure step also requires Wayland client/server development files and protocols, plus GTKmm 3.0, `atkmm-1.6` and SQLite3 for the configured standalone target. |
| Windows | The Visual Studio C++ toolchain and Windows SDK provide the native Win32 GUI APIs, including Direct2D, DirectWrite and Windows Imaging Component imports. CI selects x64 and does not install a separate GUI package. |
| macOS | Apple's C++/Objective-C++ toolchain and system frameworks. The pinned VSTGUI target links Cocoa, QuartzCore and Accelerate, UniformTypeIdentifiers for deployment targets at least 11.0, and OpenGL while its upstream default option is enabled. The project also propagates CoreFoundation for SDK base users. CI configures deployment target 11.0 with a non-Xcode generator; this does not establish a tested deployment baseline. |

The primary build sources are the pinned
[Linux package checks](https://github.com/steinbergmedia/vstgui/blob/5db272256172557818b6158cf0bb2c4410bddb25/vstgui/CMakeLists.txt),
[native platform target](https://github.com/steinbergmedia/vstgui/blob/5db272256172557818b6158cf0bb2c4410bddb25/vstgui/lib/CMakeLists.txt),
[Windows imports](https://github.com/steinbergmedia/vstgui/blob/5db272256172557818b6158cf0bb2c4410bddb25/vstgui/lib/platform/win32/win32factory.cpp),
[standalone package checks](https://github.com/steinbergmedia/vstgui/blob/5db272256172557818b6158cf0bb2c4410bddb25/vstgui/standalone/CMakeLists.txt),
and [VSTGUI option defaults](https://github.com/steinbergmedia/vstgui/blob/5db272256172557818b6158cf0bb2c4410bddb25/vstgui/cmake/modules/vstgui_init.cmake).
The workflow's Debian/Ubuntu package list is `libx11-dev`, `libfreetype6-dev`,
`libxcb1-dev`, `libxcb-util-dev`, `libxcb-cursor-dev`, `libxcb-keysyms1-dev`,
`libxcb-xkb-dev`, `libxkbcommon-dev`, `libxkbcommon-x11-dev`, `libglib2.0-dev`,
`libcairo2-dev`, `libpango1.0-dev`, `libfontconfig1-dev`, `libwayland-dev`, and
`wayland-protocols`, `libgtkmm-3.0-dev`, `libatkmm-1.6-dev`, and `libsqlite3-dev`; it also installs
`xvfb` for an X11 display environment.

Wayland development packages remain necessary because this upstream pin runs
`find_package(Wayland REQUIRED ...)` inside its Linux configuration even when
the Wayland backend is disabled. The disabled backend avoids the conditional
`wayland-server-delegate` fetch in the native platform target. These system
libraries are not vendored into the repository, and their own redistribution
terms remain separate from the SDK/VSTGUI notices. Follow the
[toolchain requirements](DEVELOPMENT.md) for the complete C++20 test build.

## Optional independent Python audit

[`tools/validate_contact_reference.py`](../tools/validate_contact_reference.py)
uses NumPy arrays and SciPy's independent Radau, matrix-exponential, root and
quadrature routines. These are developer-side numerical audit dependencies.
They are not dependencies of the C++ core, offline C++ reference, VST3 audio
callback, ordinary CTests, or standard-library candidate-sweep runner. No
Python package or third-party source is vendored by this documentation change.

The following installed versions and metadata were inspected in the Linux
Python 3.12 environment on 2026-11-23. These are observed audit versions, not a
complete environment lock or a claim about other available releases.

| Distribution | Observed version | Primary source | Upstream project license |
| --- | --- | --- | --- |
| NumPy | `2.3.5` | [numpy/numpy](https://github.com/numpy/numpy), [official license](https://numpy.org/doc/stable/license.html) | BSD 3-Clause |
| SciPy | `1.17.0` | [scipy/scipy](https://github.com/scipy/scipy), [official project](https://scipy.org/) | BSD 3-Clause |

Installed `METADATA` identifies both distributions as BSD licensed and points
to those upstream repositories. Their installed license files retain the
project notices and additional bundled-component notices. The inspected files
list OpenBLAS, LAPACK, GCC runtime and libquadmath notices; NumPy also lists
source/build components. Do not reduce a redistributed binary distribution's
complete notice set to the project-level BSD label. These Python distributions
are not packaged in the VST3 bundle.

| Installed file, relative to `site-packages` | Bytes | SHA-256 of complete installed notice file |
| --- | --- | --- |
| `numpy-2.3.5.dist-info/LICENSE.txt` | 47768 | `2046a3130e50b11c01659b3a0d963e6ae0b7436ff8e89cbcfd9e87bc6112d595` |
| `scipy-1.17.0.dist-info/LICENSE.txt` | 46838 | `4daf14e37432e7026165979ecb7a39930707a3e89bef4876cfaf9dad36ed364a` |

These hashes identify the observed notice bytes, not a wheel, source archive or
all transitive dependencies. A new audit environment should record its actual
versions and retain its distribution notices. The repository's original-code
license remains a separate owner decision.

### Local X11 keyboard adaptation

Linux editor builds apply [a two-line adaptation](../cmake/VstguiX11Keyboard.cmake)
to a generated copy of the pinned VSTGUI `x11platform.cpp`. It maps the original
key packet’s Mod4 state to `ModifierKey::Super`, which the pinned keyboard
converter omits. This preserves event-time state and the editor’s existing
shortcut guard. Shift, Control, Alt, key mapping and event order are unchanged.

The [original pinned source](https://github.com/steinbergmedia/vstgui/blob/5db272256172557818b6158cf0bb2c4410bddb25/vstgui/lib/platform/linux/x11platform.cpp)
has LF-normalized SHA-256
`7158057162bc716aeb52efc5e6744b9432ffb8b86e9e0a68895d9d340cc87bb3`.
The adapted copy has SHA-256
`c186a90e423b3895347a676e07c6059accb3b9ab0c187a795d318a1f6af361be`.
Configuration checks both hashes and exactly one source replacement. The external
SDK remains clean, with all pin/content guards active. The generated copy retains
the upstream license header and existing packaged VSTGUI license. Headless,
Windows and macOS builds are unaffected. Arbitrary XKB modifier remapping and
mouse-event translation remain outside this keyboard-only correction.

### Local X11 Cairo connection lifetime adaptation

Linux editor builds then apply the [lifetime helper](../cmake/VstguiX11Lifetime.cmake)
to that verified Mod4 keyboard overlay and the pinned
[`x11frame.cpp`](https://github.com/steinbergmedia/vstgui/blob/5db272256172557818b6158cf0bb2c4410bddb25/vstgui/lib/platform/linux/x11frame.cpp).
Both original sources come from VSTGUI commit
`5db272256172557818b6158cf0bb2c4410bddb25`, within the unchanged SDK root pin
`3cdf9ca5d1f5b1b21e0a86832aa4abe55607bd96`. The helper generates two build-local
sources in `rhodes-vstgui-overlay`: `x11platform-lifetime.cpp` and
`x11frame-lifetime.cpp`.

The frame registers its Cairo surface's device with the shared run loop. The
platform retains a device reference and, when the final run-loop user exits,
finishes and releases that device before disconnecting XCB. Teardown also
clears released XKB/cursor pointers, cached cursor IDs and the connection
pointer. The existing Mod4 mapping is retained. `ChildWindow` destruction is
unchanged by this helper.

| Source bytes | SHA-256 |
|---|---|
| Original `x11platform.cpp`, LF normalized | `7158057162bc716aeb52efc5e6744b9432ffb8b86e9e0a68895d9d340cc87bb3` |
| Keyboard-adapted platform input, LF | `c186a90e423b3895347a676e07c6059accb3b9ab0c187a795d318a1f6af361be` |
| Original `x11frame.cpp`, LF normalized | `8dc07b3212e9710d23ca5c960ace39900705aa8cb3c09f760ad78ea160200dd5` |
| Generated `x11platform-lifetime.cpp`, LF | `e0c889e4efa8bf0b2f76feb5592084b64987ba2ae012de3a7afd889b131b87f2` |
| Generated `x11frame-lifetime.cpp`, LF | `66fa490278825250966aad9f4a054942f3105ab131ff0cf45ba861c1b42dfd7c` |

Configuration checks these hashes, exactly one replacement per target source,
and each of the four transformation anchors. Both generated sources retain
their upstream license headers; the existing packaged `VSTGUI-LICENSE.txt`
applies. The external SDK remains unmodified with its pin/content guards active. No new
runtime dependency is introduced. The [source and configure report](agents/reports/photo-continuation-20261124/w3-050.md)
records 22 checks within that limited scope; it does not establish a native
build, editor close/reopen result or platform acceptance. Those results require
separate source-bound evidence.

### Local X11 child-window destruction adaptation

Linux editor builds also apply a separate
[child-window helper](../cmake/VstguiX11ChildWindow.cmake) after the Cairo lifetime
helper. It generates `rhodes-vstgui-overlay/x11utils.cpp` from the pinned
[`x11utils.cpp`](https://github.com/steinbergmedia/vstgui/blob/5db272256172557818b6158cf0bb2c4410bddb25/vstgui/lib/platform/linux/x11utils.cpp).
The SDK root pin `3cdf9ca5d1f5b1b21e0a86832aa4abe55607bd96` and VSTGUI pin
`5db272256172557818b6158cf0bb2c4410bddb25` are unchanged. This separate helper
changes `ChildWindow` destruction; the Cairo lifetime helper above does not.

The generated source adds `<cstdlib>` and replaces the empty destructor with a
connection-guarded `xcb_destroy_window_checked` request for the owned child ID,
followed by `xcb_request_check` and release of its returned error. This consumes
an expected `BadWindow` when the host has already destroyed the parent and its
child. It does not destroy the borrowed host parent, finish the shared Cairo
device or disconnect XCB, preserving the connection used by other editors.
The checked request adds a synchronous UI teardown roundtrip without pumping
events. Construction-failure recovery remains outside this adaptation's scope;
the [ownership review](agents/reports/photo-continuation-20261124/w3-055.md)
records that boundary.

| Source bytes | SHA-256 |
|---|---|
| Original `x11utils.cpp`, LF normalized | `67c30ca1e6afa5905a5205a3b2d0dc82ca640728c5c77b3e0ae42a0b46a19bac` |
| Generated `x11utils.cpp`, LF | `47554d4c4e7d330e3791a22f7920e7cfc214a84bff88b5493b4906e36b2a2eae` |

Configuration checks both hashes, one occurrence of each include/destructor
anchor and exactly one target-source replacement. It also tracks the original
as a configure dependency and retains its Linux include directory. The upstream
file starts with a blank line and has no leading license comment; all other
source bytes are preserved after LF normalization. The existing top-level
[VSTGUI license](https://github.com/steinbergmedia/vstgui/blob/5db272256172557818b6158cf0bb2c4410bddb25/LICENSE),
packaged unmodified as `VSTGUI-LICENSE.txt`, applies. The external SDK and its
license files remain unchanged, with the revision/content guards active; no new
runtime dependency is introduced. Headless, Windows and macOS builds are unaffected.

The [helper report](agents/reports/photo-continuation-20261124/w3-057.md) records
13 isolated configure checks only. Those checks do not establish compilation,
native child cleanup, sibling-editor survival or platform acceptance; such
claims require separate source-bound results.

### Local Win32 timer adaptation

The [Win32 timer helper](../cmake/VstguiWin32Timer.cmake) generates a build copy
at `rhodes-vstgui-overlay/wintimer.cpp` from the SDK-relative
`vstgui4/vstgui/lib/platform/win32/wintimer.cpp`, at VSTGUI commit
[`5db272256172557818b6158cf0bb2c4410bddb25`](https://github.com/steinbergmedia/vstgui/blob/5db272256172557818b6158cf0bb2c4410bddb25/vstgui/lib/platform/win32/wintimer.cpp).
After CRLF-to-LF normalization, the only source change is the final return in
`WinTimer::start()`: `return false;` becomes `return timer != 0;`, reporting
whether `SetTimer` returned a nonzero timer. The earlier already-running
rejection remains false; timer registration, callbacks and stop logic retain
their upstream code.

| Source bytes | SHA-256 |
|---|---|
| Original, LF normalized | `8fd9713b0adbdb8c3c5e049a87aaf2bab6d14e2e43f54293b10854942f62dadc` |
| Adapted, LF | `2d7fbc18625171a4edd7861118b821d70b68f5eae520b53704c5d6b957df0191` |

The helper checks both hashes and requires exactly one target source
replacement. It leaves the external SDK unchanged and retains the upstream
license header; the existing packaged `VSTGUI-LICENSE.txt` applies to this
adaptation. This does not select a license for the project's original code.
The [source and helper checks](agents/reports/photo-continuation-20261124/w2-033.md)
do not establish Windows timer execution or editor-close behavior. No Windows
runtime result for this adaptation is recorded yet; native verification remains
pending.
