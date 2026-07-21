# Games

Per-game working VR states: what works today, the exact known-good config, and
which experiments were rejected (with reasons).

* [Avowed](avowed.md) — playable 6DoF/AFW; stereo/avatar/Lua fixes plus a dump-derived stale crafting-attachment guard under repeated validation.
* [The Outer Worlds 2](outer-worlds-2.md) — guarded Native/OpenXR startup, then beta.4 Previous Frame AFW with zero observed ghosting; title timing remains under validation.
* [Hogwarts Legacy](hogwarts-legacy.md) — post-transition cooldown fixed TaskGraph crashes; direct-pose fallback rejected.
* [Silent Hill f](silent-hill-f.md) — Joey-derived Native UE5.7/OpenXR baseline through `57shf54`/`shf57`; current PureDark AFW compatibility release is non-working for SHf.
