window.playRobcoBoot = function(onComplete) {
  var overlay = document.getElementById("robcoBootOverlay");
  var linesBox = document.getElementById("robcoBootLines");
  var promptText = document.getElementById("robcoBootPromptText");
  if (!overlay || !linesBox) {
    if (onComplete) onComplete();
    return;
  }
  overlay.classList.remove("boot-hidden");
  linesBox.innerHTML = "";
  if (promptText) promptText.textContent = "INITIALIZING KERNEL...";
  var steps = [
    { text: "ROM BIOS v4.02 ...", ok: "OK", delay: 90 },
    { text: "MEMORY TEST: 64KB ...", ok: "64KB OK", delay: 200 },
    { text: "BUS CONTROLLER: 8-BIT STEREO ...", ok: "ONLINE", delay: 320 },
    { text: "ATTACHING DOSIMETER & AIR SENSORS ...", ok: "ATTACHED", delay: 450 },
    { text: "LOADING TACTICAL WIREFRAME MESH ...", ok: "SYNCHRONIZED", delay: 580 },
    { text: "CALIBRATING VECTOR RING GAUGES ...", ok: "10/10 READY", delay: 720 },
    { text: "CONNECTING PRISMA-UI PIP-BOY BRIDGE ...", ok: "ONLINE", delay: 860 },
    { text: "ROBCO RAD-OS v1.2.4 READY", ok: "EXEC", delay: 1020 }
  ];
  var timeouts = [];
  var isDone = false;
  function finish() {
    if (isDone) return;
    isDone = true;
    timeouts.forEach(clearTimeout);
    if (window.pipAudio) {
      window.pipAudio.playChirp(880, 0.08, "triangle");
      setTimeout(function() { if (window.pipAudio) window.pipAudio.playChirp(1320, 0.18, "triangle"); }, 90);
    }
    overlay.classList.add("boot-hidden");
    if (typeof window !== "undefined" && window.removeEventListener) window.removeEventListener("keydown", keyHandler);
    if (onComplete) onComplete();
  }
  steps.forEach(function(s, idx) {
    var t = setTimeout(function() {
      if (isDone) return;
      var row = document.createElement("div");
      row.className = "boot-line";
      row.innerHTML = "<span>" + s.text + "</span><span class='boot-line-ok'>" + s.ok + "</span>";
      linesBox.appendChild(row);
      if (promptText) promptText.textContent = s.text;
      if (window.pipAudio) {
        window.pipAudio.playChirp(1050 + (idx * 75), 0.02, "square");
      }
    }, s.delay);
    timeouts.push(t);
  });
  var finalT = setTimeout(finish, 1280);
  timeouts.push(finalT);
  overlay.onclick = finish;
  var keyHandler = function(e) {
    if (e.repeat) return;
    if (e.key !== "Shift" && e.key !== "Control" && e.key !== "Alt") {
      finish();
    }
  };
  if (typeof window !== "undefined" && window.addEventListener) window.addEventListener("keydown", keyHandler);
};
var origSetDebugMenu = window.SetDebugMenu;
window.SetDebugMenu = function(on) {
  var show = ((""+on)==="1" || on===true);
  if (origSetDebugMenu) origSetDebugMenu(on);
  if (window.setEcgActive) window.setEcgActive(show);
  if (window.setGeigerActive) window.setGeigerActive(show);
};

(function() {
  var geigerOn = false;
  var geigerTimer = null;
  var panelOpen = false;
  var currentRads = 85;
  function audioReady() {
    return !!(window.pipAudio && (window.AudioContext || window.webkitAudioContext));
  }
  function clickLoop() {
    clearTimeout(geigerTimer);
    geigerTimer = null;
    if (!geigerOn || !panelOpen || !audioReady() || !window.pipAudio.isEnabled()) return;
    var r = Math.max(0, Math.min(1000, currentRads));
    var pct = r / 1000;
    var minMs, maxMs;
    if (pct < 0.05) {
      minMs = 1500; maxMs = 5000;
    } else if (pct < 0.25) {
      minMs = 350; maxMs = 900;
    } else if (pct < 0.50) {
      minMs = 90; maxMs = 280;
    } else if (pct < 0.75) {
      minMs = 30; maxMs = 90;
    } else {
      minMs = 10; maxMs = 35;
    }
    var delay = minMs + Math.random() * (maxMs - minMs);
    geigerTimer = setTimeout(function() {
      if (window.pipAudio && window.pipAudio.playGeigerClick) {
        window.pipAudio.playGeigerClick();
      }
      clickLoop();
    }, delay);
  }
  window.toggleGeigerAudio = function() {
    geigerOn = !geigerOn;
    var btn = document.getElementById("pipGeigerToggle");
    if (btn) {
      btn.textContent = geigerOn ? "GEIGER: ON" : "GEIGER: OFF";
      btn.classList.toggle("muted", !geigerOn);
      btn.classList.toggle("active", geigerOn);
    }
    clearTimeout(geigerTimer);
    geigerTimer = null;
    if (geigerOn) {
      if (window.pipAudio) window.pipAudio.click();
      clickLoop();
    }
  };
  window.setGeigerActive = function(on) {
    panelOpen = !!on;
    clickLoop();
  };
  window.updateGeigerRadLevel = function(rads) {
    currentRads = Number(rads) || 0;
  };
  // Constraint: clickLoop stops itself while FX is off, so FX toggle-on must restart it (clickLoop clears any pending timer first).
  var baseTogglePipAudio = window.togglePipAudio;
  window.togglePipAudio = function() {
    if (baseTogglePipAudio) baseTogglePipAudio();
    if (window.pipAudio && window.pipAudio.isEnabled()) clickLoop();
  };
})();
if (window.pipAudio) {
  window.pipAudio.playGeigerClick = function() {
    if (!this.isEnabled()) return;
    try {
      this.playChirp(2600 + Math.random() * 800, 0.008, 'sawtooth');
    } catch(e) {}
  };
}
(function() {
  var canvas, ctx;
  var heartRate = 72;
  var currentHealthPct = 88;
  var currentRads = 85;
  var animFrame = null;
  var waveX = 0;
  var lastTime = 0;
  var lastDraw = 0;
  var dotOffAt = 0;
  var FRAME_MS = 28;
  var pulsePhase = 0;
  var trailHistory = [];
  var MAX_TRAIL = 230;
  function initEcg() {
    canvas = document.getElementById("ecgCanvas");
    if (!canvas || !canvas.getContext) return;
    ctx = canvas.getContext("2d");
    for (var i = 0; i < MAX_TRAIL; i++) {
      trailHistory.push(canvas.height / 2);
    }
  }
  function stopLoop() {
    if (animFrame) cancelAnimationFrame(animFrame);
    animFrame = null;
  }
  function startLoop() {
    if (animFrame || !ctx) return;
    lastTime = 0;
    lastDraw = 0;
    function render(time) {
      animFrame = requestAnimationFrame(render);
      if (!canvas || !ctx) return;
      if (time - lastDraw < FRAME_MS) return;
      var dt = lastTime ? (time - lastTime) / 1000 : 0.016;
      lastTime = time;
      lastDraw = time;
      if (isNaN(dt) || dt > 0.1) dt = 0.016;
      var freq = heartRate / 60;
      pulsePhase += dt * freq * (Math.PI * 2);
      if (pulsePhase > Math.PI * 2) {
        pulsePhase -= Math.PI * 2;
        var dot = document.getElementById("ecgPulseDot");
        if (dot) dot.style.opacity = "1";
        dotOffAt = time + 90;
      }
      if (dotOffAt && time >= dotOffAt) {
        dotOffAt = 0;
        var dotOff = document.getElementById("ecgPulseDot");
        if (dotOff) dotOff.style.opacity = "0.3";
      }
      var h = canvas.height;
      var mid = h / 2;
      var y = mid;
      var p = pulsePhase;
      if (currentHealthPct <= 0) {
        y = mid + (Math.random() - 0.5) * 1.5;
      } else {
        if (p >= 0.2 && p < 0.6) {
          y -= Math.sin((p - 0.2) / 0.4 * Math.PI) * (h * 0.14);
        }
        else if (p >= 0.8 && p < 0.95) {
          y += Math.sin((p - 0.8) / 0.15 * Math.PI) * (h * 0.10);
        }
        else if (p >= 0.95 && p < 1.25) {
          y -= Math.sin((p - 0.95) / 0.30 * Math.PI) * (h * 0.45);
        }
        else if (p >= 1.25 && p < 1.45) {
          y += Math.sin((p - 1.25) / 0.20 * Math.PI) * (h * 0.22);
        }
        else if (p >= 1.8 && p < 2.5) {
          y -= Math.sin((p - 1.8) / 0.70 * Math.PI) * (h * 0.20);
        }
        y += (Math.random() - 0.5) * 1.5;
      }
      trailHistory.push(y);
      if (trailHistory.length > MAX_TRAIL) trailHistory.shift();
      ctx.clearRect(0, 0, canvas.width, canvas.height);
      ctx.strokeStyle = "rgba(62, 207, 74, 0.08)";
      ctx.lineWidth = 1;
      ctx.beginPath();
      for (var gx = 0; gx < canvas.width; gx += 20) {
        ctx.moveTo(gx, 0); ctx.lineTo(gx, canvas.height);
      }
      for (var gy = 0; gy < canvas.height; gy += 10) {
        ctx.moveTo(0, gy); ctx.lineTo(canvas.width, gy);
      }
      ctx.stroke();
      var strokeColor = "rgb(62, 207, 74)";
      if (currentHealthPct <= 25 || currentRads >= 750) {
        strokeColor = "rgb(255, 60, 60)";
      } else if (currentHealthPct <= 50 || currentRads >= 400) {
        strokeColor = "rgb(255, 150, 40)";
      } else if (currentHealthPct <= 75 || currentRads >= 200) {
        strokeColor = "rgb(240, 200, 50)";
      }
      ctx.strokeStyle = strokeColor;
      ctx.lineWidth = 1.8;
      ctx.beginPath();
      var step = canvas.width / (MAX_TRAIL - 1);
      for (var idx = 0; idx < trailHistory.length; idx++) {
        var tx = idx * step;
        var ty = trailHistory[idx];
        if (idx === 0) ctx.moveTo(tx, ty);
        else ctx.lineTo(tx, ty);
      }
      ctx.stroke();
      var headX = (trailHistory.length - 1) * step;
      var headY = trailHistory[trailHistory.length - 1];
      ctx.fillStyle = "#ffffff";
      ctx.beginPath();
      ctx.arc(headX, headY, 2.5, 0, Math.PI * 2);
      ctx.fill();
    }
    animFrame = requestAnimationFrame(render);
  }
  window.setEcgActive = function(on) {
    if (on) startLoop(); else stopLoop();
  };
  window.updateEcgVitals = function(hp, hpMax, rads) {
    if (rads !== null && rads !== undefined) currentRads = Number(rads) || 0;
    if (hp !== null && hp !== undefined) {
      var maxH = Number(hpMax) || 220;
      currentHealthPct = Math.round(((Number(hp) || 0) / maxH) * 100);
    }
    if (currentHealthPct <= 0) {
      heartRate = 0;
    } else {
      var baseBpm = 72;
      if (currentHealthPct < 50) baseBpm += (50 - currentHealthPct) * 1.1;
      if (currentRads > 200) baseBpm += (currentRads / 1000) * 35;
      heartRate = Math.round(Math.min(165, Math.max(45, baseBpm)));
    }
    var bpmEl = document.getElementById("ecgBpmText");
    if (bpmEl) {
      var rhythm = heartRate === 0 ? "ASYSTOLE (FLATLINE)" :
                   heartRate > 125 ? "TACHYCARDIA ALERT" :
                   heartRate > 95  ? "ELEVATED STRESS" : "NOMINAL RHYTHM";
      bpmEl.textContent = (heartRate ? heartRate + " BPM" : "0 BPM") + " • " + rhythm;
      bpmEl.style.color = heartRate > 125 ? "var(--pip-crit)" : heartRate > 95 ? "var(--pip-amber)" : "var(--pip-primary)";
    }
  };
  if (typeof document !== "undefined" && typeof window !== "undefined" && window.requestAnimationFrame) {
    if (document.readyState === "loading" && document.addEventListener) {
      document.addEventListener("DOMContentLoaded", initEcg);
    } else {
      initEcg();
    }
  }
})();
window.initDialScrubbers = function() {
  var dials = document.querySelectorAll(".overview-dial");
  dials.forEach(function(dial) {
    var type = dial.getAttribute("data-type");
    if (!type) return;
    var isDown = false;
    var startX = 0, startVal = 50;
    dial.addEventListener("mousedown", function(e) {
      isDown = true;
      startX = e.clientX;
      dial.classList.add("dial-scrubbing");
      startVal = parseFloat(dial.getAttribute("data-pct"));
      if (!isFinite(startVal)) startVal = 50;
      function onMove(me) {
        if (!isDown) return;
        var dx = me.clientX - startX;
        var deltaPct = dx * 0.4;
        var newPct = Math.max(0, Math.min(100, Math.round(startVal + deltaPct)));
        if (type === "health") {
          var hp = Math.round((newPct / 100) * 220);
          window.updatePipDial("health", newPct, newPct + "%", hp + " / 220");
          if (window.updateEcgVitals) window.updateEcgVitals(hp, 220, null);
        } else if (type === "ap") {
          var ap = Math.round((newPct / 100) * 130);
          window.updatePipDial("ap", newPct, newPct + "%", ap + " / 130");
        } else if (type === "rads") {
          var rads = Math.round((newPct / 100) * 1000);
          window.updatePipDial("rads", newPct, rads, newPct + "% DOSE");
          if (window.updateGeigerRadLevel) window.updateGeigerRadLevel(rads);
          if (window.updateEcgVitals) window.updateEcgVitals(null, null, rads);
        } else if (type === "tissue") {
          window.updatePipDial("tissue", newPct, newPct, newPct + "% TISSUE");
        } else if (type === "warmth") {
          var temp = (28 + (newPct / 100) * 9).toFixed(1);
          window.updatePipDial("warmth", newPct, temp + "°C", newPct + "% WARMTH");
        } else if (type === "filter") {
          window.updatePipDial("filter", newPct, newPct + "%", "4 SPARES");
        } else {
          window.updatePipDial(type, newPct, newPct + "%");
        }
      }
      function onUp() {
        if (!isDown) return;
        isDown = false;
        dial.classList.remove("dial-scrubbing");
        if (typeof window !== "undefined" && window.removeEventListener) window.removeEventListener("mousemove", onMove);
        if (typeof window !== "undefined" && window.removeEventListener) window.removeEventListener("mouseup", onUp);
        if (window.pipAudio) window.pipAudio.click();
      }
      window.addEventListener("mousemove", onMove);
      window.addEventListener("mouseup", onUp);
    });
  });
};
window.triggerScenario = function(name) {
  if (name === "rad_burst") {
    if (window.updateVitalsInfo) {
      window.updateVitalsInfo({ health: 135, healthMax: 220, ap: 75, apMax: 130, rads: 850, radsMax: 1000, tissue: 65, permanentTissue: 25 });
    }
    if (window.updateGeigerRadLevel) window.updateGeigerRadLevel(850);
  } else if (name === "freeze") {
    if (window.updateClimateInfo) {
      window.updateClimateInfo({ core: 29.2, warmth: 12, stage: 4, ambient: -6.5, wind: 8.4, wetness: 85, insulation: 20, cooling: 0.08, warming: 0.01 });
    }
    if (window.updateVitalsInfo) {
      window.updateVitalsInfo({ health: 140, healthMax: 220, ap: 60, apMax: 130, rads: 120, radsMax: 1000, tissue: 15, permanentTissue: 5 });
    }
  } else if (name === "mask_breach") {
    if (window.updateAirInfo) {
      window.updateAirInfo({ contaminated: true, masked: true, failed: true, condition: 0, charge: 0, spares: 0, filterDrain: 0, healthLoss: 4.5, radGain: 6.0, chokingEnabled: true });
    }
  } else if (name === "nominal") {
    if (window.updateClimateInfo) {
      window.updateClimateInfo({ core: 37.0, warmth: 95, stage: 0, ambient: 21.0, wind: 1.5, wetness: 0, insulation: 80, cooling: 0.01, warming: 0.02 });
    }
    if (window.updateAirInfo) {
      window.updateAirInfo({ contaminated: false, masked: true, failed: false, condition: 100, charge: 100, spares: 5, filterDrain: 0, healthLoss: 0, radGain: 0, chokingEnabled: false });
    }
    if (window.updateVitalsInfo) {
      window.updateVitalsInfo({ health: 220, healthMax: 220, ap: 130, apMax: 130, rads: 0, radsMax: 1000, tissue: 0, permanentTissue: 0 });
    }
    if (window.updateGeigerRadLevel) window.updateGeigerRadLevel(0);
  }
  if (window.pipAudio) window.pipAudio.alert();
};
window.initArmorInspector = function() {
  var regions = ["head", "torso", "leftArm", "rightArm", "leftLeg", "rightLeg"];
  regions.forEach(function(r) {
    var regCard = document.getElementById("armorRegion-" + r);
    if (!regCard) return;
    regCard.addEventListener("mouseenter", function() {
      highlightArmorLimb(r, true);
    });
    regCard.addEventListener("mouseleave", function() {
      highlightArmorLimb(r, false);
    });
    regCard.addEventListener("click", function() {
      highlightArmorLimb(r, true);
      if (window.pipAudio) window.pipAudio.click();
    });
  });
};
function highlightArmorLimb(regionName, on) {
  var card = document.getElementById("armorRegion-" + regionName);
  if (card) card.classList.toggle("active-limb", on);
  var rows = document.querySelectorAll("#armorList .armor-row");
  var labelMap = { head: "HEAD", torso: "TORSO", leftArm: "L ARM", rightArm: "R ARM", leftLeg: "L LEG", rightLeg: "R LEG" };
  var targetLabel = labelMap[regionName];
  rows.forEach(function(row) {
    var covText = (row.children[3] || {}).textContent || "";
    if (on && covText.indexOf(targetLabel) !== -1) {
      row.classList.add("highlight-limb-row");
    } else {
      row.classList.remove("highlight-limb-row");
    }
  });
}
if (typeof window !== "undefined" && window.addEventListener) {
  window.addEventListener("DOMContentLoaded", function() {
    if (window.initDialScrubbers) window.initDialScrubbers();
    if (window.initArmorInspector) window.initArmorInspector();
  });
}
