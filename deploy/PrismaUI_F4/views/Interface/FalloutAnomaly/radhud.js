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
var MASK_PNG_ENABLED = true;
window.setMask = function(json) {
  try {
    var d = (typeof json === "string") ? JSON.parse(json) : json;
    var el = document.getElementById("maskOverlay");
    if (!el) return;
    if (d.on && MASK_PNG_ENABLED) {
      el.style.transitionDelay = MASK_ON_DELAY + "s";
      el.classList.add("on");
    } else {
      el.style.transitionDelay = "0s";
      el.classList.remove("on");
    }
  } catch(e) {}
};

var STORM_THEMES = {
  1: { cls:"fire",   name:"[!] RAD FIRESTORM",       sub:"LETHAL RADIATION - SEEK SHELTER" },
  2: { cls:"sulfur", name:"[!] TOXIC SULFUR STORM",  sub:"TOXIC ATMOSPHERE - AVOID EXPOSURE" },
  3: { cls:"acid",   name:"[!] ACID RAIN",           sub:"CAUSTIC PRECIPITATION" },
  4: { cls:"ash",    name:"[!] ASH FALLOUT",         sub:"RADIATION PARTICULATE - LETHAL" },
  5: { cls:"dust",   name:"[!] DUST STORM",           sub:"BLINDING DUST - MASK UP" }
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

window.setTheme = function() {};
