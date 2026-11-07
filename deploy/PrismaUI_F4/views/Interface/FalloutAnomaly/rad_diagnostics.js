(function () {
  function read(json) { var d = typeof json === "string" ? JSON.parse(json) : json; return d && typeof d === "object" ? d : {}; }
  function number(value, digits) { return (Number.isFinite(+value) ? +value : 0).toFixed(digits || 0); }
  
  function rows(id, values) {
    var list = document.getElementById(id);
    if (!list) return;
    list.textContent = "";
    values.forEach(function (pair) {
      var key = document.createElement("dt");
      var val = document.createElement("dd");
      key.textContent = pair[0];
      val.textContent = pair[1];
      list.appendChild(key);
      list.appendChild(val);
    });
  }
  window.updateClimateInfo = function (json) {
    try {
      var d = read(json), net = (+d.warming || 0) - (+d.cooling || 0);
      var warmth = Math.max(0, Math.min(100, Math.round(+d.warmth || 0)));
      rows("climateReadout", [
        ["Core temp", number(d.core, 1) + " °C"],
        ["Body warmth", number(d.warmth) + "%"],
        ["Ambient", number(d.ambient, 1) + " °C"],
        ["Wind", number(d.wind, 1) + " m/s"],
        ["Cold stage", ["NORMAL","CHILLED","MILD","MODERATE","SEVERE","CRITICAL"][Math.max(0,Math.min(5,+d.stage||0))]],
        ["Wetness", number(d.wetness) + "%"],
        ["Effective insulation", number(d.insulation) + "%"],
        ["Nearby heat", number(d.heat) + "%"],
        ["Thermal shelter", number(d.shelter) + "%"],
        ["Rain cover", number(d.rainCover) + "%"],
        ["Environment", d.indoors ? "INTERIOR" : "EXTERIOR"],
        ["Cooling", number(d.cooling, 3) + " °C/min"],
        ["Warming", number(d.warming, 3) + " °C/min"],
        ["Heat balance", (net > 0 ? "+" : "") + number(net, 3) + " °C/min"]
      ]);
      var eff = document.getElementById("climateEffect");
      if (eff) {
        eff.textContent = (+d.core >= 37 && net > 0 ? "Normal warmth reached; excess heat is capped." : +d.core <= 28 && net < 0 ? "Core is at the model minimum; cold exposure remains critical." : net > .001 ? "Body is warming." : net < -.001 ? "Body is cooling." : "Body temperature is steady.") + " Cold air, wind and wetness increase cooling; shelter and fire increase warming.";
      }
      
      if (window.updatePipDial) {
        window.updatePipDial("warmth", warmth, warmth + "%", number(d.core, 1) + " °C CORE");
      }
      var ovTemp = document.getElementById("ov-val-temp");
      if (ovTemp) ovTemp.textContent = number(d.core, 1) + " °C";
      var ovWet = document.getElementById("ov-val-wetness");
      if (ovWet) ovWet.textContent = Math.round(+d.wetness || 0) + "%";
      var minTemp = document.getElementById("t-min-temp");
      if (minTemp) minTemp.textContent = number(d.core, 1) + "°C (" + (net > 0 ? "WARM" : net < 0 ? "COLD" : "NOM") + ")";
    } catch (e) {}
  };
  window.updateAirInfo = function (json) {
    try {
      var d = read(json);
      var state = !d.masked ? "EXPOSED" : d.failed ? "FAILED" : "FILTERED";
      var ch = Math.max(0, Math.min(100, Math.round(+d.charge || 0)));
      var cond = Math.max(0, Math.min(100, Math.round(+d.condition || 0)));
      rows("airReadout", [
        ["Air class", d.contaminated ? "CONTAMINATED" : "CLEAN"],
        ["Breathing", state],
        ["Choking", d.chokingEnabled ? "ENABLED" : "DISABLED"],
        ["Filter drain", number(d.filterDrain, 2) + "%/sec"],
        ["Breathing HP loss", number(d.healthLoss, 2) + "/sec"],
        ["Rad gain", number(d.radGain, 2) + "/sec"]
      ]);
      var eff = document.getElementById("airEffect");
      if (eff) {
        eff.textContent = state === "EXPOSED" ? "Contaminated air reaches your lungs. Wear a working mask or take shelter. Rates above exclude other storm and radiation damage." : state === "FILTERED" ? "The mask is filtering contaminated air and consuming its reserve. Replace empty filters and repair broken masks." : "No breathing exposure at this location. Filter reserve is not consumed. Air classification follows recognized hazardous weather; it is not a particle sensor.";
      }
      
      if (window.updatePipDial) {
        window.updatePipDial("filter", ch, (+d.charge < 0 ? "OFF" : ch + "%"), d.spares + " SPARES");
        window.updatePipDial("maskcond", cond, (+d.condition < 0 ? "OFF" : cond + "%"), "CONDITION");
      }
      var maskMeter = document.getElementById("dial-meter-maskcond");
      if (maskMeter) maskMeter.style.width = cond + "%";
      var maskCondVal = document.getElementById("dial-val-maskcond");
      if (maskCondVal) maskCondVal.textContent = cond + "%";
      var airClassEl = document.getElementById("ov-air-class");
      if (airClassEl) {
        airClassEl.textContent = d.contaminated ? "CONTAMINATED WEATHER" : "CLEAN WEATHER";
        airClassEl.className = d.contaminated ? "pip-tag-warn" : "pip-tag-good";
      }
      var ovHead = document.getElementById("ov-val-head");
      if (ovHead) ovHead.textContent = !d.masked ? "NO MASK" : d.failed ? "MASK FAILED" : "FILTER WORKING";
      var ovState = document.getElementById("ov-val-state");
      if (ovState) {
        ovState.textContent = state;
        ovState.className = state === "FILTERED" || state === "CLEAN" ? "pip-tag-good" : "pip-tag-warn";
      }
      var minAir = document.getElementById("t-min-air");
      if (minAir) {
        minAir.textContent = state + " (" + ch + "%)";
        minAir.className = (state === "FILTERED" || state === "CLEAN") ? "pip-tag-good" : "pip-tag-warn";
      }
    } catch (e) {}
  };
  window.updateVitalsInfo = function (json) {
    try {
      var d = read(json);
      var maxHp = +d.healthMax || 1;
      var maxAp = +d.apMax || 1;
      var maxRads = +d.radsMax || 1000;
      var hpPct = Math.max(0, Math.min(100, Math.round((+d.health / maxHp) * 100)));
      var apPct = Math.max(0, Math.min(100, Math.round((+d.ap / maxAp) * 100)));
      var radPct = Math.max(0, Math.min(100, Math.round((+d.rads / maxRads) * 100)));
      var tissuePct = Math.max(0, Math.min(100, Math.round((+d.tissue || 0) / 10)));
      rows("vitalsReadout", [
        ["Health", number(d.health, 1) + " / " + number(d.healthMax, 1)],
        ["Stamina (AP)", number(d.ap, 1) + " / " + number(d.apMax, 1)],
        ["Radiation", number(d.rads, 1) + " / " + number(d.radsMax)],
        ["Tissue damage", number(d.tissue, 1)],
        ["Permanent scarring", number(d.permanentTissue, 1)]
      ]);
      
      if (window.updatePipDial) {
        window.updatePipDial("health", hpPct, hpPct + "%", Math.round(d.health) + " / " + Math.round(d.healthMax));
        window.updatePipDial("ap", apPct, apPct + "%", Math.round(d.ap) + " / " + Math.round(d.apMax));
        window.updatePipDial("rads", radPct, Math.round(d.rads), radPct + "% DOSE");
        window.updatePipDial("tissue", tissuePct, Math.round(d.tissue), tissuePct + "% TISSUE");
      }
      var minRads = document.getElementById("t-min-rads");
      if (minRads) minRads.textContent = number(d.rads, 1) + " / " + number(d.radsMax);
      var minHp = document.getElementById("t-min-hp");
      if (minHp) minHp.textContent = Math.round(d.health) + "/" + Math.round(d.healthMax) + " HP";
      var minAp = document.getElementById("t-min-ap");
      if (minAp) minAp.textContent = Math.round(d.ap) + "/" + Math.round(d.apMax) + " AP";
      if (window.updateEcgVitals) {
        window.updateEcgVitals(d.health, d.healthMax, d.rads);
      }
      if (window.updateGeigerRadLevel) {
        window.updateGeigerRadLevel(d.rads);
      }
    } catch (e) {}
  };
})();