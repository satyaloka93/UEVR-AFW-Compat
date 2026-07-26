# Games

Per-game working VR states: what works today, the exact known-good config, and
which experiments were rejected (with reasons).

* [Avowed](avowed.md) — playable 6DoF/AFW; stereo/avatar/Lua fixes plus a dump-derived stale crafting-attachment guard under repeated validation.
* [The Outer Worlds 2](outer-worlds-2.md) — guarded Native/OpenXR startup, then beta.4 Previous Frame AFW with zero observed ghosting; title timing remains under validation.
* [Hogwarts Legacy](hogwarts-legacy.md) — post-transition cooldown fixed TaskGraph crashes; direct-pose fallback rejected.
* [Silent Hill f](silent-hill-f.md) — alpha.2 publishes the UE5.7/OpenXR candidate and clean Native-start profile; DLSS still needs a manual reapply and AFW remains limited.
* [Silent Hill 2](silent-hill-2.md) — no carve-out, rides the generic shared path; unified onto the fork by dropping the hardened UE5.8 UESDK and forcing Native cold-start (runtime AFW works). Distinct from Silent Hill f.
