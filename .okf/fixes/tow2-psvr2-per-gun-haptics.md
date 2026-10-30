---
type: Experimental Integration
title: TOW2 PSVR2 per-gun adaptive-trigger and grip-haptics prototype
description: Opt-in semantic Lua-to-Toolkit path for firearms and melee that keeps PSVR2Toolkit as the sole Sense actuator owner.
tags: [tow2, psvr2, adaptive-triggers, haptics, melee, lua, experimental]
timestamp: 2026-08-19T00:00:00+09:00
---

# Scope

This prototype adapts the Cyberpunk per-gun haptics architecture to TOW2 without
coupling Lua directly to PSVR2 hardware:

1. TOW2 Lua identifies the current weapon and publishes semantic profile/shot
   events.
2. A minimal UEVR relay plugin forwards only the validated `TOW2PSVR2/1`
   localhost protocol.
3. The generalized PSVR2Toolkit bridge validates the command, translates a
   conservative Sense trigger profile, and mixes grip PCM through its existing
   haptics engine.

See the [TOW2 game state](../games/outer-worlds-2.md). The repository setup and
test guide is `docs/TOW2_PSVR2_HAPTICS.md`. The architecture derives from the
[CyberpunkVR Port reference](../projects/cyberpunk-vr-port.md), while the native
actuator and dual-purpose bridge are documented in the
[PSVR2Toolkit reference](../projects/psvr2toolkit.md).

# Single-owner invariant

PSVR2Toolkit must remain the only Sense actuator owner. The backend option
`VR_ExternalPSVR2Haptics` defaults off and suppresses UEVR OpenXR haptics only
when all three conditions hold:

- the option is explicitly enabled;
- the executable is TOW2;
- SteamVR PSVR2 OpenXR was detected.

This avoids the Cyberpunk failure where otherwise valid OpenXR pulses competed
with Toolkit CAPI and removed or misrouted gun effects. Other games and default
TOW2 profiles are unchanged.

# Initial event model

- Exact weapon full name/class/category chooses an override or conservative
  generic pistol, revolver, rifle, shotgun, heavy or energy profile; recognized
  SMG, assault-rifle and LMG/heavy-automatic families have distinct automatic
  profiles.
- Equipped ranged weapons receive an idle R2 slope.
- Native XInput rumble while R2 is held is the first conservative firing
  confirmation signal.
- Semiautomatic events briefly release R2 and emit per-profile
  right/support-hand PCM.
- The offline automatic-texture candidate replaces repeated off/profile toggles
  with official Sense trigger vibration plus synchronized weapon/support-hand
  PCM rhythms. Lua stops on trigger/gameplay loss or 180–200 ms rumble silence;
  a separate 220–240 ms bridge deadline restores the loaded slope profile.
- Melee weapons clear R2 resistance.
- The already calibrated native downstroke in `05_tow2_melee_swing.lua` emits a
  semantic `melee_swing` from the exact episode that commits the native attack;
  amplitude scales 0.45-0.85 with speed.
- The first impact candidate used native TOW2 rumble inside a 450 ms window.
  Runtime produced 29 inferred impacts for 30 swings at a median 153 ms after
  attack, including swings without sensible reported target contact. This is
  delayed attack/animation feedback, not collision evidence, so impact now
  fails closed.
- Swing commands mix through Toolkit `HapticsEngine`, preserving the
  single-owner invariant. Runtime still requires independent bridge-receipt and
  PCM-output evidence; a Lua `MELEE_SWING` log alone is not actuator proof.
- A follow-up bounded weapon-tip sweep was runtime-rejected and removed. It
  emitted 22 Lua swing semantics but zero contacts/impacts; the supposed early
  edge was only 2 ms median before native commit (143 ms maximum). No usable tip
  socket/bone resolved, and component-origin fallback logged 5,804 times over
  74 seconds. The user felt no haptics while native sound/damage remained about
  1–2 seconds late. The prior impact-disabled baseline was restored.
- Menu entry, nil weapon, disable, script reset and a 2.5-second bridge
  heartbeat timeout clear effects.

# Component and repository boundaries

| Component | Repository | Responsibility |
|---|---|---|
| UEVR backend | This project | Default-off, TOW2 + SteamVR-PSVR2 scoped suppression of competing OpenXR haptic application |
| TOW2 Lua | This project | Weapon classification, shot correlation and committed-downstroke swing semantics; melee impact fails closed |
| UEVR relay DLL | This project | Bounded forwarding of only `TOW2.PSVR2` / `TOW2PSVR2/1` to localhost UDP |
| Toolkit bridge | [PSVR2Toolkit](../projects/psvr2toolkit.md) | Protocol validation, trigger translation, heartbeat fail-safe and grip-PCM mixing |
| Toolkit driver/CAPI | [PSVR2Toolkit](../projects/psvr2toolkit.md) | Exclusive native Sense actuation |
| Design/melee reference | [CyberpunkVR Port](../projects/cyberpunk-vr-port.md) | Proven semantic ownership model and direct-collision swing/impact behavior |

The UEVR backend changed for the initial integration only: it introduced
`VR_ExternalPSVR2Haptics`. The later melee addition does not alter
`UEVRBackend.dll` or the relay DLL; it changes the two TOW2 Lua scripts and the
Toolkit bridge protocol/consumer.

# Validation status

The automatic-texture candidate is offline-only and not deployed or
runtime-approved. It does not change the SteamVR driver, CAPI, UEVR relay, or
launcher. Candidate bridge/parser builds pass MSVC tests, and the candidate Lua
passes Lua 5.4-compatible parsing. Runtime must begin with one already recognized
automatic weapon so classification changes are not conflated with texture
behavior.

Static/build validation completed:

- Lua syntax passed under Lua 5.4-compatible parsing.
- Relay built x64 and exports both required UEVR plugin functions.
- Relay imports only `WS2_32.dll` and `KERNEL32.dll`.
- Backend with the default-off guard built successfully:
  `b42228a1b4bf74c0c7e50ddf2fe95f1cce93887d857ce9400114a3a7b827b1b1`;
  matching PDB:
  `132c9abeb1cdb5368a0799802802f09124af0cd0427058004d5b7fbed6c4b387`.
- Relay DLL:
  `522fcbbcda33e9b943832f53cd0f36e9455410bc53696e6d8588889a7fb9261b`.
- Initial firearm bridge candidate:
  `e097e9650d76a3baf546468c327140f9e4b22442e6489d3f9945fdcdf348d7cf`.
- Melee semantic bridge candidate:
  `db4c8b344cbf486240c6d4bee2e984841ad385dffb5c896619ba0be12d530c14`.
- TOW2 protocol unit tests passed under GCC and MSVC.
- Existing DSX protocol tests still pass.
- Generalized bridge built successfully and fails closed when Toolkit CAPI is
  inactive.

A pre-candidate baseline run tested several pistols and rifles. The user found
ordinary haptics improved with the Toolkit driver but felt no adaptive trigger.
The retained UEVR log proves that run used the prior
`70f6e508+local-f37-shf-sceneview-guard` backend with standard OpenXR haptics;
the semantic Lua, relay, single-owner option and TOW2 bridge were absent. A
stale Cyberpunk bridge was running but had no TOW2 protocol support. This is a
useful baseline, not candidate validation.

The v1 candidate was staged in the experimental TOW2 deployment only. Runtime
logs prove the relay loaded, the Lua producer was enabled, and real
pistol/rifle/automatic/energy/shotgun metadata selected the expected profiles.
The user confirmed adaptive triggers work. The same run recorded four valid
native downstrokes with a Pulse Hammer while correctly keeping its trigger
profile off, providing the direct motion baseline for the next candidate.

The melee extension added protocol-level `melee_swing` and `melee_impact`
commands and preserved firearm behavior. Runtime confirmed that swing/impact
haptics were present, but the user reported late sound/haptics and unreliable
sensible target impact. Telemetry then rejected the impact producer: 29 of 30
swings generated similar native rumble at a median 153 ms after attack. The rumble producer was removed permanently. The subsequent physical-contact
candidate was also rejected: 22 `SWING_EDGE`/attack pairs had a median separation
of 2 ms, no `CONTACT` or `MELEE_IMPACT` occurred, and failed tip discovery caused
a severe per-frame logging regression. Lua did publish 22 `MELEE_SWING`
semantics, but the user felt none even though the correct `--tow2` bridge owned
UDP 6969. This localizes the haptic failure downstream of Lua production but not
yet between relay/UDP receipt, `HapticsEngine::Pulse`, and Toolkit PCM output.
Source and deployment were rolled back. The bridge now has bounded diagnostics
for UDP receipt, protocol parsing, command application, pulse scheduling, the
first PCM wait return and the first PCM write. Its off-game `--tow2-self-test`
parses and applies one right-hand semantic swing before touching the mixer; it
must prove both the logged software path and a physically felt pulse before
another gameplay candidate.

Native sound/damage timing remains TOW2's delayed animation path. It can only be
replaced safely through a validated game-owned melee event/trace endpoint.
Generic `ApplyDamage` would bypass combat rules and can double-hit when the old
native trace arrives, so it remains prohibited. A separate bounded discovery
script inventories an explicit candidate-function allowlist on the current pawn
and weapon without hooking or invocation. The off-game self-test has now
completed the software chain through the first Toolkit PCM write, and the user
felt a small right-controller pulse. This proves actuator routing, but the weak
pulse still requires later waveform/gain tuning. In a subsequent gameplay run,
17 semantic swings all reached pulse scheduling and first PCM write but the user
felt none; those pulses were only 42 ms versus the self-test's 120 ms. The
replacement uses an unmistakable speed-scaled 170 ms / 92 Hz swing rather than
asking for another actuator-path proof.

Moving-component and channel-0 sphere contact runs produced zero hits. The
object-type sphere API then failed to marshal its array; six subsequent attacks
proved positive contact arms but the channel fallback still emitted no contact.
The replacement uses bounded `LineTraceSingle` segments across hand, midpoint,
tip, current blade span and both lateral edges on channels 0 and 4. A 22-attack
run executed the trace API without failure but reported zero contacts while also
requiring reflected `bBlockingHit == true`. Removing that comparison still gave
zero contacts in seven further positive arms, rejecting the hypothesis. The
current diagnostic performs one haptic-free pawn-to-floor calibration over
trace-query indices 0–31 and retains at most six proven indices. No gameplay
damage function is called. This standalone Lua contact family is now parked and
must not receive further channel/shape permutations.

Exa research and source review found a decisive split in UEVR melee designs.
DeadIsland2VR, Remnant1-UEVR and SystemShock-UEVR detect gestures and synthesize
normal attack input; they preserve game rules but do not solve animation-timed
contact. SH2R-UEVR's full physical melee instead uses a C++ inline hook on the
game's native `MeleeWeaponEnvTrace`, derives capsule endpoints/radius from the
actual weapon mesh transform and bounds, writes accepted `FHitResult` values
back into the game-owned hit array, and has Lua prepare valid attack state before
invoking the game's melee AnimNotify virtual. Generic Unreal MeleeTrace likewise
uses interpolated mesh/socket sweeps rather than guessed controller-forward
points. Therefore TOW2's next phase is an observation-only external C++ plugin
to capture the exact `AnimNotifyState_MeleeDamage` ABI, native trace producer,
parameters/results and duplicate-suppression point. Only then may a
mesh-bounds capsule replace the delayed native trace and emit impact after the
game accepts the hit. No further Lua scene-contact probe is justified.[4][5][6]

`examples/tow2_melee_observer` implements the first observation-only candidate.
It fails closed on executable filename, PE timestamp, image size, seven native
signatures and four call targets. Five register-only SafetyHook mid-hooks observe
processor entry, trace pre/post and native acceptance pre/post; they never alter
the context. Filtered reflected notify callbacks always continue into the game.
A fixed 4,096-record buffer keeps callback work bounded, while an offline
validator confirms the analyzed executable and call graph. The Release DLL/PDB
build succeeds. Its approved isolated deployment adds only the observer DLL and
parks the rejected `09_tow2_controller_contact_diagnostics.lua`; swing, relay
and pass-through weapon-notify behavior remain unchanged.

The first runtime completed with normal shutdown and 630 contiguous records:
210 processor/trace-pre/trace-post triplets on one game thread, no dropped
records, and one caller at RVA `0x01339DBE`. The native producer returned 48
non-empty arrays containing 1–5 results and 162 empty calls across 13 trace
windows. Windows lasted 159–249 ms and followed attacks by 877 ms median in the
normal group or 476 ms in the faster group, directly proving animation-delayed
collision. Inherited reflected notify callbacks never ran. Valid vtable entries
identify a float-argument native override at RVA `0x025A856C` and NotifyEnd RVA
`0x025A84A8`; the scanner's later entries crossed the base-vtable boundary and
are rejected. The
selected actor-accept call never ran, so acceptance/damage ownership remains
unproven and impact stays fail-closed.

Phase A.2's thirteen validated register-only hooks produced 2,025 contiguous
records: 502 processor/trace triplets, 405 hit iterations, ten geometry branches,
ten actor branches and twenty common dispatches, with both known callers live.
Runtime begin/end pairing corrects RVA `0x025A856C` to native NotifyBegin, whose
float is total duration; eleven player swings reached it after 875–979 ms
normally or 485 ms for the final faster combo. Six selected geometry, two missed
and three selected actors, though eleven armed swings prevent mapping the
requested five labels exactly.

All actor targets entered the game-owned `+0x6D0` deduplication array. The old
helper returned false ten times and event `0x22` never ran. Both accepted branches
converge on call RVA `0x06D302A4` after the game's `APPLY DAMAGE` diagnostic. Its
target builds native damage, emits real `EWeaponEvent::MeleeImpact` value `0x21`
at call RVA `0x06D2DEF3`, and returned 20, 65 or 250 in this run. Result stride
`0xF8` and actor offset `+0xB8` are proven, but component layout remains blocked.
Future work must replace trace producer RVA `0x06D2EF80` for both callers and
preserve filters/dedup/damage; immediate damage additionally requires safe current
NotifyBegin context. Replacement and impact feedback remain disabled.

An unintended repeat A.2 run added 713 contiguous records across nine player
NotifyBegin windows: three misses and six geometry accepts. Phase A.3 then added
real MeleeImpact `0x21` observation and bounded layout snapshots. Its only runtime
was rejected after the user reported loss of all melee haptics and sound. The run
captured 772 stable contiguous records and proved `FHitResult` physical material,
actor and component offsets `+0xB0`, `+0xB8` and `+0xD8`, respectively. It also
captured eight real MeleeImpact events. However, the Lua gesture producer emitted
zero `ATTACK`/`MELEE_SWING` semantics, explaining absent controlled PCM but not
the sound regression. A.3 DLL `787468d...` was removed, A.2 was not restored, and
no observer is approved for normal play.

Chronology then exposed the physical-swing failure: a manual Start/Back menu
latch stayed true for 86 seconds across every player test, so Lua emitted no
attack, trigger pulse, sound initiation or swing semantic. The maintained swing
script replaces it with live UI/aim state plus a 250 ms close debounce and emits
a bounded `TOW2.PhysicalMelee` commit carrying the validated live weapon pointer.

The isolated Phase-B candidate uses one exact-image hook at native trace producer
`0x06D2EF80`. It samples the permanently attached weapon component's actual UE5
`ComponentToWorld` around Lua commit and replays fixed physical transform segments
through TOW2's original player Trip Blade trace. All NPC, wrong-weapon, stale,
unexpected-shape or unresolved calls remain original. It has no observer,
NotifyBegin, MeleeImpact, damage, sound or haptics hook and intentionally retains
the animation delay. Candidate 3 (`66125658...`) loaded safely and proved the
menu fix with eleven native attacks, eleven swing semantics and 95 animation
events, but all eleven physical episodes had zero transform samples. It therefore
failed open to original collision and cannot validate Phase B. Candidate 4
received the exact proven Lua mesh, but all 42 context attempts failed because
both reflected `ComponentToWorld` and guessed `K2_GetComponentTransform` were
absent. Twelve attacks and 98 animation events remained fail-open/original.
Candidate 4 is rejected and removed. Offline inspection identified
`K2_GetComponentToWorld`, but candidate 5 and its build artifacts were deleted at
the user's request; no corrected candidate is retained or approved. The active
profile retains only the proven menu guard/native attack/swing haptics. Phase-B
collision and exact-montage acceleration remain blocked.

The earlier 25 attacks all scheduled 170 ms pulses and reached Toolkit PCM; the
user confirmed improved swing feedback. The six instrumented attacks applied
and restored `Melee.AnimSpeed=4.0`, but normal native markers remained delayed
and tightly spaced attacks produced overlapping sequences. The scalar gave no
reliable visible benefit and is removed.

Static executable analysis located TOW2's native contact/damage processor at RVA
`0x06D2F758` and its direct callers. Runtime reflection confirmed
`Arkansas.Weapon.OnAnimNotifyEvent(EAnimNotify, FName)` on the Trip Blade. Static
UE enumerator recovery maps observed values 2/4/5/6/7/8/9 to melee input,
action-finished, recoil and damage-type markers; `EWeaponEvent::MeleeImpact` is
separately value 33 and was not observed through this function. Normal markers
remained around 560–610 ms, 780–830 ms, 1.0–1.13 s and 2.0–2.3 s. The observer
always continues into the original function and now filters NPC weapons.
Contact-owned native damage remains blocked until the delayed trace can be
safely replaced.

# Felt-effect and detection findings (2026-08 tuning)

A run of hands-on tuning resolved the long-standing "weak pulse" thread above
into durable, reusable rules. These generalize to any game built on this path;
the process is captured in
[Adding PSVR2 haptics to a new game](../playbooks/psvr2-haptics-integration.md).

- **A single grip pulse is not felt — use a sustained rhythm.** On the Sense
  *grip* actuator a one-shot PCM `Pulse` (protocol `shot` / `melee_*`) is written
  and confirmed in the bridge log (`pcm_available=true`, `pulse_scheduled`,
  `pcm_write`) yet is barely perceptible even at amplitude 1.0. The sustained
  rhythm mechanism (`SetRhythm`, driven by `rhythm` / `automatic_fire`) is
  strongly felt. Amplitude is not the lever near 1.0; rhythm-vs-pulse and rate
  (Hz, lower = chunkier) / burst length (ms) are. Firing effects were therefore
  re-routed: semi-automatic and charge shots emit a **short `automatic_fire`
  burst** instead of `shot`; the melee swing emits a short `rhythm` …
  `rhythm_stop` window (`rhythm`, not `automatic_fire`, so the trigger is left
  alone during a swing).

- **Rumble-gated detection misses whole weapon classes.** "Native rumble while R2
  held" never sees **hold-to-charge** weapons (fire on trigger *release*) or
  **near-silent energy/"Goo" weapons** (motors barely move). Confirmed dead in
  testing: the shock rifle (`Weapon_SR_Damage`) and biomass pistols
  (`Weapon_PI_Goo_*`). Fix: **trigger-release-edge detection** — track R2
  press/hold/release plus a per-press count of rumble shots; on release, if a
  semi-auto weapon fired zero rumble shots that press, emit one burst scaled by
  charge time. Weapons that already worked are unaffected (their rumble shots
  suppress the release shot).

- **Continuous energy beams need their own profile.** TOW2 "Spinbeam" specials
  (`Weapon_SPCL_Spinbeam_*`, e.g. Zyrinium Ray) are held continuous beams that
  fall through generic keywords; a dedicated **beam** profile on the
  `automatic_fire` path (high rate, low amplitude) gives a felt sustained tingle.

- **Weapon identity + a diagnostic op.** Classify from the reflected
  `WeaponCategory.TagName` first, then keyword the full name, then a generic
  fallback; log the `source`. Per-weapon overrides persist keyed on the **class
  full-name path** (stable across sessions/instances). A diagnostic `weapon`
  protocol op mirrors the equipped-weapon identity into `tow2_bridge.log` so the
  game-side classification can be correlated with bridge activity. This
  identity logging is what made the undetected/mis-classified weapons visible.

- **In-game tuning editor.** The producer exposes a UEVR script panel with
  per-profile sliders (grip amplitude, rhythm/burst rate + length, trigger
  position/strength, slope), plain-language hover tooltips, a "test on Sense"
  button, and JSON save/load, so weapons are tuned live without editing Lua.

- **Melee swing restored as felt rhythm.** After the melee saga above, basic
  swing haptics are re-enabled via the rhythm path (delayed native-attack timing
  accepted); the observation-only discovery/notify diagnostic scripts remain
  disabled for normal play.

Reference state (2026-08): bridge `psvr2_toolkit_dsx_bridge.exe --tow2` sha256
`994f692c…` (adds the `weapon` diagnostic op; earlier ops unchanged); producer
`07_tow2_psvr2_haptics.lua` still per-weapon tuning (latest session
`075a7233…`). Archived revisions with rollback copies and per-change hashes:
`F:\ai\GITHUB\UEVR-AFW-JOEY\safety\tow2-psvr2-auto-nuance-candidate1-20260816-105352\`
(`rev2…rev8`).

# Related reference knowledge

- [Adding PSVR2 haptics to a new game (process)](../playbooks/psvr2-haptics-integration.md)
- [CyberpunkVR Port PSVR2 architecture and melee behavior](../projects/cyberpunk-vr-port.md)
- [PSVR2Toolkit actuator and bridge boundary](../projects/psvr2toolkit.md)
- [TOW2 working game state](../games/outer-worlds-2.md)

# Citations

[1] [Cyberpunk PSVR2 OKF runbook](https://github.com/satyaloka93/cyberpunk-vr-port/blob/psvr2-tweaks/.okf/operations/psvr2-adaptive-triggers.md)
[2] [PSVR2Toolkit community bridge branch](https://github.com/satyaloka93/PSVR2Toolkit/tree/cyberpunk-dsx-bridge)
[3] [UEVR AFW compatibility project](https://github.com/satyaloka93/UEVR-AFW-Compat)
[4] [SH2R-UEVR native melee hook](https://github.com/praydog/SH2R-UEVR/blob/master/src/Plugin.cpp)
[5] [SH2R-UEVR physical-swing AnimNotify integration](https://github.com/praydog/SH2R-UEVR/blob/master/scripts/camera.lua)
[6] [MeleeTrace mesh/socket interpolation](https://github.com/rlewicki/MeleeTrace)
