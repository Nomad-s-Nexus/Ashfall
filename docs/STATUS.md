# Rad Mod — Release Status

**Last updated:** 2026-07-19

Single authoritative list of what is NOT finished. Anything not listed here is considered done.
Historical detail lives in `CHANGELOG.md` and `ISSUES_FIXED.md`; superseded planning docs are in
`archive/`.

Legend: **BLOCKER** = advertised to players but missing/broken. **OPEN** = incomplete, not
advertised. **DECISION** = waiting on the author. **CLEANUP** = internal only.

---

## BLOCKER — advertised but not delivered

### ~~1. Ghoulification capstone does not exist~~ — RESOLVED 2026-07-19
Author's call: drop the feature rather than restore it. The `README.md` bullet (and its reference to
a non-existent `RadOverhaul_GhoulRace.esp`) is removed, and the top stage is renamed
**"Systemic Failure"** in both `data/thresholds.json` and the deployed copy — matching that stage's
own existing description, "Complete systemic failure - death near". Nothing in code keyed on the
string, so this is label-only; stage count, ranges and penalties are unchanged.

### 2. Storm dome renders black — FIXES SHIPPED, awaiting visual confirmation
Not "root cause unknown" as this file previously said. Fixes landed after the 07-15 changelog entry
and were recorded in `docs/Prisma/CHANGELOG.md` + commit messages rather than here, which is why
this doc went stale:
- **Arena light restored** (`fe6e2aacf`). An earlier pass removed it as pointless; in fact the cloud
  layers ship with own-emit OFF and effect-lighting ON, so with no light they render as a dark blob.
  Placed at the NIF's `TopLightNode` height, aimed down, radius 20000.
- **Dome animation force-started** (`fe6e2aacf`). A natively-placed ACTI ref leaves the clouds'
  shader float stuck at its initial value; every layer's controller sequence is now activated once
  its 3D attaches.
- **Black-hole apex card** (`147ed6689`, 07-17). NASA Goddard/Schnittman texture, card switched from
  `BSLightingShaderProperty` to an emissive shader.

Remaining: this is a visual judgement only the author can make in-game. Confirm the dome reads
correctly, then close this item. Note the 07-15 changelog entry in this folder is stale and still
describes the bug as open.

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
