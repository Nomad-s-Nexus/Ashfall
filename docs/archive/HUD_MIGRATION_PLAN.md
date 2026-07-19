# Rad Mod HUD → PrismaHUD_F4 Migration Plan

> **DONE — plan fully implemented, confirmed 2026-07-18.** `Projects/Rad Mod/src/RadHud.cpp`
> registers a dedicated `hud_toxic.html` widget (`rad_toxic`) with PrismaHUD_F4 via
> `IVPrismaHUD1::RegisterWidget` — the toxic-tissue readout has been split out and parented into
> PrismaHUD's layout as this plan describes. Kept below as the design record of how/why.

**Goal:** stop the Rad Mod from drawing its own generic vitals HUD, let **PrismaHUD_F4** own the unified HUD, and have the Rad Mod's unique **toxic** (tissue-damage) readout be picked up and **parented** into PrismaHUD's layout. The Rad Mod keeps only its gameplay overlays (gas mask, storm, popups), not the vitals bars.

---

## 1. Where things stand

**Rad Mod today** ships one monolithic PrismaUI view (`radhud.html` + `.css` + `.js`, created by `RadHud.cpp`) that bundles, all in one view:
- Vitals bars: **rads, tissue, health, AP** (`updateRad`)
- **Storm banners** (`stormPhase`)
- **Gas-mask screen overlay** (`setMask`) + **filter/condition widget** (`updateFilter`)
- **SPECIAL-penalty popup** (`showSpecialInfo`) + the **debug menu**

**PrismaHUD_F4** is a modular HUD framework (`E:\F4SE OG\Prisma\UI_Systems\PrismaHUD_F4`) with one PrismaUI view per widget — it **already has** `hud_health.html`, `hud_ap.html`, `hud_rads.html`, `hud_survival.html`, etc., driven by its own `HUDDataProvider`, and a `HUDLayoutManager` + in-game HUD editor. So **health / AP / rads are already covered by PrismaHUD's own widgets** — the Rad Mod is duplicating them.

The only vitals the Rad Mod has that PrismaHUD does **not** is the **tissue / permanent-tissue "toxic" pool** (PILLAR I — persistent radiation damage). That is the piece to hand to PrismaHUD.

---

## 2. The integration mechanism (from `PrismaHUD_F4_API.h`)

PrismaHUD exposes a positioning API, not a data feed:

```cpp
auto* hud = PRISMA_HUD_API::RequestPluginAPI();           // GetProcAddress on PrismaHUD_F4.dll
hud->RegisterWidget(myView, { "rad_toxic", "Toxicity", x, y, w, h });
```
- The owning plugin still **creates its own PrismaUI view and pushes its own data** into it.
- PrismaHUD only **manages the widget's position/size** (parents it into the shared layout + editor).
- The widget's HTML must expose **`window.setHUDPosition({x,y,w,h})`**; PrismaHUD calls it with the saved layout.
- Timing: `RegisterWidget()` on **kPostLoadGame / kNewGame, after CreateView()**. Re-registering the same `id` with a new handle replaces the old entry (handles reloads).

So "parented to PrismaHUD" = the Rad Mod's toxic view is registered as a PrismaHUD widget; PrismaHUD positions it alongside health/AP/rads in one editable HUD.

---

## 3. Target state

- **PrismaHUD_F4** renders **health, AP, rads** (its existing widgets). The Rad Mod no longer draws them.
- The Rad Mod renders a small **toxic** widget (tissue + permanent-tissue bar) and **registers it with PrismaHUD** so it lives in the same layout/editor.
- The Rad Mod keeps its **gameplay/overlay UI in its own view** (these are not positionable vitals): the fullscreen **gas-mask overlay**, the **storm banners**, the **SPECIAL popup**, and the **debug menu**. The **filter/condition widget** is a positionable readout and *may* also be registered with PrismaHUD (optional — see §6).

---

## 4. Two ways to do the toxic hand-off (pick one)

**Option A — Rad Mod owns the toxic widget, PrismaHUD positions it (RECOMMENDED).**
Uses the existing API with zero edits to PrismaHUD.
- Rad Mod creates a dedicated `hud_toxic.html` view (just the tissue bar + icon + value), pushes tissue data to it (a new `InteropCall` like `updateToxic`), and calls `RegisterWidget(toxicView, {"rad_toxic","Toxicity",...})`.
- Clean separation: Rad Mod owns the data + rendering, PrismaHUD owns the position.

**Option B — PrismaHUD reads the toxic data itself.**
PrismaHUD's `HUDDataProvider` `LookupForm`s the Rad Mod's tissue global (`RadOverhaul_Tissue` GLOB `0x850` + permanent `0x851` in `RadiationOverhaul.esp`) and renders a native `hud_toxic` widget.
- "Most picked-up": the Rad Mod drops **all** HUD and just exposes its globals (which already exist).
- Cost: couples PrismaHUD to the Rad Mod's FormIDs and requires editing PrismaHUD. Gracefully no-ops if the global isn't found.

**Recommendation: Option A.** It is the supported, decoupled path and matches how PrismaHUD expects third-party widgets to integrate. Use Option B only if you want the Rad Mod to carry no PrismaUI views at all.

---

## 5. Step-by-step (Option A)

1. **Copy the API header** `PrismaHUD_F4_API.h` into the Rad Mod `src/` (runtime `GetProcAddress`, no link dependency — same pattern as `PrismaUI_F4_API.h` already in the Rad Mod).
2. **Split the toxic out of `radhud.html`.** Create `hud_toxic.html/.css/.js` containing only the tissue bar (current `tbar` + `tperm` + icon + value). Add `window.setHUDPosition(p)` that sets the root div's `left/top/width/height`. Add `window.updateToxic(json)` that sets the bar widths.
3. **RadHud.cpp:** create the toxic view at kPostLoadGame (a second `CreateView("Interface/FalloutAnomaly/hud_toxic.html", ...)`), store its handle, and in its OnDomReady acquire the PrismaHUD API and call `RegisterWidget(toxicView, {"rad_toxic","Toxicity", defaultX, defaultY, w, h})`. Add `PushToxic(tissue, permanentTissue)` → `InteropCall(toxicView,"updateToxic",...)`.
4. **Remove the generic vitals** (rads, health, AP) from the Rad Mod's HUD: delete those rows from `radhud.html/.css/.js` and stop pushing them (drop `updateRad`'s health/AP/rads handling; keep only what the overlays still need). Let PrismaHUD's `hud_health` / `hud_ap` / `hud_rads` show them.
   - Decision point: does PrismaHUD's `hud_rads` show **vanilla Rads** only? The Rad Mod's "rads" bar is the same vanilla Rads AV, so PrismaHUD covers it. The **twofold severity** (rads + tissue) is conveyed by health/AP + the new toxic widget, so nothing is lost.
5. **Keep the Rad Mod's own view** for the gas-mask overlay, storm banners, SPECIAL popup, and debug menu (rename `radhud.*` → `radoverlay.*` to reflect it is no longer the vitals HUD). These are fullscreen/centred/modal and are **not** registered as PrismaHUD widgets.
6. **Graceful fallback:** if `PRISMA_HUD_API::RequestPluginAPI()` returns null (PrismaHUD not installed), the toxic view still renders at its default position (skip `RegisterWidget`, keep a sane default `setHUDPosition`). The Rad Mod must not hard-require PrismaHUD.
7. **Build + deploy** both the Rad Mod DLL and the new `hud_toxic.*` files into the live PrismaUI views folder; confirm the toxic widget appears in PrismaHUD's HUD editor and positions correctly.

---

## 6. Open questions / decisions for you

- **Filter/condition widget:** it is a positionable readout like the vitals — do you want it **also** registered as a PrismaHUD widget (so users can move it in the HUD editor), or kept fixed in the Rad Mod overlay? Easy either way; default plan keeps it in the overlay.
- **Storm banners / SPECIAL popup / gas-mask overlay:** confirmed **stay in the Rad Mod** (alerts/modals/fullscreen, not HUD vitals).
- **Option A vs B** for the toxic (recommend A).
- **PrismaHUD rads vs twofold rads:** confirm PrismaHUD's `hud_rads` is acceptable as the rad readout, or whether the toxic widget should show a combined "effective severity" too.

---

## 7. Net result

- One unified, user-editable HUD owned by PrismaHUD_F4 (health, AP, rads + the Rad Mod's toxic widget parented in).
- Rad Mod stops duplicating vitals; it owns only its gameplay overlays and one small toxic widget.
- No hard dependency added: PrismaHUD absent → toxic widget falls back to a default position, overlays unaffected.

**Key files:** Rad Mod `src/RadHud.cpp` / `RadHud.h`, `deploy/PrismaUI_F4/views/Interface/FalloutAnomaly/` (split `radhud.*`, add `hud_toxic.*`); PrismaHUD `src/PrismaHUD_F4_API.h` (copy into Rad Mod), and for Option B `src/HUDDataProvider.*` + a new `assets/views/hud_toxic.html`.
