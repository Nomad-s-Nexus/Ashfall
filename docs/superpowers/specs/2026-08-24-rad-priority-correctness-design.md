# Rad Overhaul priority correctness fixes

## Scope

Fix the four high-priority defects identified in the 2026-08-24 audit:

1. The native storm dome remains at its spawn position.
2. Any mouth-slot armor is treated as a gas mask.
3. Inventory transfers of Chelation or RadAway can be mistaken for consumption.
4. Queued load-sensitive work can execute after a newer load starts.

This change does not alter storm balance, mask durability, shelter policy, or unrelated scheduling behavior.

## Storm dome follow

Store each native dome reference with its fixed vertical offset from the player. While the dome is active and the player is exposed, move live references to the current player position at a one-second cadence. The existing spawn, cleanup, and animation paths remain responsible for reference lifetime and controller activation.

The movement policy is pure and tested independently: a player position plus a layer offset produces that layer's next position. Invalid or expired handles are skipped. Entering shelter still clears the dome.

## Gas-mask classification

`RadiationOverhaul.esp` owns a new `KYWD`, `FARO_GasMask`. Native mask recognition requires both this keyword and the mouth biped slot. The keyword keeps a third-party compatibility patch explicit instead of treating cosmetic mouth armor as protective equipment.

Before writing the deployed plugin, back it up. Use FO4RecordEditor to create the keyword, apply it only to supported mask records, save, then read every edited record and run the plugin checker. Do not broadly patch all mouth-slot armors.

## Cure detection

Remove the inventory-count-delta rule for Chelation and RadAway. The native implementation will arm cure handling only after a verified player item-use or matching magic-effect application event. The event must identify the player and the relevant ingestible before it changes tissue state. Inventory movement, selling, dropping, and container transfer never arm a cure.

If the supported CommonLib/F4SE surface cannot provide an event that identifies the consumed source item, the implementation fails closed: it does not cure tissue until a correctly identified native hook is validated. It must not retain the unsafe count-delta fallback.

## Load generation

Introduce a single atomic load-generation counter shared by post-load, new-game, and HUD-wait work. `kPreLoadGame` advances it before new work starts. Every queued load-sensitive lambda captures its generation and returns without touching engine state when it no longer matches.

## Tests and verification

Add a focused native test target for pure policies. Tests cover dome coordinate updates, keyword-plus-slot mask classification, a cure event versus inventory movement, and stale generation rejection. Each test is introduced red first.

Verification consists of the focused native test binary, a cross-build with the supported CommonLib authority, static source checks for removal of count-delta cure paths, FO4RecordEditor plugin read-back and checker output, and an explicit Windows/OG Fallout 4 smoke test. The smoke test is separate from build evidence.
