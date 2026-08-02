# Games

Per-game working VR states: what works today, the exact known-good config, and
which experiments were rejected (with reasons).

* [Avowed](avowed.md) — playable 6DoF/AFW; game-aware avatar resolution, dynamic Lua and a dump-derived stale crafting/loadout attachment guard.
* [The Outer Worlds 2](outer-worlds-2.md) — guarded Native/OpenXR startup, beta.4 Previous Frame AFW with zero observed ghosting, and committed explicit-enrollment source plus a packaged overlay for true Steam weapon 6DoF.
* [Hogwarts Legacy](hogwarts-legacy.md) — post-transition cooldown fixed TaskGraph crashes; direct-pose fallback rejected.
* [Silent Hill f](silent-hill-f.md) — alpha.2 publishes the UE5.7/OpenXR candidate and clean Native-start profile; DLSS still needs a manual reapply and AFW remains limited.
* [Silent Hill 2](silent-hill-2.md) — Native-first renderer checkpoint plus a separate full plugin/IK first-person profile; preserve renderer and profile variants independently. Distinct from Silent Hill f.
