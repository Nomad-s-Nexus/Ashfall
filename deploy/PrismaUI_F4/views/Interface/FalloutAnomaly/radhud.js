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


window.setArmorInfo = function(json) {
  try {
    var d = (typeof json === "string") ? JSON.parse(json) : json;
    if (!d || typeof d !== "object") return;
    var list = document.getElementById("armorList");
    var nonThermalList = document.getElementById("armorNonThermalList");
    if (!list || !nonThermalList) return;
    var regions = ["head", "torso", "leftArm", "rightArm", "leftLeg", "rightLeg"];
    var labels = ["HEAD", "TORSO", "L ARM", "R ARM", "L LEG", "R LEG"];
    function percent(value) {
      var n = Number(value);
      return isFinite(n) ? Math.round(Math.max(0, Math.min(100, n))) : 0;
    }
    var pieces = (Array.isArray(d.pieces) ? d.pieces : []).filter(function(p) {
      return p && typeof p === "object";
    });
    pieces.sort(function(a, b) {
      return percent(b.insulation) - percent(a.insulation) || String(a.name || "").localeCompare(String(b.name || ""));
    });
    var thermal = [], nonThermal = [];
    pieces.forEach(function(p) {
      var contributes = percent(p.insulation) > 0 ||
        (percent(p.warmth) > 0 && regions.some(function(region) { return !!p[region]; }));
      (contributes ? thermal : nonThermal).push(p);
    });
    document.getElementById("armorInsulation").textContent = percent(d.insulation) + "%";
    document.getElementById("armorEffective").textContent = percent(d.effectiveInsulation) + "%";
    document.getElementById("armorInsulationMeter").style.width = percent(d.insulation) + "%";
    document.getElementById("armorEffectiveMeter").style.width = percent(d.effectiveInsulation) + "%";
    document.getElementById("armorPower").textContent = d.powerArmor ? "SEALED" : "UNSEALED";
    document.getElementById("armorPowerDetail").textContent = d.powerArmor ? "COMPLETE SUIT DETECTED" : "NO COMPLETE SUIT DETECTED";
    var figure = document.getElementById("armorFigure");
    figure.classList.toggle("power-armor", d.inPowerArmor === true);
    figure.setAttribute("aria-label", d.inPowerArmor === true ?
      "Front-facing Power Armor reference schematic" : "Front-facing Vault suit reference schematic");
    document.getElementById("armorThermalCount").textContent = thermal.length + " RECORDS";
    document.getElementById("armorNonThermalCount").textContent = nonThermal.length + " RECORDS";
    function renderRows(target, records, emptyText) {
      target.textContent = "";
      if (!records.length) {
        var empty = document.createElement("tr");
        var cell = document.createElement("td");
        cell.className = "armor-empty";
        cell.colSpan = 5;
        cell.textContent = emptyText;
        empty.appendChild(cell);
        target.appendChild(empty);
      }
      records.forEach(function(piece) {
        var row = document.createElement("tr");
        row.className = "armor-row";
        var coverage = regions.filter(function(region) { return !!piece[region]; }).map(function(region) {
          return labels[regions.indexOf(region)];
        });
        [piece.name || "<unnamed>", coverage.length ? percent(piece.warmth) + "%" : "--", percent(piece.insulation) + "%",
          coverage.join(" / ") || "NONE", piece.override ? "OVERRIDE" : coverage.length ? "DEFAULT" : "--"].forEach(function(value, index) {
          var cell = document.createElement("td");
          cell.textContent = value;
          if (index === 4 && piece.override) cell.className = "armor-override";
          row.appendChild(cell);
        });
        target.appendChild(row);
      });
    }
    renderRows(list, thermal, pieces.length ? "NO THERMAL CONTRIBUTION" : "NO WORN ARMOR");
    renderRows(nonThermalList, nonThermal, "NONE EQUIPPED");
    regions.forEach(function(region) {
      var zone = document.getElementById("armorRegion-" + region);
      var names = thermal.filter(function(piece) { return !!piece[region]; });
      var items = zone.querySelector("ul");
      zone.classList.toggle("covered", names.length > 0);
      items.textContent = "";
      (names.length ? names : [{name: "NO THERMAL COVERAGE"}]).forEach(function(piece) {
        var item = document.createElement("li");
        item.textContent = piece.name || "<unnamed>";
        items.appendChild(item);
      });
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
  if (show && window.RadShowcase) window.RadShowcase.stop();
};

var SURV_POOLS = [["hunger", "Hunger"], ["thirst", "Thirst"], ["sleepDebt", "Fatigue"]];
var SURV_DISEASES = { 1: "FEVER", 2: "WASTING" };

function survStage(percent) { return percent < 34 ? 0 : percent < 67 ? 1 : percent < 90 ? 2 : 3; }

window.updateSurvival = function(json) {
  var data = json;
  if (typeof json === "string") { try { data = JSON.parse(json); } catch (e) { return; } }
  if (!data) return;
  var pools = document.getElementById("survivalPools");
  if (pools) {
    var rows = "";
    for (var i = 0; i < SURV_POOLS.length; i++) {
      var key = SURV_POOLS[i][0], label = SURV_POOLS[i][1];
      var value = Math.max(0, Math.min(100, Math.round(+data[key] || 0)));
      var stage = survStage(value);
      rows += "<dt>" + label + "</dt><dd>" + value + "%" + (stage > 0 ? "  stage " + stage : "") + "</dd>";
    }
    pools.innerHTML = rows;
  }
  var illness = document.getElementById("survivalDisease");
  if (illness) {
    var risk = Math.max(0, Math.min(100, Math.round(+data.risk || 0)));
    var packed = Math.max(0, Math.min(23, Math.round(+data.disease || 0)));
    var id = Math.floor(packed / 10), stage = packed % 10;
    var name = SURV_DISEASES[id] || "";
    var state = id === 0 ? "NONE" : (stage > 0 ? name + " (stage " + stage + ")" : name + " (incubating)");
    illness.innerHTML = "<dt>Exposure</dt><dd>" + risk + "%</dd><dt>Illness</dt><dd>" + state + "</dd>";
  }
};

function showTab(which) {
  var tabs = ["rad", "wthr", "ghouls", "armor", "survival", "showcase", "hud"];
  for (var i = 0; i < tabs.length; i++) {
    var pane = document.getElementById("tab-" + tabs[i]);
    var btn  = document.getElementById("tabbtn-" + tabs[i]);
    var on   = (tabs[i] === which);
    if (pane) { if (on) pane.classList.add("on"); else pane.classList.remove("on"); }
    if (btn)  { if (on) btn.classList.add("on");  else btn.classList.remove("on"); }
  }
  var panel = document.getElementById("debugPanel");
  if (panel) {
    panel.classList.toggle("armor-view", which === "armor");
    panel.scrollTop = 0;
  }
}

function needsScene(id, label, apply, revert) {
  return {
    id: id,
    label: label,
    steps: [[0, "time:freeze"], [0, apply], [10, revert], [0, "time:resume"]],
    abort: [revert, "time:resume"]
  };
}

var SHOWCASE_SCENES = [
  { id: "ash",      label: "Ash storm",   steps: [[0, "storm:0"], [4, "skipwarn"], [50, "endstorm"]] },
  { id: "fire",     label: "Firestorm",   steps: [[0, "storm:1"], [4, "skipwarn"], [50, "endstorm"]] },
  { id: "sulfur",   label: "Sulfur",      steps: [[0, "storm:2"], [4, "skipwarn"], [50, "endstorm"]] },
  { id: "acid",     label: "Acid storm",  steps: [[0, "storm:3"], [4, "skipwarn"], [50, "endstorm"]] },
  { id: "dust",     label: "Dust storm",  steps: [[0, "storm:4"], [4, "skipwarn"], [50, "endstorm"]] },
  { id: "rain",     label: "Rain",        steps: [[0, "forceweather:Fallout4.esm|1CA7E4"], [30, "weather:clear"]] },
  { id: "acidrain", label: "Acid rain",   steps: [[0, "forceweather:RadiationOverhaul.esp|000843"], [30, "weather:clear"]] },
  { id: "mask",     label: "Mask and filter", steps: [[0, "mask:on"], [0, "filter:full"], [8, "filter:low"], [8, "filter:dead"], [8, "filter:replace"], [6, "mask:auto"]] },
  { id: "rads",     label: "Radiation",   steps: [[0, "rads:200"], [6, "tissue:150"], [8, "rads:600"], [10, "heal"], [2, "clear"]] },
  needsScene("peckish", "Peckish", "needs:0.5:-1:-1:-1:0", "needs:0:-1:-1:-1:0"),
  needsScene("starving", "Starving", "needs:0.95:-1:-1:-1:0", "needs:0:-1:-1:-1:0"),
  needsScene("exhausted", "Exhausted", "needs:-1:-1:0.95:-1:0", "needs:-1:-1:0:-1:0"),
  needsScene("fever", "Fever stage 2", "needs:-1:-1:-1:1:2", "needs:-1:-1:-1:0:0"),
  needsScene("wasting", "Wasting stage 3", "needs:-1:-1:-1:2:3", "needs:-1:-1:-1:0:0"),
  { id: "needsclear", label: "Clear needs", steps: [[0, "needs:0:0:0:0:0"], [0, "time:resume"]] }
];

var RadShowcase = window.RadShowcase = (function() {
  var timers = [];
  var pendingAbort = null;
  var scenes = SHOWCASE_SCENES;
  function status(text) { var el = document.getElementById("showcaseStatus"); if (el) el.textContent = text; }
  function later(fn, seconds) { timers.push(setTimeout(fn, seconds * 1000)); }
  function sceneLength(scene) {
    var total = 0;
    for (var i = 0; i < scene.steps.length; i++) total += scene.steps[i][0];
    return total + 4;
  }
  function schedule(scene, offset) {
    var at = offset;
    var last = scene.steps.length - 1;
    if (scene.abort) later(function() { pendingAbort = scene.abort; }, offset);
    for (var i = 0; i < scene.steps.length; i++) {
      at += scene.steps[i][0];
      (function(command, isLast) {
        later(function() {
          cmd(command);
          if (isLast && scene.abort) pendingAbort = null;
        }, at);
      })(scene.steps[i][1], i === last);
    }
    return offset + sceneLength(scene);
  }
  function stop() {
    for (var i = 0; i < timers.length; i++) clearTimeout(timers[i]);
    timers = [];
    if (pendingAbort) {
      for (var j = 0; j < pendingAbort.length; j++) cmd(pendingAbort[j]);
      pendingAbort = null;
    }
    status("Idle.");
  }
  function run(id) {
    stop();
    for (var i = 0; i < scenes.length; i++) {
      if (scenes[i].id === id) { schedule(scenes[i], 0); status("Playing " + scenes[i].label + "."); return; }
    }
  }
  function startTour() {
    stop();
    cmd("god:on");
    var at = 2;
    for (var i = 0; i < scenes.length; i++) {
      (function(scene, when) { later(function() { status("Tour: " + scene.label + "."); }, when); })(scenes[i], at);
      at = schedule(scenes[i], at);
    }
    later(function() { status("Tour finished."); }, at);
    cmd("close");
  }
  function build() {
    var row = document.getElementById("showcaseScenes");
    if (!row) return;
    for (var i = 0; i < scenes.length; i++) {
      var b = document.createElement("button");
      b.textContent = scenes[i].label;
      (function(id) { b.onclick = function() { run(id); }; })(scenes[i].id);
      row.appendChild(b);
    }
  }
  build();
  return { scenes: scenes, run: run, startTour: startTour, stop: stop };
})();
