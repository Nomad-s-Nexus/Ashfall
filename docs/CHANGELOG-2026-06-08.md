# Radiation Overhaul - Changelog (2026-06-08)

A focused session on the Radiation Overhaul plugin (`FalloutAnomalyRadiation.dll`) plus its HUD (`radhud.html`). Storm pacing and warnings were retuned, several player-facing bugs were fixed, two code-review passes hardened the plugin, and a "rest heals tissue" gap was closed. All changes built and deployed to `mods\Fallen World Rad Overhaul\`.

Files touched: `src\RadiationEngine.cpp` / `.h`, `src\RadHud.cpp`, `src\Hooks.cpp`, `src\RadiationConfig.cpp`, and `PrismaUI_F4\views\Interface\FalloutAnomaly\radhud.html`.

## Storm HUD banner
- The storm alert banner ("Toxic Sulfur Storm", etc.) would never disappear, lingering through the whole storm, into the aftermath, across a save load, and onto the main menu. Root cause was CSS: each theme class carried an `infinite` animation that pinned `opacity: 1`, so removing the `on` class could not hide it. Hiding now clears the theme/animation class too, so the banner shows for about 10 seconds per phase change and then fades on its own.

## Debug hotkeys
- Tissue damage was spiking on its own in roughly 150-point jumps while rads stayed low. Cause: the F11 debug "add tissue" hotkey was polled with the unreliable `GetAsyncKeyState & 0x0001` bit and false-fired during normal play. Removed the F11 tissue hotkey entirely (tissue and clear buttons still live inside the debug menu). The only hotkey now is Left Shift + F10 to open the debug menu, edge-detected from the reliable key-down bit so it cannot false-fire.

## Storm pacing and warnings
- Storms hit instantly with no real warning. The rad sky was being forced at the start of the Warning phase, so the sky changed the moment a storm was flagged. The sky force (and all damage) now happens when the storm actually hits at the Active phase, leaving the Warning phase a genuine lead-up with a normal sky plus the alert. Warning lead time increased to 90 seconds for lethal storms and 60 seconds for ambient.
- Storms ran back to back for over an hour. The schedule used a fixed game-hour gap, which elapses in minutes under a fast Hardcore timescale. Scheduling is now expressed in real-world minutes (35 to 90) and converted through the live timescale, so storm frequency is the same regardless of the modlist's timescale.
- Storm length capped under about 5 minutes end to end. The two long ambient storms had their active phase cut from 240 and 300 seconds down to 180.
- On a new game, a radiation weather was active the moment the player walked into the world. Cause: the next storm is scheduled relative to pre-war game-time, then leaving Vault 111 jumps the clock about 200 years forward, so the storm was "overdue" and fired immediately. The scheduler now reschedules instead of firing when it is overdue by more than a full gap (a large time jump).

## Indoors safety, silence, and stillness
- Storms were damaging the player indoors. Ambient (non-lethal) storms still applied half damage when sheltered. Interiors now take zero storm damage for all storm types.
- Storms no longer shake the screen or play the storm audio while the player is safely indoors. The camera shake and nuke are gated on being exposed at the storm hit (dome-only when sheltered), and the storm ambient audio plays only while exposed, fading out when the player steps inside and restarting when they step back out.
- Note: "sheltered" is still defined as being in an interior cell. Settlement buildings and shacks in the open world are exterior cells, so they do not yet count as shelter. Roof detection is a known open item.

## Audio
- The storm ambient (the firestorm "crow" audio) used to start at the Warning phase and play under a clear sky for the whole lead-up, for every storm type. It now starts when the storm hits, synced with the sky. (The 4-second fade-out at storm end from the previous session is retained.)

## Cure and tissue mechanics
- Sleeping, waiting, or a doctor visit could silently wipe all permanent (scarred) tissue. The cure system inferred a "doctor" or "chelation" cure from per-tick rads and HP swings, and the update hook pauses during sleep, so the first tick back saw a huge swing and false-fired. Permanent tissue is now cured only by the chelation compound, detected reliably from a drop in the player's inventory count of that item, not from stat deltas. RadAway still heals regular (reducible) tissue. Design note: doctors no longer cure permanent scarring, only the chelation compound does.
- Sleeping or waiting did not reduce tissue in proportion to the time spent. Tissue healing runs in the update hook, which pauses during sleep, so the skipped game-time produced no healing. Resting now bulk-heals reducible tissue at 40 per game-hour (gated on low rads, clamped to the permanent floor), applied both for in-game sleep/wait/fast-travel and across a save load. The save-load path was unified to the same rate.

## Code-review pass 1 (correctness)
- The `weather:clear` debug button could crash indoors because it called `Sky::ResetWeather()` (which dereferences the current sky weather and crashes in interiors). It now releases the weather override only.
- The lethal-storm peak burst was killing companions, settlers, and quest NPCs caught outside. It now spares essential and protected actors (companions bleed out rather than die) and settlers (WorkshopNPC faction). Raiders and creatures are still thinned out.
- Removed dead code: the old environmental-radiation, damage-ticking, and pain-blink paths and their helpers, plus an unused debug member. The frame-time accumulator now subtracts the tick interval instead of resetting to zero (no slow drift) and clamps after a stall (no catch-up burst).

## Code-review pass 2 (hardening)
- Added an out-of-bounds guard so any non-idle storm phase with an invalid storm type resets to idle instead of reading past the storm table.
- The peak burst now uses a fired-flag with a "less than or equal" check instead of exact-tick equality, so it cannot be skipped if the peak tick ever lands inside the firestorm roll-in window.
- The config loader wraps each JSON section in a key check, so a missing key in one file no longer aborts loading the others.
