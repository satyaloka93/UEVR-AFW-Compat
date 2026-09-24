# AFW beta.4 compatibility v0.1.0-alpha.5

Experimental backend upgrade based on source `832bff79` and UESDK
`2f201aaeb8fbc213078afe3b0a7c6e80feb0a3f8`. Not a stable release or an
upgrade to PureDark beta.6. Existing profiles are deliberately not replaced.

## Changes since alpha.4

### CVar access and stereo lighting

- Bounded console-registry discovery and validated CVar getter/setter layouts
  restore usable controls in The Outer Worlds 2 and Silent Hill f, including
  the extended interface that defeated the legacy scanner.
- Game-thread writes include before/request/readback diagnostics. Unsupported
  interfaces fail closed rather than calling guessed native functions.
- Users reported corrected stereo lighting/shadows; retained SHf logs verify
  `r.Shadow.Virtual.Enable` changing from 1 to 0. Other settings also changed,
  so this is not proof of isolated VSM causality or a performance gain.

### Performance menu and controller navigation

- Twenty detected-in-game controls cover Lumen reflections/GI, screen-space
  reflections, volumetric fog, shadow quality/distance, view distance and
  foliage density. Missing registry entries are hidden; an exposed CVar does
  not guarantee the game currently uses that rendering feature.
- Friendly labels, tooltips and bounded values; sliders apply on release.
  No automatic performance preset is imposed.
- Direct right-thumbstick scrolling in the settings pane, with protections
  against duplicate mouse-wheel input, dragging and popup interference.

### User-script control

- All games gain saved opt-in automatic `user_script.txt` application,
  explicit apply-once and bypass/cancel controls.
- TOW2/SHf use validated numeric access, one script entry per tick, skip
  unchanged values and report conflicts with frozen menu settings.
- Cancel stops pending work; it does **not** undo already applied CVars.
  Restart for a clean baseline. Independent Lua scripts and saved/frozen
  menu settings are not disabled by this switch.

### UObject browser safety

- Validate array headers and object identity before formatting object names.
- Correct interface-array stride and page object arrays in bounded groups.
- Valid engine objects no longer have to be enrolled in UObjectHook merely
  to be browsed. No 6DoF enrollment/profile behavior is intentionally changed.

### AFW interoperability and diagnostics

- Port selected upstream implementation-hook, velocity-detection, OptiScaler
  NGX routing and scanner-dependency fixes while retaining beta.4's ABI.
- Validate texture-copy layouts, recreate targets when formats change and
  restore the observed resource state when copying velocity buffers.
- Correct AFW vectors in private copies, preserving DLSS/Cheeky inputs.
- Optional Cheeky final-eye SR outline rendering avoids painting diagnostic
  borders into persistent warp history. Requires a compatible addon, such as
  [the Cheeky September 24 prerelease](https://github.com/satyaloka93/CheekyFoveatedDLSS/releases/tag/uevr-20260924-pre1).
- Native OpenXR stale-display-time guard and bounded delivery/API summaries;
  sampled final-copy GPU costs and CPU wait/engine/Present measurements.
  Coverage varies by game; these are not complete engine GPU profiles.
- Addon-host/NR compatibility code exists in the source, but the built-in
  addon host remains disabled. ReShade owns Cheeky; this release does not
  enable native-host NR or claim NR execution/performance improvements.

## Installation and runtime requirement

This is a **backend-only upgrade** for an existing UEVR AFW installation.
Close the game and injector. Back up the entire existing install, then replace
`UEVRBackend.dll` with the file from the main alpha.5 ZIP. Preserve your
frontend, loaders, configuration and AppData profiles. The symbols ZIP is for
debugging; retain its matching PDB if collecting crash dumps.

The maintained runtime checkpoint uses the **official beta.4**
`PDAFWPlugin.dll`, SHA-256:

`76bbc4d7a5370ba0d6a1a50de6b81d45223e42b764d2d7c79f0c6c48a59f6b64`

Alpha.4 bundled an amended runtime with hash `b129118b...`; do not assume it
is the alpha.5 checkpoint. Obtain the matching official runtime under its
distribution terms. The runtime and generated no-op build stub are **not**
included here. Do not replace your runtime with a build-output stub.

Back up first and update only with the matching backend/runtime pair; do not
mix beta.6 runtime/header ABI with this beta.4 backend. To roll back, restore
the complete previous pair and its configuration from your backup.

## Validation and limitations

Prior working-tree Release builds, targeted regressions and user gameplay
tests informed this checkpoint. Publication uses an isolated export of the
committed source with its pinned submodules and build-system SDK patch.
The isolated Release build and Windows CVar interface fixtures passed, as did
performance-CVar catalog, script policy, UObject array, OpenXR submission,
AFW copy-layout/border-raster and actual-ImGui scrolling regressions.
Generated build identification explicitly records the source hash and alpha.5.
No local haptics/melee experiments, profiles, dumps, caches or RenderDoc
runtime are packaged. UESDK remains a private build dependency.

AFW ghosting, final-eye outline visual correctness and broad cross-game
regression remain open. Native timing runs showed accepted submissions and
low measured copy costs, but **no demonstrated overall FPS improvement**.
The right-stick fix has a real-ImGui regression test; headset confirmation
remains pending. A new release binary is not automatically a new game-test pass.

Keep known-good game-specific rendering modes. Restart to leave AFW;
in-process AFW-to-Native switching remains unsafe. Silent Hill 2 remains
Native-only. Do not blindly apply alpha.4's historical AFW profile settings
to a currently working Native profile.

## Assets

- `UEVR-AFW-Compat-v0.1.0-alpha.5.zip`: backend, license and release notes.
- `UEVR-AFW-Compat-v0.1.0-alpha.5-symbols.zip`: matching backend PDB.
- `RELEASE_SHA256SUMS.txt`: archive and binary checksums.

No injector, PDAFW runtime, Cheeky addon or new profile pack is included.

Backend SHA-256: `70ccbaaf4c357deaacc3990fd01a911a029d31dccf0e5abc01c7a9073e4f50c1`.
Matching PDB SHA-256: `25977a41f1ab3fa9f53af74cf29c121bd5c7679ebdcc1e61f271184c2ca4d716`.
