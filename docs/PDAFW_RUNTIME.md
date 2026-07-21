# PDAFW runtime dependency

`PDAFWPlugin.dll` is PureDark's compiled frame-warp runtime. UEVR supplies the
D3D device/queue, eye color and depth buffers, Unreal motion vectors, camera
matrices, and optional UI textures; the plugin produces reconstructed eye
frames for Alternate Eye, Previous Frame, and Combined Warping.

The public source tree contains only:

- `dependencies/pd-afwmod/include/PDAFWPlugin.h` — the caller/runtime ABI;
- `dependencies/pd-afwmod/dummy/PDAFWPlugin.cpp` — no-op exports used to create
  the link-time import library.

The dummy DLL does **not** perform frame warping. The real DLL is ignored by git
and must be obtained from the matching official PureDark release. Do not package
or run the dummy output as if it were the runtime.

## Required beta.4 runtime

This compatibility effort targets the official PureDark beta.4 runtime:

```text
PDAFWPlugin.dll SHA-256
76bbc4d7a5370ba0d6a1a50de6b81d45223e42b764d2d7c79f0c6c48a59f6b64
```

Obtain it from the official
[PureDark UEVR AFW beta.4 release](https://github.com/PureDark/UEVR/releases/tag/UEVR_AFW_v1.0-beta.4),
subject to PureDark's distribution terms, and place it next to the deployed
`UEVRBackend.dll`.

The following must remain one compatible checkpoint:

1. `PDAFWPlugin.dll`;
2. `PDAFWPlugin.h`;
3. the UEVR backend callers built against that header.

Do not hot-swap only the DLL across ABI versions. Beta.4 adds
`FrameWarpEvaluateParams::UseUINT64` and changes the renderer `Copy` signature;
a mismatched binary/header/caller set can crash or corrupt memory.

## UESDK is separate

UESDK remains a gated source submodule. Builders must link their GitHub account
to Epic, obtain access to `PureDark/UESDK`, configure SSH, and then run:

```bash
git submodule update --init --recursive
```

The compatibility integration uses UESDK revision:

```text
9034a85742ac178dbfbb2e93dd8492ec58ee6d99
```

UESDK source and the PDAFW runtime solve different dependency requirements:
UESDK provides Unreal reflection/stereo integration source, while
`PDAFWPlugin.dll` provides the compiled frame-warp implementation.
