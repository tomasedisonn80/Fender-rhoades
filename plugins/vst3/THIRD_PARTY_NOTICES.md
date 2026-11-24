# Third-party notice index — Rhodes Research Prototype

## Official VST3 SDK

The optional instrument is built with Steinberg's official VST3 SDK
`v3.8.1_build_84`. The accompanying `LICENSE.txt` is the unmodified common MIT
notice, copyright 2026 Steinberg Media Technologies GmbH. It covers the five
identical notice files identified below; this index does not replace that text.

| SDK component | Exact revision | Official notice source |
| --- | --- | --- |
| Root `vst3sdk` | `3cdf9ca5d1f5b1b21e0a86832aa4abe55607bd96` | [LICENSE.txt](https://github.com/steinbergmedia/vst3sdk/blob/3cdf9ca5d1f5b1b21e0a86832aa4abe55607bd96/LICENSE.txt) |
| `base` | `fcf9da0bd27a16f7f03773a3a39822f28f5c8477` | [base/LICENSE.txt](https://github.com/steinbergmedia/vst3_base/blob/fcf9da0bd27a16f7f03773a3a39822f28f5c8477/LICENSE.txt) |
| `cmake` | `054c9143cbb8d47fc4694e473f2ee3b4d951a8f5` | [cmake/LICENSE.txt](https://github.com/steinbergmedia/vst3_cmake/blob/054c9143cbb8d47fc4694e473f2ee3b4d951a8f5/LICENSE.txt) |
| `pluginterfaces` | `4f547e8e102b47de4a8b8aaf343c73b700786372` | [pluginterfaces/LICENSE.txt](https://github.com/steinbergmedia/vst3_pluginterfaces/blob/4f547e8e102b47de4a8b8aaf343c73b700786372/LICENSE.txt) |
| `public.sdk` | `586dc5e6c8012c3e4b01c79389375cbe96bdb1da` | [public.sdk/LICENSE.txt](https://github.com/steinbergmedia/vst3_public_sdk/blob/586dc5e6c8012c3e4b01c79389375cbe96bdb1da/LICENSE.txt) |

All five inspected files are 1094 bytes and have SHA-256
`e86d79e19e4a33ebc442d2e0d083887dfc75a2a8de9fe17eeddedb84810ad74e`.
Keep this index and the common notice together in `Contents/Resources` when
packaging the module. SDK examples, documentation and tutorials are not included
in the module.

## Native photo editor

Editor-enabled builds also use the official VSTGUI checkout at
`5db272256172557818b6158cf0bb2c4410bddb25`. Its unmodified BSD-style notice is
included as `VSTGUI-LICENSE.txt`. The bundled parser/compression dependencies have
their unmodified notices alongside it as `RapidJSON-LICENSE.txt` and
`miniz-LICENSE.txt`, copied from that same pinned checkout. Scripting is disabled;
Tiny-JS is not linked. The native platform GUI libraries remain system libraries.

`rhodes-suitcase.png` is a lossless format conversion of the image supplied by
the repository owner with an explicit request to commit and use it for this
editor. The original image, hashes, intended use and authorization scope are
recorded in `assets/reference` in the source repository. No broader image
license or historical-image authorship is asserted.

### Expat XML parser

The pinned VSTGUI checkout includes an Expat fallback whose `expat.h` identifies
version 2.1.1. Its source headers refer to an upstream `COPYING` file that is
absent from the SDK's embedded directory. The accompanying `Expat-LICENSE.txt`
is the complete, unmodified [official Expat 2.1.1 COPYING](https://github.com/libexpat/libexpat/blob/ee0adc0d9d245f4f18863c997be9a1bfeeb0a070/expat/COPYING),
from tag `R_2_1_1` at commit `ee0adc0d9d245f4f18863c997be9a1bfeeb0a070`.
It retains the MIT-style permission terms and the Thai Open Source Software
Center, Clark Cooper and Expat maintainers' copyrights. The file is 1208 bytes;
its SHA-256 is
`f6eeb2310d0b00e5cecac39c73a0696a5bdbfb754b26325ed5a502fc1a6c338c`.

Editor bundles include this fallback notice even when VSTGUI finds and uses a
system Expat library. The notice does not replace the applicable notices for a
separately redistributed system Expat version.

## Scope of this index

The repository's original code has no selected project-wide software license.
The SDK notice does not license that original code or imply affiliation with
Steinberg, Fender or Rhodes. NumPy and SciPy are optional external offline audit
tools; their code and binary distributions are not included in this VST3 bundle.

## Local VSTGUI adaptation

Linux editor builds use a generated copy of the pinned VSTGUI
`vstgui/lib/platform/linux/x11platform.cpp` with two additional lines mapping
the original key event’s Mod4 state to Super. The original license header and
all other behavior are retained. The external SDK checkout is unchanged.
See `docs/DEPENDENCIES.md` and `cmake/VstguiX11Keyboard.cmake` in the project
source for exact input/output hashes and checks. The accompanying
`VSTGUI-LICENSE.txt` covers this adaptation.
