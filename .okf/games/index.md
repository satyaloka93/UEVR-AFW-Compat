# Games

Per-game working VR states: what works today, the exact known-good config, and
which experiments were rejected (with reasons).

* [Avowed](avowed.md) — known-good dynamic-Lua 6DoF plus a dump-derived stale crafting/loadout attachment guard; the separate local-avatar/native-bone hand experiment is parked and not a maintained-backend dependency.
* [The Outer Worlds 2](outer-worlds-2.md) — guarded Native/OpenXR startup, beta.4 Previous Frame AFW with zero observed ghosting, and committed explicit-enrollment source plus a packaged overlay for true Steam weapon 6DoF.
* [Hogwarts Legacy](hogwarts-legacy.md) — post-transition cooldown fixed TaskGraph crashes; direct-pose fallback rejected.
* [Silent Hill f](silent-hill-f.md) — alpha.4 publishes the maintained rebuilt first-person/6DoF profile and f37 plus fail-closed SceneView backend; it remains experimental pending decisive guard-path and broader cross-game validation.
* [Silent Hill 2](silent-hill-2.md) — forced-Native, Native-Stereo-Fix-on renderer checkpoint plus a separate full plugin/IK first-person profile; AFW remains unsafe. Distinct from Silent Hill f.
