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

var MASK_ON_DELAY = 0.0;
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

function setGhoulChanceLabel(value) {
  var n = Math.max(0, Math.min(100, parseInt(value, 10) || 0));
  var output = document.getElementById("ghoulChanceValue");
  if (output) output.textContent = n + "%";
  return n;
}

function setGhoulChance(value) {
  cmd("ghouls:chance:" + Math.max(0, Math.min(100, parseInt(value, 10) || 0)));
}

function setGhoulEnabled(enabled) {
  cmd(enabled ? "ghouls:on" : "ghouls:off");
}

function resetGhoulSettings() {
  cmd("ghouls:reset");
}

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
    if (status) {
      status.textContent = "HUNTER PACKS: " + (enabled ? "ON" : "OFF") + " | SPAWN CHANCE: " + n + "%";
      status.classList.toggle("off", !enabled);
    }
  } catch (e) {}
}

window.SetDebugMenu = function(on) {
  var panel = document.getElementById("debugPanel");
  var show  = ((""+on)==="1" || on===true);
  if (panel) {
    if (show) panel.classList.add("on"); else panel.classList.remove("on");
  }
};

function showTab(which) {
  var tabs = ["rad", "wthr", "ghouls", "hud"];
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
