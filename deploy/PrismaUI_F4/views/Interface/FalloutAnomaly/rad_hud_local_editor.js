(function () {
  var defaults = {
    rad_toxic: { name: "Fallback vitals: Health + Rads / Stamina (AP) / Tissue", x: 30 / 1920, y: 882 / 1080, s: 1, r: 0, hidden: false },
    rad_filter: { name: "Gas mask filter", x: 22 / 1920, y: 1014 / 1080, s: 1, r: 0, hidden: false },
    rad_needs: { name: "Needs: hunger / thirst / fatigue", x: 22 / 1920, y: 812 / 1080, s: 1, r: 0, hidden: false },
    rad_climate: { name: "Climate: CORE / AIR / WET / WARM / HEAT", x: 22 / 1920, y: 944 / 1080, s: 1, r: 0, hidden: false }
  };
  var editor = null;
  var status = null;
  var layout = null;
  var temperatureUnit = "C";

  function cloneDefaults() {
    return JSON.parse(JSON.stringify(defaults));
  }

  function mergeLayout(input) {
    var out = cloneDefaults();
    if (!input || typeof input !== "object") return out;
    Object.keys(out).forEach(function (id) {
      if (!input[id]) return;
      ["x", "y", "s", "r", "hidden"].forEach(function (key) {
        if (typeof input[id][key] !== "undefined") out[id][key] = input[id][key];
      });
    });
    return out;
  }

  function queueLayout(command) {
    if (!window.radHudLayout) return;
    window.radHudLayout(JSON.stringify(command));
  }

  function sendUpdate(id, save) {
    if (!layout || !layout[id]) return;
    var cfg = layout[id];
    queueLayout({
      action: "update",
      widget: id,
      x: cfg.x,
      y: cfg.y,
      s: cfg.s,
      r: cfg.r,
      hidden: !!cfg.hidden,
      save: !!save
    });
    if (status) status.textContent = save ? "SAVING..." : "LIVE PREVIEW";
  }

  function setPreview(on) {
    queueLayout({ action: "preview", on: !!on });
  }

  function setUnit(unit, save) {
    temperatureUnit = unit === "F" ? "F" : "C";
    updateUnitToggle();
    queueLayout({ action: "unit", unit: temperatureUnit, save: !!save });
    if (status) status.textContent = save ? "SAVING..." : "LIVE PREVIEW";
  }

  function updateUnitToggle() {
    if (!editor) return;
    var toggle = editor.querySelector("[data-unit-toggle]");
    if (!toggle) return;
    toggle.textContent = temperatureUnit === "F" ? "FAHRENHEIT (°F)" : "CELSIUS (°C)";
    toggle.setAttribute("data-unit", temperatureUnit);
    toggle.setAttribute("aria-pressed", temperatureUnit === "F" ? "true" : "false");
  }

  function percent(v) {
    return Math.round(v * 1000) / 10;
  }

  function updateReadout(card, id) {
    var cfg = layout[id];
    card.querySelector('[data-readout="x"]').textContent = percent(cfg.x) + "%";
    card.querySelector('[data-readout="y"]').textContent = percent(cfg.y) + "%";
    card.querySelector('[data-readout="s"]').textContent = Math.round(cfg.s * 100) + "%";
    card.querySelector('[data-readout="r"]').textContent = Math.round(cfg.r) + "°";
  }

  function bindCard(card) {
    var id = card.getAttribute("data-widget");
    var cfg = layout[id];
    Array.prototype.forEach.call(card.querySelectorAll("input[data-field]"), function (input) {
      var field = input.getAttribute("data-field");
      if (input.type === "checkbox") input.checked = !!cfg[field];
      else input.value = cfg[field];
      input.addEventListener("input", function () {
        if (input.type === "checkbox") cfg[field] = !!input.checked;
        else cfg[field] = +input.value;
        updateReadout(card, id);
        sendUpdate(id, false);
      });
      input.addEventListener("change", function () {
        if (input.type === "checkbox") cfg[field] = !!input.checked;
        else cfg[field] = +input.value;
        updateReadout(card, id);
        sendUpdate(id, true);
      });
    });
    var unitToggle = card.querySelector("[data-unit-toggle]");
    if (unitToggle) {
      unitToggle.onclick = function () { setUnit(temperatureUnit === "F" ? "C" : "F", true); };
      updateUnitToggle();
    }
    updateReadout(card, id);
  }

  function cardHtml(id) {
    return '<div class="rad-local-card" data-widget="' + id + '">' +
      '<div class="rad-local-name">' + defaults[id].name + '</div>' +
      (id === "rad_climate" ? '<div class="rad-local-unit-row"><span>TEMPERATURE UNIT</span><button type="button" class="rad-local-unit-toggle" data-unit-toggle="1" data-unit="C" aria-pressed="false">CELSIUS (°C)</button></div>' : '') +
      controlHtml("X", "x", 0, 0.95, 0.005) +
      controlHtml("Y", "y", 0, 0.95, 0.005) +
      controlHtml("SIZE", "s", 0.5, 2.5, 0.05) +
      controlHtml("ROTATE", "r", -180, 180, 5) +
      '<label class="rad-local-check"><input type="checkbox" data-field="hidden"> HIDE WIDGET</label>' +
      '</div>';
  }

  function controlHtml(label, field, min, max, step) {
    return '<label class="rad-local-control"><span>' + label + '</span><input type="range" data-field="' + field + '" min="' + min + '" max="' + max + '" step="' + step + '"><span data-readout="' + field + '"></span></label>';
  }

  function buildEditor() {
    if (editor) return;
    var style = document.createElement("style");
    style.textContent = "#radLocalHudEditor{display:none;margin-top:10px;padding:10px;border:1px solid rgba(120,200,120,.35);background:rgba(8,22,10,.9)}#radLocalHudEditor.on{display:block}.rad-local-title{font-size:10px;letter-spacing:2px;color:#b6ffb6;margin-bottom:8px}.rad-local-card{padding:8px;margin:7px 0;border:1px solid rgba(120,200,120,.2);background:rgba(12,30,14,.75)}.rad-local-name{font-size:10px;color:#d9ffd9;margin-bottom:6px}.rad-local-control{display:grid;grid-template-columns:62px 1fr 52px;gap:6px;align-items:center;margin:5px 0;font-size:9px;color:#a8c8a8}.rad-local-control input[type=range]{width:100%}.rad-local-unit-row{display:flex;align-items:center;justify-content:space-between;gap:8px;margin:6px 0 8px;padding:6px 0;border-top:1px solid rgba(120,200,120,.16);border-bottom:1px solid rgba(120,200,120,.16);font-size:9px;color:#a8c8a8}.rad-local-unit-toggle{flex:0 0 auto;min-width:132px;padding:5px 8px!important;color:#cdfbcf!important;background:rgba(20,60,24,.9)!important;border:1px solid rgba(120,200,120,.55)!important}.rad-local-unit-toggle:hover,.rad-local-unit-toggle:focus{background:rgba(70,145,76,.95)!important;border-color:rgba(180,255,180,.9)!important;box-shadow:0 0 8px rgba(60,255,90,.65)!important}.rad-local-unit-toggle[data-unit=F]{color:#fff!important;background:rgba(60,120,64,.9)!important}.rad-local-check{display:flex;align-items:center;gap:7px;margin-top:6px;font-size:9px;color:#d8b97a}.rad-local-actions{display:flex;gap:5px;margin-top:8px}.rad-local-status{margin-top:7px;font-size:9px;color:#7fd07f;letter-spacing:1px}";
    document.head.appendChild(style);

    editor = document.createElement("div");
    editor.id = "radLocalHudEditor";
    editor.innerHTML =
      '<div class="rad-local-title">BUILT-IN RAD HUD EDITOR</div>' +
      '<div class="dbg-help">Without PrismaHUD, Rad supplies Health with radiation loss, Stamina/AP, Tissue, and the gas-mask filter. PrismaHUD takes over automatically when installed.</div>' +
      cardHtml("rad_toxic") + cardHtml("rad_filter") + cardHtml("rad_climate") + cardHtml("rad_needs") +
      '<div class="rad-local-actions"><button id="radLocalSave">SAVE</button><button id="radLocalReset" class="warn">RESET</button><button id="radLocalClose" class="close">DONE</button></div>' +
      '<div class="rad-local-status" id="radLocalStatus">READY</div>';
    document.getElementById("tab-hud").appendChild(editor);
    status = document.getElementById("radLocalStatus");

    document.getElementById("radLocalSave").onclick = function () {
      sendUpdate("rad_toxic", true);
      sendUpdate("rad_filter", true);
      sendUpdate("rad_climate", true);
      sendUpdate("rad_needs", true);
      status.textContent = "SAVING...";
    };
    document.getElementById("radLocalReset").onclick = function () {
      layout = cloneDefaults();
      rebuildCards();
      sendUpdate("rad_toxic", true);
      sendUpdate("rad_filter", true);
      sendUpdate("rad_climate", true);
      sendUpdate("rad_needs", true);
      status.textContent = "SAVING...";
    };
    document.getElementById("radLocalClose").onclick = function () {
      window.CloseLocalHudEditor();
      setPreview(false);
    };
  }

  function rebuildCards() {
    Array.prototype.forEach.call(editor.querySelectorAll(".rad-local-card"), function (card) {
      var replacement = document.createElement("div");
      replacement.innerHTML = cardHtml(card.getAttribute("data-widget"));
      var next = replacement.firstChild;
      card.parentNode.replaceChild(next, card);
      bindCard(next);
    });
  }

  window.OpenLocalHudEditor = function (json) {
    var parsed = {};
    try { parsed = (typeof json === "string") ? JSON.parse(json) : json; } catch (e) {}
    layout = mergeLayout(parsed);
    temperatureUnit = parsed && parsed.temperatureUnit === "F" ? "F" : "C";
    buildEditor();
    rebuildCards();
    editor.classList.add("on");
    status.textContent = "READY";
  };

  window.CloseLocalHudEditor = function () {
    if (editor) editor.classList.remove("on");
  };
})();
