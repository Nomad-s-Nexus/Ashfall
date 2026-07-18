# Gas Masks of the Wasteland (GMW) vs Rad Mod - Gas Mask Feature Gap Analysis

**Date:** 2026-07-03. Produced by opening `Gas Masks of the Wasteland.esp` (831 records) in
FO4RecordEditor and decompiling all 112 `Metro:*` Papyrus scripts from its Main.BA2
(extraction: BSA Browser `bsab.exe`; scripts decompiled to the session scratchpad).

**Modlist context:** GMW itself is DISABLED in the Fallen World profile (Rad Mod replaces it).
The companion mod **"Gas Masks of the Wasteland - METRO EXODUS sound - Tweaked audio" is
ENABLED** and ships loose WAVs under `sound\fx\FO2287\` (choking M/F masked/unmasked, mask
draw/withdraw, filter replace x2 + fast x2, wipe). The full audio set (breathing loops M/F/neutral,
coughing, 7 glass-crack variants, repair, weather warning + safe cues, sprint breathing) plus all
overlay textures live in `Gas Masks of the Wasteland - Main.BA2` / `- Textures.BA2` and can be
extracted the same way.

---

## Rad Mod already has (parity or better)

Mask-agnostic mouth-slot detection; auto-equip in rad weather (player + NPCs, with tracked
un-equip); filter charge drain / ReplaceKey / AutoReplace / spares count; mask condition +
on-hit damage + repair kit; rad choke when filter dead or mask broken; fullscreen overlay +
filter/condition HUD widget (PrismaUI); equip fade; interior/shelter detection incl. roof raycast;
a much richer storm system than GMW's dust storms; INI settings. All C++ (no Papyrus overhead).

## Missing vs GMW

1. **Audio feedback (biggest gap; assets already installed).** Breathing loop while masked
   (M/F/neutral, presets, intensified while sprinting), choking when unprotected in hazard
   (masked/unmasked variants), coughing after exposure, draw/withdraw sounds on equip/unequip,
   filter-replace + repair sounds, glass-crack sounds on condition damage (7 variants),
   weather-warning + all-clear cues. Rad Mod's mask system is silent.
2. **Visor visual state.** Per-mask-style vignettes (visor/goggles/one-eye/sackhood/generic),
   6-stage crack overlays by condition (`textures\FO2287\Overlay\Generic\Stage1-6`), environmental
   splatter (rain refraction 1-6, snow, mud from explosions within ~700 units, blood from hits,
   dust), and a WIPE action (consumable `Metro_GearWipe` + FP/TP idle + smear textures + sound)
   that clears splatter. Ours = one static PNG. Port as PrismaUI HTML layers driven from C++.
3. **Protection tiers / classification.** GMW classifies all relevant headgear (10 own masks +
   vanilla hazmat/flight helmets/raider masks) as GasMask / Bandana (25% partial protection) /
   none, in a runtime database with PER-MASK condition tracking. Condition damage is
   weapon-tier scaled via formlists (VeryHigh -20, High -10, Med -4, Low -2, generic -1 per hit)
   vs our flat [Condition]DamagePerHit=6.
4. **Filter economy.** Replace returns a Used Filter item; Makeshift Filter (weaker); 3-rank
   filter-crafting perk line unlocked by crafting activity; chem-station recipes.
5. **Unprotected consequences.** Choking HEALTH damage when unmasked in hazard (toggle),
   camera imagespace shift while masked, low-filter vision IMAD.
6. **World/NPC extras (mostly skip - covered or low value):** radiation zones w/ target-exposure
   ramp (zones A-D presets), powered air-filtration safe rooms (`AirFilter_Power`/`FilteredAir`),
   geiger HUD widget, NPC choke damage (cloak), NPC mask stealing, muffled player voice,
   holotape settings menus (our INI/MCM equivalent), LL injection, hardcore drop-rate ESP.

## Recommended port tiers (decision pending - user chose "report only" 2026-07-03)

- **Tier 1 "immersion pack":** full audio set + visor overlays (cracks/splatter) + wipe action.
  Pure C++/PrismaUI; audio from the enabled loose-WAV mod + BA2 extraction into
  `Sound\FalloutAnomaly\`; DDS overlays converted to PNG for radhud.html.
- **Tier 2 "+ economy":** bandana partial protection, weapon-scaled condition damage,
  used/makeshift filters + recipes (ESP additions via gen_esl.py pipeline).
- **Tier 3 "full":** choking health damage, low-filter vision blur, camera shift, geiger widget,
  NPC choke.

Tracked in beads (see `bd list` RadMod GMW issue).
