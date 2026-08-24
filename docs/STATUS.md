# Rad Mod - Release Status

**Last updated:** 2026-08-18

Single authoritative list of what is not finished. Historical detail lives in `CHANGELOG.md` and
`ISSUES_FIXED.md`; deferred GMW work lives in `GASMASK_ROADMAP.md`.

Legend: **VERIFY** = implementation exists but still needs live Fallout 4 confirmation.
**OPEN** = active engineering issue. **DEFERRED** = intentionally outside the current release scope.
**CLEANUP** = internal maintenance only.

---

## BLOCKER

No known advertised feature is currently missing from source. The remaining release gates are
verification and the engineering issues below.

---

## VERIFY - implementation complete, live confirmation still required

### 1. Native lethal-storm camera shake - GitHub #8

The dead Papyrus `SetStage` route was replaced by native `ShakeCamera`. `UpdateStormShake()` drives
an initial burst plus sustained rumble and tears the counters down when the storm leaves Active.
The path is OG-only and guarded because the 4 Hz engine hook is a worker thread.

Test Ash Fallout and Rad Firestorm in-game. Confirm the shake starts in Active, sustains, stops
cleanly, and does not CTD around interiors/cell transitions.

### 2. Native firestorm distant nuke - GitHub #9

The old placement/stage route was replaced with the native `CreateExplosionAtLocation` detonation
path. `_stormNukeFired` limits it to one strike per firestorm.

Test repeated Rad Firestorms outdoors. Confirm exactly one horizon-scale mushroom-cloud strike per
storm, no duplicates, no CTD around cell transitions, and a successful
`[Storm/Nuke] distant firestorm strike detonated=true` log line.

### 3. Storm siren live confirmation

Siren instrumentation and the debug-shortened storm lead are implemented. A release test should
still observe `[StormCue] Siren started` in a live log and confirm the cue stops/fades normally.

---

## OPEN - active engineering

### 4. Storm dome has a hidden KORs light dependency - GitHub #19

The core ESP/README previously claimed no other plugin dependency, but the native dome path still
resolves `ArenaLight01` (`0x025334`) and `ArenaLight02` (`0x02626D`) from
`KORsAnomaly Stripped.esp`. The preferred fix is to add equivalent scriptless LIGH records to
`RadiationOverhaul.esp`, resolve them locally, and verify the complete dome visual with KORs absent.

### 5. Worker-thread engine-call audit - GitHub #20

Runtime probing established that the 4 Hz `PlayerCharacter::Update` hook is a worker thread and
F4SE `AddTask` is not a reliable main-thread marshal in this runtime. Some current comments and
call classifications still assume main-thread execution. Audit every engine-facing operation,
document proven-safe/guarded worker calls, and establish a verified dispatcher for anything that
actually requires game-main execution.

### 6. Larger roadmap systems

Not current release blockers: NPC shelter-seeking AI, post-storm loot loop, later radiation pillars
(disease/gear durability/nuclear-blast expansion), and a Geiger path tied to pre-resistance exposure.
Open focused issues only when an item is selected for implementation.

---

## RESOLVED / CLOSED 2026-08-18

- **Dust storm airborne particles (#10):** `UpdateDustMotes()` continuously re-applies a
  camera-attached heavy-dust ArtObject during exposed Dust Storm Warning/Active phases. Nuka-World
  art is used when available with a vanilla fallback.
- **Filter drain across sleep/wait/fast-travel (#12):** merged PR #16. Time jumps now drain filters
  using the pre-jump mask/exposure state and live `timescale`; AutoReplace can consume multiple
  spares. The same PR also blocks duplicate/free tissue healing when loading later saves.
- **RadAway false tissue-heal trigger (#13):** the earlier inventory-decrement approach was removed
  because it could not distinguish use from dropping, selling, or transfer. RadAway and chelation
  item-triggered tissue cures are now disabled fail-closed pending a verified source-identifying
  item-use adapter.
- **Fix Stats vs permanent scar END (#14):** scar END is added after the stage-penalty suspension,
  so Fix Stats cannot clear permanent scarring.
- **Firestorm missile flyover (#11):** closed as not planned. The actual distant detonation exists;
  the remaining flyover was cosmetic and the investigated projectile-launch path carried CTD risk.
- **GMW umbrella (#15):** closed as not planned for the current release. Remaining parity ideas stay
  in `GASMASK_ROADMAP.md` and should become focused feature issues if selected.
- **Release hardening (PR #17):** unsupported runtimes fail closed, stale thread probes were removed,
  README/layout documentation was refreshed, and legacy threshold fields are clearly marked as
  non-runtime tuning.

---

## DEFERRED / ACCEPTED

- **GMW parity work:** breathing/sprint loops, crack overlays/audio, visor splatter/wipe, protection
  tiers, weapon-scaled mask damage, expanded filter economy, and lower-priority extras remain
  deliberately deferred.
- **Weather transition snap:** a storm beginning during an existing transition can cosmetically
  reset the blend once. This remains an accepted trade-off unless it becomes visibly disruptive.
- **New Gen:** not supported. The current plugin is intentionally OG / Fallout 4 1.10.163 only.

---

## CLEANUP - internal only

1. `data/items.json`, `data/perks.json`, and `data/zones.json` are source-tree design leftovers and
   are not consumed by the runtime loader.
2. `thresholds.json` still contains legacy non-runtime tuning fields. Their comments now state this
   explicitly, so removal is optional cleanup rather than a correctness fix.
3. IMAD `0x830` is an orphaned old radiation-tint record; the HTML HUD overlay replaced it.
4. The ESP/Spriggit build pipeline still needs an end-to-end verification pass and FormID-map audit.
5. The legacy `RadStormFX.psc/.pex` path is no longer authoritative. Its old Papyrus shake logic has
   real defects if revived; keep native storm FX authoritative unless the script is deliberately
   repaired and recompiled.
