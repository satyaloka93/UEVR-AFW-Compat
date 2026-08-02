# Included game profiles

These profiles accompany the experimental AFW beta.4 compatibility backend.
They are not universal defaults and should be installed only for the matching
shipping executable.

## Installation

1. Back up the existing directory under `%APPDATA%\UnrealVRMod\`.
2. Start from a clean profile directory; do not merge stale scripts or settings.
3. Copy the matching directory from this folder into `%APPDATA%\UnrealVRMod\`.
4. Use a fresh game process and inject early with the official UEVR frontend.
5. Read the game-specific `PROFILE_NOTES.md` before changing rendering modes.

Included runtime logs, caches, dumps, saves, local paths, MCP plugins and backup
files have intentionally been excluded.

## TOW2 6DoF overlay

`TheOuterWorlds2-Win64-Shipping-6DoF-overlay/` is deliberately an overlay rather
than a complete copy of the third-party-derived alpha.3 TOW2 profile. Apply it
to that known-good 3DoF base and merge `REQUIRED_CONFIG.txt` as instructed in
its `PROFILE_NOTES.md`. It requires backend source commit `217162d7` or later;
the alpha.3 release backend cannot enroll TOW2's late Steam weapon component.

The overlay adds only the independently auditable dynamic attachment, optional
native downstroke melee and framework-menu aim-capture scripts. It contains no
address-derived UObjectHook state or diagnostics.

The game-specific Lua/profile work derives from the Joey Hodge profile lineage
and its bundled UEVR Lua utility libraries, with compatibility hardening recorded
in the repository OKF. The backend and closed PDAFW runtime remain separate
components with their own provenance and licensing requirements.
