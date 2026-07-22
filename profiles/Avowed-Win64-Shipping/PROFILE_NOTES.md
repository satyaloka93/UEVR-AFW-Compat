# Avowed profile notes

Executable: `Avowed-Win64-Shipping.exe`

This profile includes the hardened `Avowed6dof.lua`, its active Lua libraries,
controller bindings, game data and the current AFW/OpenXR configuration. Install
it as a clean profile; do not merge `hands_avowed.lua` or old backup scripts.

The bundled configuration starts in AFW with Ghosting Fix enabled. If diagnosing
injection or stereo initialization, restore Native first and use a fresh process.
Do not switch AFW back to Native in-process; restart instead.

`data/avowed.txt` preserves the tested profile's gameplay options, including
automatic health/essence regeneration. Review and disable the `CHEAT_*` values
before playing if those options are not wanted.

Repeated crafting, weapon replacement and loadout switching remain important
regression tests. The backend contains an Avowed-scoped stale attachment guard,
but a crash-free run does not prove that its protective branch fired.
