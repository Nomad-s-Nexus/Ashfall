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
    if (phase === 0 || !STORM_THEMES[theme]) {
      clearTimeout(bannerTimer);
      banner.className=""; sub.className=""; return;
    }
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
      var warning = phase === 1;
      var blink = warning ? " warning-blink" : "";
      banner.className = "on " + th.cls + "-b" + blink;
      sub.className    = "on " + th.cls + "-s" + blink;
      clearTimeout(bannerTimer);
      bannerTimer = warning ? 0 : setTimeout(function(){ banner.className=""; sub.className=""; }, 10000);
    }
  } catch(e) {}
};

function cmd(action) { try { if (window.radDebug) window.radDebug(action); } catch (e) {} }

window.SetDebugMenu = function(on) {
  var panel = document.getElementById("debugPanel");
  var show  = ((""+on)==="1" || on===true);
  if (panel) {
    if (show) panel.classList.add("on"); else panel.classList.remove("on");
  }
};

// Debug-menu tabs: "rad" (Rad Mod controls) | "wthr" (force-weather list) | "hud" (layout editor).
function showTab(which) {
  var tabs = ["rad", "wthr", "hud"];
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
