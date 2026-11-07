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
var MASK_PNG_ENABLED = false;
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

function cmd(action) {
  try {
    if (window.pipAudio) window.pipAudio.click();
    if (window.radDebug) {
      window.radDebug(action);
    } else {
      if (action === "radHeal" || action === "heal") {
        if (window.updateVitalsInfo) window.updateVitalsInfo({ health: 220, healthMax: 220, ap: 130, apMax: 130, rads: 0, radsMax: 1000, tissue: 0, permanentTissue: 0 });
      } else if (action === "radBurst200" || action === "rads:200") {
        if (window.updateVitalsInfo) window.updateVitalsInfo({ health: 180, healthMax: 220, ap: 100, apMax: 130, rads: 200, radsMax: 1000, tissue: 50, permanentTissue: 10 });
      } else if (action === "radBurst600" || action === "rads:600") {
        if (window.updateVitalsInfo) window.updateVitalsInfo({ health: 120, healthMax: 220, ap: 70, apMax: 130, rads: 600, radsMax: 1000, tissue: 150, permanentTissue: 40 });
      } else if (action === "tissue150" || action === "tissue:150") {
        if (window.updateVitalsInfo) window.updateVitalsInfo({ health: 80, healthMax: 220, ap: 50, apMax: 130, rads: 750, radsMax: 1000, tissue: 550, permanentTissue: 150 });
      } else if (action === "tissueClear" || action === "clear") {
        if (window.updateVitalsInfo) window.updateVitalsInfo({ health: 220, healthMax: 220, ap: 130, apMax: 130, rads: 0, radsMax: 1000, tissue: 0, permanentTissue: 0 });
      } else if (action === "maskAuto" || action === "mask:auto") {
        if (window.updateAirInfo) window.updateAirInfo({ contaminated: true, sealed: false, sheltered: false, masked: true, failed: false, charge: 85, condition: 94, spares: 4, filterDrain: 0.11, healthLoss: 0, radGain: 0, chokingEnabled: true });
      } else if (action === "filterFull" || action === "filter:full") {
        if (window.updateAirInfo) window.updateAirInfo({ contaminated: true, sealed: false, sheltered: false, masked: true, failed: false, charge: 100, condition: 100, spares: 5, filterDrain: 0.11, healthLoss: 0, radGain: 0, chokingEnabled: true });
      } else if (action === "filterLow" || action === "filter:low") {
        if (window.updateAirInfo) window.updateAirInfo({ contaminated: true, sealed: false, sheltered: false, masked: true, failed: false, charge: 20, condition: 45, spares: 1, filterDrain: 0.11, healthLoss: 0, radGain: 0, chokingEnabled: true });
      } else if (action === "filterDead" || action === "filter:dead") {
        if (window.updateAirInfo) window.updateAirInfo({ contaminated: true, sealed: false, sheltered: false, masked: true, failed: true, charge: 0, condition: 0, spares: 0, filterDrain: 0, healthLoss: 2.5, radGain: 5.0, chokingEnabled: true });
      } else if (action === "endstorm") {
        var ovStorm = document.getElementById("ov-storm-status");
        if (ovStorm) { ovStorm.textContent = "NONE DETECTED"; ovStorm.className = ""; }
      }
    }
  } catch (e) {}
}

window.setTheme = function() {};

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

var TUNE_CATEGORIES = [
  {
    id: "metabolic",
    title: "01 METABOLIC & SURVIVAL RATES",
    groups: ["SURVIVAL RATES", "WATER BOTTLES", "HUD"]
  },
  {
    id: "gasmask",
    title: "02 GAS MASK & CANISTER MECHANICS",
    groups: ["GAS MASK AND FILTER", "CHOKING"]
  },
  {
    id: "pathology",
    title: "03 PATHOLOGY & DISEASE VECTOR RISKS",
    groups: ["DISEASES"]
  },
  {
    id: "storms",
    title: "04 STORMS, CLIMATE & AUDIO VOLUMES",
    groups: ["AMBIENT AND STORMS", "TEMPERATURE AND WETNESS"]
  }
];
function tuneRows() {
  var host = document.getElementById("tuneRows");
  if (!host || host.firstChild) return host;
  var groupMap = {};
  TUNING.forEach(function(g) { groupMap[g[0]] = g[1]; });
  TUNE_CATEGORIES.forEach(function(cat, catIdx) {
    var details = document.createElement("details");
    details.className = "tune-accordion";
    details.id = "tune-cat-" + cat.id;
    if (catIdx === 0) details.open = true;
    var totalControls = 0;
    cat.groups.forEach(function(gName) {
      if (groupMap[gName]) totalControls += groupMap[gName].length;
    });
    var summary = document.createElement("summary");
    summary.className = "tune-summary";
    summary.innerHTML = '<div class="tune-summary-left">' +
      '<span class="tune-chevron">&#9654;</span>' +
      '<span class="tune-group-title">' + cat.title + '</span>' +
      '</div>' +
      '<div class="tune-summary-right">' +
      '<span class="tune-mod-badge" id="tune-mod-' + cat.id + '" style="display:none;">MODIFIED</span>' +
      '<span class="tune-count-badge">' + totalControls + ' CONTROLS</span>' +
      '</div>';
    details.appendChild(summary);
    var body = document.createElement("div");
    body.className = "tune-accordion-body";
    cat.groups.forEach(function(gName) {
      var rows = groupMap[gName];
      if (!rows) return;
      if (cat.groups.length > 1) {
        var subhead = document.createElement("div");
        subhead.className = "tune-subgroup-title";
        subhead.textContent = "// " + gName;
        body.appendChild(subhead);
      }
      rows.forEach(function(row) {
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
            checkTuneModified();
          };
        } else {
          line.className = "ghoul-range tune-range";
          line.innerHTML = '<label for="tune-' + key + '">' + row[1] + '</label>' +
            '<input id="tune-' + key + '" type="range" step="' + row[2] + '"><output id="tuneval-' + key + '"></output>';
          var input = line.querySelector("input");
          input.oninput = function() {
            document.getElementById("tuneval-" + key).textContent = tuneText(row, input.value);
          };
          input.onchange = function() {
            tuneCommand(key, input.value);
            checkTuneModified();
          };
        }
        body.appendChild(line);
      });
    });
    details.appendChild(body);
    host.appendChild(details);
  });
  return host;
}
window.toggleAllTuneAccordions = function(openState) {
  var accordions = document.querySelectorAll(".tune-accordion");
  accordions.forEach(function(a) { a.open = !!openState; });
  if (window.pipAudio) window.pipAudio.click();
};
function checkTuneModified() {
  TUNE_CATEGORIES.forEach(function(cat) {
    var badge = document.getElementById("tune-mod-" + cat.id);
    if (!badge) return;
    var hasTuned = false;
    var details = document.getElementById("tune-cat-" + cat.id);
    if (details) {
      hasTuned = !!details.querySelector("output.tuned");
    }
    badge.style.display = hasTuned ? "inline-block" : "none";
  });
}
function copyText(text, onDone) {
  function fallback() {
    try {
      var ta = document.createElement("textarea");
      ta.value = text;
      ta.setAttribute("readonly", "");
      ta.style.position = "fixed";
      ta.style.left = "-9999px";
      document.body.appendChild(ta);
      ta.select();
      var ok = document.execCommand("copy");
      document.body.removeChild(ta);
      if (ok && onDone) onDone();
    } catch (e) {}
  }
  if (!navigator.clipboard || !navigator.clipboard.writeText) {
    fallback();
    return;
  }
  try {
    navigator.clipboard.writeText(text).then(function() {
      if (onDone) onDone();
    }, fallback);
  } catch (e) {
    fallback();
  }
}
window.copyIniDiff = function(ev) {
  var lines = ["[Tuning]"];
  var modifiedCount = 0;
  TUNING.forEach(function(group) {
    group[1].forEach(function(row) {
      var key = row[0];
      var input = document.getElementById("tune-" + key);
      var out = document.getElementById("tuneval-" + key);
      if (!input) return;
      var val = (row.length === 2) ? (input.checked ? "1" : "0") : Number(input.value).toFixed(4);
      var isTuned = out && out.classList.contains("tuned");
      if (isTuned) {
        lines.push(key + " = " + val);
        modifiedCount++;
      }
    });
  });
  if (modifiedCount === 0) {
    lines.push("; All current parameter values");
    TUNING.forEach(function(group) {
      group[1].forEach(function(row) {
        var key = row[0];
        var input = document.getElementById("tune-" + key);
        if (!input) return;
        var val = (row.length === 2) ? (input.checked ? "1" : "0") : Number(input.value).toFixed(4);
        lines.push(key + " = " + val);
      });
    });
  }
  var text = lines.join("\n");
  copyText(text, function() {
    var btn = ev ? ev.target : null;
    if (btn) {
      var orig = btn.textContent;
      btn.textContent = "[COPIED INI DIFF!]";
      setTimeout(function() { btn.textContent = orig; }, 1800);
    }
  });
  if (window.pipAudio) window.pipAudio.click();
};

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
    var ovTorso = document.getElementById("ov-val-torso");
    if (ovTorso) ovTorso.textContent = "INSULATION " + percent(d.insulation) + "%";
    var ovFig = document.getElementById("overviewFigure");
    if (ovFig) ovFig.classList.toggle("power-armor", d.inPowerArmor === true);
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
        var cellName = document.createElement("td");
        cellName.textContent = piece.name || "<unnamed>";
        row.appendChild(cellName);
        var cellWarmth = document.createElement("td");
        cellWarmth.textContent = coverage.length ? percent(piece.warmth) + "%" : "--";
        row.appendChild(cellWarmth);
        var cellInsul = document.createElement("td");
        cellInsul.textContent = percent(piece.insulation) + "%";
        cellInsul.style.fontWeight = "bold";
        row.appendChild(cellInsul);
        var cellCov = document.createElement("td");
        cellCov.textContent = coverage.join(" / ") || "NONE";
        row.appendChild(cellCov);
        var cellSrc = document.createElement("td");
        cellSrc.textContent = piece.override ? "OVERRIDE" : coverage.length ? "DEFAULT" : "--";
        if (piece.override) cellSrc.className = "armor-override";
        row.appendChild(cellSrc);
        target.appendChild(row);
      });
    }
    renderRows(list, thermal, pieces.length ? "NO THERMAL CONTRIBUTION" : "NO WORN ARMOR");
    renderRows(nonThermalList, nonThermal, "NONE EQUIPPED");
        regions.forEach(function(region) {
      var zone = document.getElementById("armorRegion-" + region);
      if (!zone) return;
      var matching = thermal.filter(function(piece) { return !!piece[region]; });
      var items = zone.querySelector("ul");
      zone.classList.toggle("covered", matching.length > 0);
      items.textContent = "";
      (matching.length ? matching : [{name: "NO THERMAL COVERAGE"}]).forEach(function(piece) {
        var item = document.createElement("li");
        item.textContent = piece.name || "<unnamed>";
        items.appendChild(item);
      });
      var statEl = document.getElementById("regStat-" + region);
      if (statEl) {
        var topInsul = 0;
        matching.forEach(function(p) { topInsul = Math.max(topInsul, percent(p.insulation)); });
        statEl.textContent = matching.length ? topInsul + "%" : "EXPOSED";
        statEl.style.color = matching.length ? "var(--pip-amber)" : "var(--pip-danger)";
      }
      var itemEl = document.getElementById("regItem-" + region);
      if (itemEl) {
        if (matching.length === 0) {
          itemEl.textContent = "UNPROTECTED";
          itemEl.className = "armor-reg-item reg-unprotected";
        } else {
          itemEl.textContent = matching[0].name + (matching.length > 1 ? " (+" + (matching.length - 1) + ")" : "");
          itemEl.className = "armor-reg-item";
        }
      }
    });
    var nonThermalDetails = document.getElementById("armorNonThermalDetails");
    if (nonThermalDetails) {
      nonThermalDetails.style.display = nonThermal.length ? "block" : "none";
    }
    ["head", "torso", "leftArm", "rightArm", "leftLeg", "rightLeg"].forEach(function(reg) {
      var tag = document.getElementById("paTag-" + reg);
      if (tag) {
        tag.classList.toggle("sealed", !!d.powerArmor);
      }
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

function debugPanelOpen() {
  var panel = document.getElementById("debugPanel");
  return !!(panel && panel.classList.contains("on"));
}
function closeDebugMenu() {
  window.SetDebugMenu(0);
  cmd("close");
}
if (typeof document !== "undefined" && document.addEventListener) {
  document.addEventListener("keydown", function(e) {
    if ((e.key === "Escape" || e.key === "Esc") && debugPanelOpen()) closeDebugMenu();
  });
}

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
  if (window.updatePipDial) {
    var h = Math.max(0, Math.min(100, Math.round(+data.hunger || 0)));
    var t = Math.max(0, Math.min(100, Math.round(+data.thirst || 0)));
    var f = Math.max(0, Math.min(100, Math.round(+data.sleepDebt || 0)));
    var r = Math.max(0, Math.min(100, Math.round(+data.risk || 0)));
    window.updatePipDial("hunger", h, h + "%", "STAGE " + survStage(h));
    window.updatePipDial("thirst", t, t + "%", "STAGE " + survStage(t));
    window.updatePipDial("fatigue", f, f + "%", "STAGE " + survStage(f));
    window.updatePipDial("exposure", r, r + "%", (+data.disease > 0 ? "INFECTED" : "NO ILLNESS"));
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
  var tabs = ["overview", "rad", "wthr", "ghouls", "armor", "survival", "showcase", "hud"];
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
  if (window.pipAudio) window.pipAudio.tab();
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
    closeDebugMenu();
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

window.pipAudio = (function() {
  var ctx = null;
  var enabled = true;
  function getContext() {
    if (!ctx && (window.AudioContext || window.webkitAudioContext)) {
      try { ctx = new (window.AudioContext || window.webkitAudioContext)(); } catch(e) {}
    }
    return ctx;
  }
  return {
    playChirp: function(freq, duration, type) {
      if (!enabled) return;
      try {
        var c = getContext();
        if (!c) return;
        if (c.state === 'suspended') c.resume();
        var osc = c.createOscillator();
        var gain = c.createGain();
        osc.type = type || 'sine';
        osc.frequency.setValueAtTime(freq || 880, c.currentTime);
        gain.gain.setValueAtTime(0.04, c.currentTime);
        gain.gain.exponentialRampToValueAtTime(0.001, c.currentTime + (duration || 0.08));
        osc.connect(gain);
        gain.connect(c.destination);
        osc.start();
        osc.stop(c.currentTime + (duration || 0.08));
      } catch(e) {}
    },
    click: function() { this.playChirp(1200, 0.03, 'square'); },
    tab: function() { this.playChirp(750, 0.05, 'triangle'); },
    alert: function() { this.playChirp(440, 0.12, 'sawtooth'); },
    toggle: function() {
      enabled = !enabled;
      return enabled;
    },
    isEnabled: function() { return enabled; }
  };
})();
window.togglePipAudio = function() {
  var on = window.pipAudio.toggle();
  var btn = document.getElementById("pipAudioToggle");
  if (btn) {
    btn.textContent = on ? "FX: ON" : "FX: OFF";
    btn.classList.toggle("muted", !on);
  }
  if (on) window.pipAudio.click();
};
window.setPipColor = function(colorTheme) {
  var root = document.documentElement;
  var body = document.body;
  var panel = document.getElementById("debugPanel");
  ["pip-theme-amber", "pip-theme-cyan", "pip-theme-white"].forEach(function(cls) {
    root.classList.remove(cls);
    if (body) body.classList.remove(cls);
    if (panel) panel.classList.remove(cls);
  });
  if (colorTheme === "amber" || colorTheme === "cyan" || colorTheme === "white") {
    var cls = "pip-theme-" + colorTheme;
    root.classList.add(cls);
    if (body) body.classList.add(cls);
    if (panel) panel.classList.add(cls);
  }
  var btns = document.querySelectorAll(".pip-theme-pickers .theme-btn");
  btns.forEach(function(b) {
    b.classList.toggle("active", b.getAttribute("data-theme") === colorTheme);
  });
  try { localStorage.setItem("pip_theme_color", colorTheme); } catch(e) {}
  if (window.pipAudio) window.pipAudio.click();
};
window.updatePipDial = function(type, pct, valText, subText) {
  var valEl = document.getElementById("dial-val-" + type);
  if (valEl && valText !== undefined) valEl.textContent = valText;
  var subEl = document.getElementById("dial-sub-" + type);
  if (subEl && subText !== undefined) subEl.textContent = subText;
  var barEl = document.getElementById("dial-bar-" + type);
  if (barEl) {
    var r = barEl.getAttribute("r") || 30;
    var circ = 2 * Math.PI * parseFloat(r);
    var safePct = Math.max(0, Math.min(100, Math.round(Number(pct) || 0)));
    var offset = circ * (1 - safePct / 100);
    barEl.style.strokeDasharray = circ.toFixed(2);
    barEl.style.strokeDashoffset = offset.toFixed(2);
    barEl.classList.remove("dial-bar-yellow", "dial-bar-orange", "dial-bar-red", "dial-bar-warn", "dial-bar-crit");
    var dialBox = barEl.closest ? barEl.closest(".overview-dial") : null;
    if (dialBox) {
      dialBox.classList.remove("dial-state-yellow", "dial-state-orange", "dial-state-red");
      dialBox.setAttribute("data-pct", safePct);
    }
    var state = null;
    var isHazard = (type === "rads" || type === "tissue" || type === "hunger" || type === "thirst" || type === "fatigue" || type === "exposure");
    if (isHazard) {
      if (safePct >= 75) {
        state = "red";
      } else if (safePct >= 50) {
        state = "orange";
      } else if (safePct >= 25) {
        state = "yellow";
      }
    } else {
      if (safePct <= 25) {
        state = "red";
      } else if (safePct <= 50) {
        state = "orange";
      } else if (safePct <= 75) {
        state = "yellow";
      }
    }
    if (state) {
      barEl.classList.add("dial-bar-" + state);
      if (dialBox) dialBox.classList.add("dial-state-" + state);
    }
    if (type === "maskcond") {
      var maskMeter = document.getElementById("dial-meter-maskcond");
      if (maskMeter) {
        maskMeter.classList.remove("meter-state-yellow", "meter-state-orange", "meter-state-red");
        if (state) maskMeter.classList.add("meter-state-" + state);
      }
    }
  }
};
if (typeof window !== "undefined" && window.addEventListener) window.addEventListener("keydown", function(e) {
  var p = document.getElementById("debugPanel");
  if (!p || !p.classList.contains("on")) return;
  var tabs = ["overview", "rad", "wthr", "ghouls", "armor", "survival", "showcase", "hud"];
  if (e.key >= "0" && e.key <= "7") {
    var idx = parseInt(e.key, 10);
    if (tabs[idx]) {
      showTab(tabs[idx]);
    }
  }
});
try {
  var savedTheme = localStorage.getItem("pip_theme_color");
  if (savedTheme) setPipColor(savedTheme);
} catch(e) {}
if (typeof window !== "undefined" && window.addEventListener) {
  window.addEventListener("DOMContentLoaded", function() {
        if (!window.radDebug) {
      if (window.location && window.location.hash) {
        if (window.location.hash.indexOf("skipboot=1") !== -1) {
          window.suppressBootAnimation = true;
          var bootEl = document.getElementById("robcoBootOverlay");
          if (bootEl) bootEl.classList.add("boot-hidden");
        }
      }
      if (window.SetDebugMenu) window.SetDebugMenu(1);
      var initialTab = "overview";
      if (window.location && window.location.hash) {
        var m = window.location.hash.match(/tab=([a-z0-9_-]+)/i);
        if (m) initialTab = m[1];
      }
      if (window.showTab) window.showTab(initialTab);
      function loadMockPreview() {
        if (window.updateClimateInfo) {
          window.updateClimateInfo({
            core: 36.8, warmth: 85, stage: 0, ambient: 16.2, wind: 4.1,
            wetness: 15, insulation: 68, heat: 10, shelter: 50, rainCover: 80,
            indoors: false, cooling: 0.025, warming: 0.035
          });
        }
        if (window.updateAirInfo) {
          window.updateAirInfo({
            contaminated: true, sealed: false, sheltered: false, masked: true, failed: false,
            charge: 76, condition: 94, spares: 4, filterDrain: 0.11, healthLoss: 0,
            radGain: 0, chokingEnabled: true
          });
        }
        if (window.updateVitalsInfo) {
          window.updateVitalsInfo({
            health: 195, healthMax: 220, ap: 115, apMax: 130, rads: 85, radsMax: 1000,
            tissue: 28, permanentTissue: 10
          });
        }
        if (window.setGhoulSettings) {
          window.setGhoulSettings({ enabled: true, chance: 40 });
        }
        if (window.setArmorInfo) {
          window.setArmorInfo({
            insulation: 68, effectiveInsulation: 62, powerArmor: false, inPowerArmor: false,
            pieces: [
              { name: "Vault 111 Armored Jumpsuit", warmth: 50, insulation: 28, torso: true, leftLeg: true, rightLeg: true, leftArm: true, rightArm: true },
              { name: "Heavy Combat Armor Chest", warmth: 35, insulation: 32, torso: true, override: true },
              { name: "Sturdy Leather Arm Guards", warmth: 25, insulation: 18, leftArm: true, rightArm: true },
              { name: "M50 Military Gas Mask", warmth: 20, insulation: 14, head: true }
            ]
          });
        }
        if (window.updateSurvival) {
          window.updateSurvival({ hunger: 18, thirst: 32, sleepDebt: 12, risk: 24, disease: 0 });
        }
        if (window.setNeedsTuning) {
          window.setNeedsTuning({
            RateMultiplier: { v: 1.0, min: 0.1, max: 3.0, o: 0 },
            HourlyHunger: { v: 0.05, min: 0.01, max: 0.2, o: 0 },
            HourlyThirst: { v: 0.1, min: 0.01, max: 0.3, o: 0 },
            HourlySleepDebt: { v: 0.025, min: 0.005, max: 0.1, o: 0 },
            FoodScale: { v: 0.2, min: 0.05, max: 0.5, o: 0 },
            DrinkScale: { v: 0.25, min: 0.05, max: 0.5, o: 0 },
            SleepRecovery: { v: 0.12, min: 0.02, max: 0.3, o: 0 },
            DiseasesEnabled: { v: 1 },
            FilterEnabled: { v: 1 },
            FilterDuration: { v: 300, min: 60, max: 1200, o: 0 },
            FilterAutoReplace: { v: 1 },
            ConditionEnabled: { v: 1 },
            AmbientEnabled: { v: 1 },
            AmbientVolume: { v: 100, min: 0, max: 100, o: 0 },
            StormVolume: { v: 100, min: 0, max: 100, o: 0 },
            StormsEnabled: { v: 1 },
            TemperatureEnabled: { v: 1 }
          });
        }
      }
      loadMockPreview();
    }
  });
}

window.showRadDeck = function(deck) {
  var decks = ["rads", "mask", "storm"];
  var container = document.getElementById("radUniformDecks");
  var pills = document.querySelectorAll(".rad-deck-pill");
  pills.forEach(function(p) {
    p.classList.toggle("active", p.getAttribute("data-deck") === deck);
  });
  if (deck === "all") {
    decks.forEach(function(d) {
      var el = document.getElementById("deck-" + d);
      if (el) el.style.display = "flex";
    });
    if (container) {
      container.style.gridTemplateColumns = "repeat(3, minmax(0, 1fr))";
      container.style.maxWidth = "none";
      container.style.margin = "0";
    }
  } else {
    decks.forEach(function(d) {
      var el = document.getElementById("deck-" + d);
      if (el) el.style.display = (d === deck) ? "flex" : "none";
    });
    if (container) {
      container.style.gridTemplateColumns = "1fr";
      container.style.maxWidth = "640px";
      container.style.margin = "0 auto";
    }
  }
  if (window.pipAudio) window.pipAudio.click();
};
window.toggleArmorMechanics = function() {
  var drawer = document.getElementById("armorMechanicsDrawer");
  var btn = document.getElementById("btnToggleArmorMech");
  if (!drawer) return;
  var isHidden = drawer.style.display === "none";
  drawer.style.display = isHidden ? "grid" : "none";
  if (btn) btn.textContent = isHidden ? "[-] CLOSE GUIDE" : "[?] MECHANICS GUIDE";
  if (window.pipAudio) window.pipAudio.click();
};
window.focusLimb = function(regionName) {
  var regions = ["head", "torso", "leftArm", "rightArm", "leftLeg", "rightLeg"];
  regions.forEach(function(r) {
    var card = document.getElementById("armorRegion-" + r);
    if (card) card.classList.toggle("active-limb", r === regionName);
  });
  var rows = document.querySelectorAll("#armorList .armor-row");
  var labelMap = { head: "HEAD", torso: "TORSO", leftArm: "L ARM", rightArm: "R ARM", leftLeg: "L LEG", rightLeg: "R LEG" };
  var targetLabel = labelMap[regionName];
  rows.forEach(function(row) {
    var covText = (row.children[3] || {}).textContent || "";
    if (covText.indexOf(targetLabel) !== -1) {
      row.classList.add("highlight-limb-row");
    } else {
      row.classList.remove("highlight-limb-row");
    }
  });
  if (window.pipAudio) window.pipAudio.click();
};
