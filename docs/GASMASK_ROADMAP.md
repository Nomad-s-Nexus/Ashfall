# Gas Mask Feature Roadmap (deferred GMW ports)

**Last updated:** 2026-07-03. Companion to `GMW_GAP_ANALYSIS.md` (full analysis + where every
asset lives). This file tracks what is DONE vs what was deliberately deferred, so the next
session can pick an item without re-analyzing GMW.

## Done (2026-07-03)

- **METRO EXODUS foley wired** (WAVs copied into `Sound\FalloutAnomaly\mask\` in deploy + live,
  self-contained - no dependency on the source sound mod staying enabled):
  - mask draw / withdraw on every worn-state transition, male/female variants (auto-equip included)
  - filter replace: full take on manual replace, hurried "fast" take on auto-replace, alternating
    between the two recordings
  - choking one-shots (male/female x masked/unmasked) on a randomized ~6-11 s throttle while choking
- **Choking health damage** (INI `[Choking]`): breathing rad weather while exposed damages HEALTH -
  full `HealthPerSecond` (default 1.5) with no mask, `* FailedMaskFactor` (default 0.5) when masked
  but the filter is dead / mask broken (that case also keeps the existing rads choke). Sheltered,
  powered Pulowski shelters, and full power armor are exempt. One corner notify per episode.
- **Low-filter visor fog**: HTML condensation layer (`#visorFog` in radhud.html) driven by the
  existing `updateFilter` push - builds below 30% filter charge (max at 0%), fixed 55% haze when
  the mask is broken, cleared while unmasked.

## Deferred (in rough value order)

1. **Breathing loop while masked.** Loops (`GasMask_Breathing_Male_01/02`, `Female_01/02/03`,
   `Neutral`) exist only in the DISABLED GMW `Main.BA2` (`sound\fx\FO2287\Breathing\*.xwm`) -
   extract with BSA Browser (`Tools\KnownModTools\BSA BROWSER\bsab.exe`) into our tree. Needs a
   looping handle managed by shelter/sprint state (start on mask-on, intensify while sprinting -
   `Sprinting\Male_Sprint_Mask1.xwm` etc., stop on mask-off). Coughing-after-exposure one-shots
   also in the BA2.
2. **Glass-crack audio + crack overlays.** 7 crack sounds (`Cracking\GlassCrack_0*.xwm`, BA2) to
   play in `OnPlayerHit` when condition drops; 6-stage crack DDS overlays
   (`textures\FO2287\Overlay\Generic\Stage1-6`, Textures.BA2) to convert to PNG layers in
   radhud.html keyed off condition %.
3. **Visor splatter + wipe action.** Rain refraction (6 DDS), snow, mud (explosion within ~700
   units), blood (on hit), dust layers + a wipe consumable/hotkey with smear textures and
   `gasmaskwipe.wav` (already copied). All PrismaUI layers + C++ state we mostly already track.
4. **Protection tiers.** Bandana class = partial (25%) protection middle tier; classify vanilla
   headgear (hazmat, flight helmets, raider masks). Our mouth-slot scan stays the base; add a
   keyword/formlist classification (ESP work via gen_esl.py).
5. **Weapon-scaled condition damage.** GMW: explosive -20 / high -10 / med -4 / low -2 / generic
   -1 per hit vs our flat `DamagePerHit=6`. Needs hit-source classification in the hit sink.
6. **Filter economy.** Used-filter item returned on replace, weaker makeshift filter, 3-rank
   filter-crafting perk line + chem recipes (ESP + gen_esl.py).
7. **Camera imagespace shift while masked / geiger widget / NPC choke damage / muffled player
   voice / mask leveled-list injection.** Low priority; see gap analysis section 6.

## Weather-warning cues (partial)

`gasmaskweatherwarning.xwm` / `gasmaskweathersafewarning.xwm` exist in the BA2. Our storm system
already has its own warning audio + HUD banners, so these are only useful for VANILLA radstorm
warnings if wanted - currently covered by `CheckRadstormWarning()`.
