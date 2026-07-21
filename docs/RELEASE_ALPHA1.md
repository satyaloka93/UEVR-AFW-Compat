# AFW beta.4 compatibility v0.1.0-alpha.1

This is an experimental compatibility prerelease based on PureDark
`UEVR_AFW_v1.0-beta.4`. It publishes the Avowed and The Outer Worlds 2 source
integration so users can test additional games and report reproducible failures.
It is not a stable UEVR release.

## Game status

| Game | Status |
|---|---|
| Avowed | Working experimental AFW/6DoF integration. Repeated crafting, weapon replacement, and loadout validation remains open. |
| The Outer Worlds 2 | Working experimental Native startup followed by beta.4 Previous Frame AFW. Title timing and repeated 2D/inventory transitions remain open. |
| Silent Hill f | **Not supported.** Joey Hodge's working Native/UE5.7 lineage is documented in OKF as future porting reference, but this AFW build does not run SHf. |
| Other games | No compatibility claim unless listed in the OKF game state. |

## Package contents

- `UEVRBackend.dll` — compatibility backend;
- `openvr_api.dll` — matching OpenVR loader built from this source;
- `UEVRPluginNullifier.dll` — matching plugin nullifier;
- `PDAFWPlugin.dll` — official PureDark beta.4 frame-warp runtime;
- `openxr_loader.dll` — matching OpenXR loader;
- `SHA256SUMS.txt` and this release note.

The matching `UEVRBackend.pdb` is attached separately for crash analysis. The
package does not include the UEVR injector; use the official UEVR frontend.

## TOW2 startup rule

Start the game in Native Stereo:

```text
VR_RenderingMethod=0
VR_NativeStereoFix=false
VR_AFW_FramewarpMode=2
VR_GhostingFix=false
VR_GhostingFixBootstrapViewStates=false
VR_AFW_FixMovingObjectBrightnessFlickering=false
```

After reaching gameplay, switch Native → AFW and enable Ghosting Fix plus
Bootstrap. Do not switch AFW → Native in-process; PDAFW exposes no teardown API
and the right eye can remain black. Restart to leave AFW.

The proven gameplay settings and remaining title requirements are in
`.okf/games/outer-worlds-2.md`.

## Avowed notes

Avowed retains authoritative OpenXR wait-frame timing, direct Native RHI pose
publication, monotonic second-pass handling, object-motion/ghosting corrections,
and the stale attachment guard. See `.okf/games/avowed.md` and
`.okf/fixes/avowed-stale-attachment-guard.md`.

## Runtime and source dependencies

The backend callers and `PDAFWPlugin.dll` must remain beta.4 ABI-compatible.
Do not hot-swap only the runtime DLL. See `docs/PDAFW_RUNTIME.md`.

Building requires Epic-authorized access to gated `PureDark/UESDK`. The
submodule directly pins tested revision `9034a857`; no UESDK source or
compatibility patch is redistributed in this public fork.

## Reporting problems

Attach the game profile config, `log.txt`, exact SHA-256 values, and—when a
crash occurs—the dump matching the released PDB. Remove usernames, local paths,
tokens, saves, and unrelated personal data before posting.
