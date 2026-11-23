# Third-party notice index — Rhodes Research Prototype

## Official VST3 SDK

The optional headless instrument is built with Steinberg's official VST3 SDK
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
packaging the module. VSTGUI, SDK examples, documentation and tutorials are not
included in this headless integration.

## Scope of this index

The repository's original code has no selected project-wide software license.
The SDK notice does not license that original code or imply affiliation with
Steinberg, Fender or Rhodes. NumPy and SciPy are optional external offline audit
tools; their code and binary distributions are not included in this VST3 bundle.
