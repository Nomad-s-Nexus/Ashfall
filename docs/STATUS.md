# Rad Mod — Release Status

**Last updated:** 2026-07-19

Single authoritative list of what is NOT finished. Anything not listed here is considered done.
Historical detail lives in `CHANGELOG.md` and `ISSUES_FIXED.md`; superseded planning docs are in
`archive/`.

Legend: **BLOCKER** = advertised to players but missing/broken. **OPEN** = incomplete, not
advertised. **DECISION** = waiting on the author. **CLEANUP** = internal only.

---

## BLOCKER — advertised but not delivered

### 1. Ghoulification capstone does not exist
`README.md` lists it as a headline feature and `thresholds.json` still names stage 9
"Ghoulification", but the player-transformation code was removed and nothing replaced it. A player
reaching stage 9 gets a banner promising a transformation that never occurs.
Either restore the feature or remove it from `README.md` + the stage label.
Refs: `src/RadEngine_Internal.h:127`, `deploy/F4SE/Plugins/FalloutAnomaly/thresholds.json:125`

### 2. Storm dome renders black
The marquee storm visual. Earlier `SkyrimCloudLUT.dds` diagnosis was retracted. Three untested
candidates remain: a loose sky-mod DDS override, the CommonwealthDusty imagespace crushing the
emissive, and backface culling. Refs: `CHANGELOG.md:6`

### 3. New Gen support is unverified
`README.md` advertises "(OG and New Gen)". The `PlayerCharacter::Update` vtable index the entire
4 Hz engine hangs off was never verified on NG — wrong index means silent no-op or crash. Either
verify it or drop the NG claim from `README.md`. Refs: `CODE_REVIEW_2026-07-03.md:49`

### 4. Storm screen FX dead on pre-existing saves
`EnsureQuestRunning` is a no-op, so the storm-FX quest never starts on any save predating it. This
is broader than the tracked nuke/shake issue (bd `f4se_og-dpd`): *no* stage-driven storm FX fires.
New games only. Refs: `src/RadiationEngine.cpp:721-724`

### 5. Gas-mask put-on / take-off fade is a stub
`CallFadeOutGame` is an empty body (Papyrus VM dispatch off the worker thread CTD'd). Everything
downstream is dead code, and the documented replacement (a JS black-flash div) was never built.
Refs: `src/RadEngine_Mask.cpp:322`, callers `src/RadiationEngine.cpp:307,320`

---

## OPEN — incomplete, not advertised

### 6. Dust storms have no airborne dust
Fog only. Needs a clean-room weather-particle BGEM+DDS plus a camera-dust `VisualEffect`. FormIDs
`0x845`/`0x846` are reserved but empty. Refs: `CHANGELOG.md:937,1159`, `PROGRESS.md:118-121`

### 7. Storm siren never confirmed to fire in-game
Fixed and evidence-backed, but `[StormCue] Siren started` has not yet been observed in a live log.
Test: force a storm (debug lead is now 10 s) and check the log. Refs: `ISSUES_FIXED.md`

### 8. Firestorm missile flyover not implemented
Only the static distant explosion exists; `Projectile::Launch` was explicitly left out of scope.
Refs: `src/RadiationEngine.cpp:736-737`

### 9. Gas-mask filter ignores time-jumps
Sleeping or fast-travelling does not drain the filter, so the filter economy can be skipped by
waiting. Refs: `NEEDS_INPUT.md`

### 10. Roadmap features not started
NPC flee-to-shelter AI, post-storm loot loop, Pillars II-IV (diseases, gear durability, nuclear
blasts), Geiger tied to pre-resistance rads, siren fade-out. Refs: `PROGRESS.md:81-86`

### 11. GMW gas-mask feature port
7 items across three proposed tiers, decision pending. Refs: `GASMASK_ROADMAP.md:24-45`,
`GMW_GAP_ANALYSIS.md`, bd `f4se_og-9xs`

---

## DECISION — waiting on the author

### 12. RadAway tissue-heal false-fires
Any rad drop over ~15 in one tick reads as a doctor visit and can wipe permanent scarring (RadAway
plus a stimpak in the same tick will do it). Fix is known — switch to inventory-count-delta
detection, the method chelation already uses. Design/feel call, not a bug.
Refs: `NEEDS_INPUT.md`, `src/RadEngine_Tissue.cpp:177`

### 13. Fix-Stats vs permanent scar penalty
Fix-Stats currently suspends the permanent-scar END penalty indefinitely. Author's stated intent is
that it should NOT — not yet implemented. Refs: bd `f4se_og-9kp`

### 14. Weather transition can snap once
A storm starting mid-transition resets blend pct and cosmetically jumps. Documented as an accepted
trade-off — listed so shipping it stays a conscious choice. Refs: `src/RadEngine_Weather.cpp:145-151`

---

## CLEANUP — internal only

15. **Three config files ship but are never read** — `data/items.json`, `data/perks.json`,
    `data/zones.json` are populated but only `thresholds.json` loads. Not in `deploy/`, so they
    mislead contributors, not players. Refs: `src/RadiationConfig.cpp:10-15`
16. **`thresholds.json` carries dead tuning fields** — `endPenalty`/`strPenalty` and old
    limb/health/fast-travel/radaway fields are ignored; real penalties are hardcoded in
    `kStagePenalties`. Modders editing them see no effect. Refs: `src/RadiationConfig.h:3-8`
17. **Debug thread probe still in the shipping hook** — marked "TEMP, remove after diagnosis",
    fires two warn-level lines every session. Refs: `src/Hooks.cpp:48-64`
18. **Release package contains build junk** — `deploy/F4SE.zip`, a `CloudArena04.nif.20260717.bak`,
    plus `fallout-4-image-3.jpg` and the `GasMask/` / `GasMaskOverlays/` PSD trees at repo root.
19. **README describes files that do not exist** — a `tooling/` directory and an optional
    `RadOverhaul_GhoulRace.esp`; it also omits `RadOverhaulSurPatch.esp`, which *is* shipped.
20. **Orphaned ESP record** — IMAD `0x830` (old radiation tint) is never resolved or triggered;
    replaced by the HTML overlay. Refs: `src/RadEngine_Tissue.cpp:58`
21. **ESP build pipeline partly unverified** — mechanics not re-verified end-to-end, the `spriggit`
    CLI wrapper is broken for this tree (bypass documented), step-3 source path unconfirmed, and the
    FormID map omits `0x802-0x804`, `0x844`, `0x880-0x884`. Refs: `ESP_BUILD_PIPELINE.md`
22. **Dead Papyrus path has real bugs if revived** — beyond bd `f4se_og-whl`: `_StartShake`'s
    rumble-loop `StartTimer` is unreachable, two empty `If/EndIf` bodies, and `DebugNotify` defaults
    to `true` (would spam on-screen debug text at players).
    Refs: `deploy/Scripts/Source/User/RadStormFX.psc`
