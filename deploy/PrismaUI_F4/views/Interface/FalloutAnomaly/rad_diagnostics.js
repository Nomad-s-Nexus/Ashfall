(function () {
  function read(json) { var d = typeof json === "string" ? JSON.parse(json) : json; return d && typeof d === "object" ? d : {}; }
  function number(value, digits) { return (Number.isFinite(+value) ? +value : 0).toFixed(digits || 0); }
  function rows(id, values) {
    var list = document.getElementById(id); if (!list) return; list.textContent = "";
    values.forEach(function (pair) { var key = document.createElement("dt"), value = document.createElement("dd"); key.textContent = pair[0]; value.textContent = pair[1]; list.appendChild(key); list.appendChild(value); });
  }
  window.updateClimateInfo = function (json) {
    try {
      var d = read(json), net = (+d.warming || 0) - (+d.cooling || 0);
      rows("climateReadout", [
        ["Core temperature", number(d.core, 1) + " °C"], ["Body warmth", number(d.warmth) + "%"],
        ["Cold stage", ["NORMAL","CHILLED","MILD","MODERATE","SEVERE","CRITICAL"][Math.max(0,Math.min(5,+d.stage||0))]],
        ["Ambient temperature", number(d.ambient, 1) + " °C"], ["Wind estimate", number(d.wind, 1) + " m/s"],
        ["Wetness", number(d.wetness) + "%"], ["Effective insulation", number(d.insulation) + "%"],
        ["Nearby heat", number(d.heat) + "%"], ["Thermal shelter", number(d.shelter) + "%"],
        ["Rain cover", number(d.rainCover) + "%"], ["Environment", d.indoors ? "INTERIOR" : "EXTERIOR"],
        ["Cooling", number(d.cooling, 3) + " °C/min"], ["Warming", number(d.warming, 3) + " °C/min"],
        ["Heat balance", (net > 0 ? "+" : "") + number(net, 3) + " °C/min"]
      ]);
      document.getElementById("climateEffect").textContent = (+d.core >= 37 && net > 0 ? "Normal warmth reached; excess heat is capped." : +d.core <= 28 && net < 0 ? "Core is at the model minimum; cold exposure remains critical." : net > .001 ? "Body is warming." : net < -.001 ? "Body is cooling." : "Body temperature is steady.") + " Cold air, wind and wetness increase cooling. Clothing and shelter reduce it; nearby heat and activity add warmth. Core temperature is capped at 37°C.";
    } catch (e) {}
  };
  window.updateAirInfo = function (json) {
    try {
      var d = read(json), state = !d.contaminated ? "CLEAN" : d.sealed ? "SEALED" : d.sheltered ? "COVERED" : d.masked && !d.failed ? "FILTERED" : "EXPOSED";
      rows("airReadout", [
        ["Air classification", d.contaminated ? "CONTAMINATED WEATHER" : "CLEAN WEATHER"], ["Breathing state", state],
        ["Mask", !d.masked ? "NOT WORN" : d.failed ? "FAILED" : "WORKING"],
        ["Filter reserve", +d.charge < 0 ? "DISABLED" : number(d.charge) + "%"],
        ["Mask condition", +d.condition < 0 ? "DISABLED" : number(d.condition) + "%"], ["Spare filters", number(d.spares)],
        ["Filter drain", number(d.filterDrain, 2) + "%/sec"], ["Breathing HP loss", number(d.healthLoss, 2) + "/sec"],
        ["Failed-mask rads", number(d.radGain, 2) + "/sec"], ["Choking damage", d.chokingEnabled ? "ENABLED" : "DISABLED"]
      ]);
      document.getElementById("airEffect").textContent = state === "EXPOSED" ? "Contaminated air reaches your lungs. Wear a working mask or take shelter. Rates above exclude other storm and radiation damage." : state === "FILTERED" ? "The mask is filtering contaminated air and consuming its reserve. Replace empty filters and repair broken masks." : "No breathing exposure at this location. Filter reserve is not consumed. Air classification follows recognized hazardous weather; it is not a particle sensor.";
    } catch (e) {}
  };
  window.updateVitalsInfo = function (json) {
    try { var d = read(json); rows("vitalsReadout", [["Health", number(d.health, 1) + " / " + number(d.healthMax, 1)], ["Stamina (AP)", number(d.ap, 1) + " / " + number(d.apMax, 1)], ["Radiation", number(d.rads, 1) + " / " + number(d.radsMax)], ["Tissue damage", number(d.tissue, 1)], ["Permanent scarring", number(d.permanentTissue, 1)]]); } catch (e) {}
  };
})();
