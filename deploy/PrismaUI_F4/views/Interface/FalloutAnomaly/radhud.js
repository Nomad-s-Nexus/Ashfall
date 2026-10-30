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

window.setTheme = function(name) { document.body.classList.toggle("theme-phosphor", String(name).replace(/"/g, "") === "phosphor"); };

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


var TUNING = [
  ["SURVIVAL RATES", [
    ["RateMultiplier", "NEEDS RATE", 0.05, "x"], ["HourlyHunger", "HUNGER /H", 0.005, "%"],
    ["HourlyThirst", "THIRST /H", 0.005, "%"], ["HourlySleepDebt", "FATIGUE /H", 0.005, "%"],
    ["FoodScale", "FOOD RELIEF", 0.01, "%"], ["DrinkScale", "DRINK RELIEF", 0.01, "%"],
    ["SleepRecovery", "REST /H", 0.005, "%"]]],
  ["DISEASES", [
    ["DiseasesEnabled", "DISEASES"], ["DiseaseBaseRisk", "BASE RISK /H", 0.002, "%"],
    ["RiskRadWeather", "RAD WEATHER", 0.01, "%"], ["RiskRain", "RAIN", 0.005, "%"],
    ["RiskWound", "WOUNDS", 0.005, "%"], ["RiskSwim", "SWIMMING", 0.005, "%"],
    ["RiskTaintedFood", "TAINTED FOOD", 0.01, "%"], ["RiskTaintedWater", "TAINTED WATER", 0.01, "%"],
    ["RiskContaminated", "CONTAMINATED", 0.01, "%"]]],
  ["GAS MASK AND FILTER", [
    ["FilterEnabled", "FILTERS"], ["FilterDuration", "FILTER LIFE", 30, "n", " s"],
    ["FilterChokeRads", "CHOKE RADS", 0.5, "n", " rad/s"], ["FilterAutoReplace", "AUTO-SWAP FILTER"],
    ["ConditionEnabled", "MASK WEAR"], ["ConditionDamage", "WEAR / HIT", 0.5, "n"],
    ["ConditionAutoRepair", "AUTO-REPAIR"],
    ["AutoEquipMask", "AUTO-EQUIP MASK"], ["AutoEquipInCombat", "EQUIP IN COMBAT"]]],
  ["CHOKING", [
    ["ChokingEnabled", "CHOKING"], ["ChokeHealthPerSec", "HP DRAIN /S", 0.1, "n"],
    ["ChokeFailedFactor", "FAILED FILTER", 0.05, "x"], ["ChokeSounds", "COUGH SOUNDS"]]],
  ["AMBIENT AND STORMS", [
    ["AmbientEnabled", "AMBIENT"], ["AmbientVolume", "AMBIENT VOL", 1, "n", "%"],
    ["StormVolume", "STORM VOL", 1, "n", "%"], ["StormsEnabled", "RAD STORMS"],
    ["StormMinMinutes", "STORM MIN", 1, "n", " min"], ["StormMaxMinutes", "STORM MAX", 1, "n", " min"]]],
  ["TEMPERATURE AND WETNESS", [
    ["TemperatureEnabled", "TEMPERATURE"], ["ColdIntensity", "COLD", 0.05, "x"],
    ["WetnessEnabled", "WETNESS"], ["WetnessSoakRate", "SOAK RATE", 0.05, "x"],
    ["WetnessDryRate", "DRY RATE", 0.05, "x"]]],
  ["HUD", [["AlwaysShowNeeds", "ALWAYS SHOW NEEDS"]]],
  ["WATER BOTTLES", [["WaterRequireSneak", "SNEAK TO FILL"]]]
];

function tuneText(row, value) {
  var v = Number(value);
  if (row[3] === "x") return "x" + v.toFixed(2);
  if (row[3] === "%") return (v * 100).toFixed(1) + "%";
  return v.toFixed(row[2] < 1 ? 1 : 0) + (row[4] || "");
}

function tuneCommand(key, value) { cmd("tune:" + key + ":" + Number(value).toFixed(4)); }

function tuneRows() {
  var host = document.getElementById("tuneRows");
  if (!host || host.firstChild) return host;
  TUNING.forEach(function(group) {
    var head = document.createElement("div");
    head.className = "dbg-sec";
    head.textContent = group[0];
    host.appendChild(head);
    group[1].forEach(function(row) {
      var key = row[0];
      var line = document.createElement("div");
      if (row.length === 2) {
        line.className = "ghoul-range tune-range tune-toggle";
        line.innerHTML = '<label for="tune-' + key + '">' + row[1] + '</label>' +
          '<input id="tune-' + key + '" type="checkbox"><output id="tuneval-' + key + '"></output>';
        var box = line.querySelector("input");
        box.onchange = function() {
          document.getElementById("tuneval-" + key).textContent = box.checked ? "ON" : "OFF";
          tuneCommand(key, box.checked ? 1 : 0);
        };
      } else {
        line.className = "ghoul-range tune-range";
        line.innerHTML = '<label for="tune-' + key + '">' + row[1] + '</label>' +
          '<input id="tune-' + key + '" type="range" step="' + row[2] + '"><output id="tuneval-' + key + '"></output>';
        var input = line.querySelector("input");
        input.oninput = function() { document.getElementById("tuneval-" + key).textContent = tuneText(row, input.value); };
        input.onchange = function() { tuneCommand(key, input.value); };
      }
      host.appendChild(line);
    });
  });
  return host;
}

window.setNeedsTuning = function(json) {
  try {
    var d = (typeof json === "string") ? JSON.parse(json) : json;
    tuneRows();
    TUNING.forEach(function(group) {
      group[1].forEach(function(row) {
        var entry = d && d[row[0]];
        var input = document.getElementById("tune-" + row[0]);
        var output = document.getElementById("tuneval-" + row[0]);
        if (!entry || !input || !output) return;
        if (row.length === 2) {
          input.checked = entry.v >= 0.5;
          output.textContent = input.checked ? "ON" : "OFF";
        } else {
          input.min = entry.min;
          input.max = entry.max;
          input.value = entry.v;
          output.textContent = tuneText(row, entry.v);
        }
        output.classList.toggle("tuned", !!entry.o);
      });
    });
  } catch (e) {}
};

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
  var tabs = ["rad", "wthr", "ghouls", "armor", "survival", "hud"];
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
