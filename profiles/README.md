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

The game-specific Lua/profile work derives from the Joey Hodge profile lineage
and its bundled UEVR Lua utility libraries, with compatibility hardening recorded
in the repository OKF. The backend and closed PDAFW runtime remain separate
components with their own provenance and licensing requirements.
