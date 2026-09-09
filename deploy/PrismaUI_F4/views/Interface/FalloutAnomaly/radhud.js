// Rad Overhaul HUD overlay: gas-mask overlay, filter/condition widget, storm banners,
// SPECIAL popup, and the debug menu. The vitals HUD (rads/tissue/health/AP bars) moved to
// PrismaHUD_F4 — its JS (updateRad/toggleHud/loadHudPrefs, drag/resize, prefs persistence)
// was excised 2026-07-03 along with the matching C++ paths.

// SPECIAL-penalty diagnostic popup, driven by C++ (showSpecialInfo {lines:[...]}). Shows the full
// multi-line report (the vanilla HUD corner truncated it). Dismissed via the CLOSE button or backdrop.
window.showSpecialInfo = function(json) {
  try {
    var d = (typeof json === "string") ? JSON.parse(json) : json;
    var body = document.getElementById("specialPopupBody");
    if (!body) return;
    body.innerHTML = "";
    (d.lines || []).forEach(function(t) {
      var div = document.createElement("div");
      div.className = "line";
      div.textContent = t;
      body.appendChild(div);
    });
    document.getElementById("specialPopup").classList.add("on");
  } catch(e) {}
};
function closeSpecialInfo() {
  var p = document.getElementById("specialPopup");
  if (p) p.classList.remove("on");
}

// Fullscreen low-filter visor fog, driven by C++ (updateFilter {charge, condition, spares, masked}).
// The FILTER/MASK BAR widget moved to the standalone hud_filter.html view (registered with PrismaHUD_F4
// for positioning); only the fullscreen condensation haze stays here. Fog builds below 30% charge (max
// at 0), or a fixed haze when the mask is broken; cleared entirely while unmasked.
window.updateFilter = function(json) {
  try {
    var d = (typeof json === "string") ? JSON.parse(json) : json;
    var fogEl = document.getElementById("visorFog");
    if (!fogEl) return;
    var fog = 0;
    if (d.masked) {
      var ch = +d.charge;
      if (ch >= 0 && ch < 30) fog = Math.max(fog, (30 - ch) / 30);
      if ((+d.condition) === 0) fog = Math.max(fog, 0.55);
    }
    fogEl.style.opacity = (fog * 0.85).toFixed(2);
  } catch(e) {}
};

// First-person gas-mask screen overlay, driven by C++ (setMask {on:bool}). Toggles the CSS fade
// class on the fullscreen <img>; the engine sends only on state change.
//   Put-on timing: this mod ships its own mask (no GMW), so there is no external first-person
//   put-on animation to wait for. Fade in promptly. If we later add our own put-on animation
//   (e.g. a baked APNG sweep), raise this to hold the static overlay until that finishes.
var MASK_ON_DELAY = 0.0;   // seconds to wait after equip before the overlay fades in
window.setMask = function(json) {
  try {
    var d = (typeof json === "string") ? JSON.parse(json) : json;
    var el = document.getElementById("maskOverlay");
    if (!el) return;
    if (d.on) {
      el.style.transitionDelay = MASK_ON_DELAY + "s";
      el.classList.add("on");
    } else {
      el.style.transitionDelay = "0s";
      el.classList.remove("on");
    }
  } catch(e) {}
};

var STORM_THEMES = {
  1: { cls:"fire",   name:"⚡ RAD FIRESTORM",       sub:"LETHAL RADIATION - SEEK SHELTER" },
  2: { cls:"sulfur", name:"☣ TOXIC SULFUR STORM",  sub:"TOXIC ATMOSPHERE - AVOID EXPOSURE" },
  3: { cls:"acid",   name:"⚗ ACID RAIN",           sub:"CAUSTIC PRECIPITATION" },
  4: { cls:"ash",    name:"☢ ASH FALLOUT",         sub:"RADIATION PARTICULATE - LETHAL" },
  5: { cls:"dust",   name:"☁ DUST STORM",           sub:"BLINDING DUST - MASK UP" }
};
var currentStormTheme = 0, currentStormPhase = 0, bannerTimer = 0;

window.stormPhase = function(json) {
  try {
    var d = (typeof json === "string") ? JSON.parse(json) : json;
    var phase=d.phase|0, theme=d.theme|0, shelter=!!d.sheltered, lethal=!!d.lethal;
    var phaseChanged = (phase !== currentStormPhase) || (theme !== currentStormTheme);
    currentStormTheme = theme; currentStormPhase = phase;
    var banner = document.getElementById("emissionBanner");
    var sub    = document.getElementById("emissionSub");
    if (phase === 0 || !STORM_THEMES[theme]) { banner.className=""; sub.className=""; return; }
    if (phaseChanged) {
      var th = STORM_THEMES[theme];
      if (phase === 3) {
        // Aftermath: the storm is clearing. Show a calmer "passing" banner so the player
        // knows it's ending, instead of re-flashing the same alarming storm warning.
        banner.textContent = th.name + " - PASSING";
        sub.textContent    = "The storm is clearing - radiation subsiding";
      } else {
        banner.textContent = th.name + (lethal && (phase===1||(phase===2&&!shelter)) ? " - SEEK SHELTER!" : "");
        sub.textContent    = th.sub;
      }
      banner.className = "on " + th.cls + "-b";
      sub.className    = "on " + th.cls + "-s";
      clearTimeout(bannerTimer);
      bannerTimer = setTimeout(function(){ banner.className=""; sub.className=""; }, 10000);
    }
  } catch(e) {}
};

function cmd(action) { try { if (window.radDebug) window.radDebug(action); } catch (e) {} }

function setGhoulChanceLabel(value) {
  var n = Math.max(0, Math.min(100, parseInt(value, 10) || 0));
  var output = document.getElementById("ghoulChanceValue");
  if (output) output.textContent = n + "%";
  return n;
}

function setGhoulChance(value) { cmd("ghouls:chance:" + setGhoulChanceLabel(value)); }
function setGhoulEnabled(enabled) { cmd(enabled ? "ghouls:on" : "ghouls:off"); }
function resetGhoulSettings() { cmd("ghouls:reset"); }

function setGhoulSettings(state) {
  try {
    var d = (typeof state === "string") ? JSON.parse(state) : state;
    var enabled = !!(d && d.enabled);
    var n = Math.max(0, Math.min(100, Math.round(Number(d && d.chance) || 0)));
    var input = document.getElementById("ghoulChance");
    var output = document.getElementById("ghoulChanceValue");
    var status = document.getElementById("ghoulState");
    if (input) input.value = n;
    if (output) output.textContent = n + "%";
    if (status) status.textContent = "HUNTER PACKS: " + (enabled ? "ON" : "OFF") + " | SPAWN CHANCE: " + n + "%";
  } catch (e) {}
}

window.setArmorInfo = function(json) {
  try {
    var d = (typeof json === "string") ? JSON.parse(json) : json;
    var summary = document.getElementById("armorSummary");
    var list = document.getElementById("armorList");
    if (!summary || !list) return;
    var pieces = Array.isArray(d && d.pieces) ? d.pieces : [];
    summary.textContent = "ARMOR INSULATION: " + (+d.insulation || 0) + "% | EFFECTIVE THERMAL: " +
      (+d.effectiveInsulation || 0) + "% | POWER ARMOR: " + (d.powerArmor ? "SEALED" : "NO");
    list.textContent = "";
    if (!pieces.length) {
      var empty = document.createElement("div");
      empty.className = "armor-empty";
      empty.textContent = "NO WORN ARMOR";
      list.appendChild(empty);
      return;
    }
    pieces.forEach(function(piece) {
      var row = document.createElement("div");
      row.className = "armor-row";
      var name = document.createElement("div");
      name.className = "armor-name";
      name.textContent = (piece.name || "<unnamed>") + (piece.override ? "  [OVERRIDE]" : "");
      var detail = document.createElement("div");
      detail.className = "armor-detail";
      var coverage = [];
      if (piece.head) coverage.push("HEAD");
      if (piece.torso) coverage.push("TORSO");
      if (piece.leftArm || piece.rightArm) coverage.push("ARMS");
      if (piece.leftLeg || piece.rightLeg) coverage.push("LEGS");
      detail.textContent = "WARMTH " + (+piece.warmth || 0) + "% | INSULATION " +
        (+piece.insulation || 0) + "% | COVER " + (coverage.join("/") || "NONE");
      row.appendChild(name);
      row.appendChild(detail);
      list.appendChild(row);
    });
  } catch (e) {}
};

window.setHudLayoutStatus = function(ok) {
  var status = document.getElementById("radLocalStatus");
  if (status) status.textContent = (("" + ok) === "1" || ok === true) ? "SAVED" : "SAVE FAILED";
};

window.SetDebugMenu = function(on) {
  var panel = document.getElementById("debugPanel");
  var show  = ((""+on)==="1" || on===true);
  if (panel) {
    if (show) panel.classList.add("on"); else panel.classList.remove("on");
  }
};

// Debug-menu tabs: "rad" (Rad Mod controls) | "wthr" (force-weather list) | "armor" (climate gear).
function showTab(which) {
  var tabs = ["rad", "wthr", "ghouls", "armor", "hud"];
  for (var i = 0; i < tabs.length; i++) {
    var pane = document.getElementById("tab-" + tabs[i]);
    var btn  = document.getElementById("tabbtn-" + tabs[i]);
    var on   = (tabs[i] === which);
    if (pane) { if (on) pane.classList.add("on"); else pane.classList.remove("on"); }
    if (btn)  { if (on) btn.classList.add("on");  else btn.classList.remove("on"); }
  }
  var panel = document.getElementById("debugPanel");
  if (panel) panel.scrollTop = 0;
}
