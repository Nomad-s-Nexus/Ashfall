(function () {
  var uid = 0;

  function clamp(v) {
    return v < 0 ? 0 : v > 1 ? 1 : v;
  }

  function arc(a0, a1) {
    var t0 = (a0 - 90) * Math.PI / 180,
      t1 = (a1 - 90) * Math.PI / 180;
    return "M" + (32 + 28 * Math.cos(t0)).toFixed(2) + " " + (32 + 28 * Math.sin(t0)).toFixed(2) +
      "A28 28 0 " + (a1 - a0 > 180 ? 1 : 0) + " 1 " + (32 + 28 * Math.cos(t1)).toFixed(2) + " " + (32 + 28 * Math.sin(t1)).toFixed(2);
  }

  function ring(h, g, lo, hi) {
    if (g._lo === lo && g._hi === hi) return;
    g._lo = lo;
    g._hi = hi;
    var step = 360 / h.n, a = lo * 360, b = hi * 360;
    for (var k = 0; k < h.n; k++) {
      var x0 = Math.max(k * step + h.gap / 2, a),
        x1 = Math.min((k + 1) * step - h.gap / 2, b);
      g.children[k].setAttribute("d", x1 - x0 > 0.05 ? arc(x0, x1) : "");
    }
  }

  // Constraint: every core is 20 segments with no small variant; see docs/code-notes/Ashfall.md.
  function build(el, icon, label, solid) {
    var id = "pc" + (++uid), n = 20, gap = 3.2, paths = "";
    for (var k = 0; k < n; k++) paths += '<path d=""/>';
    el.classList.add("core");
    el.innerHTML =
      '<svg viewBox="0 0 64 64"><circle class="disc" cx="32" cy="32" r="24.5"/>' +
      '<g class="trk">' + paths + '</g><g class="ghost">' + paths + '</g><g class="val">' + paths + '</g>' +
      '<clipPath id="' + id + '"><rect class="clip" x="-2" y="-1" width="28" height="26"/></clipPath>' +
      '<g transform="translate(19 19) scale(1.083)"><g class="ic dim">' + icon + '</g>' +
      '<g class="ic lit' + (solid ? ' solid' : '') + '" clip-path="url(#' + id + ')" style="clip-path:url(#' + id + ')">' + icon + '</g></g></svg>' +
      '<small>' + label + '<b></b></small>';
    var h = {
      el: el,
      n: n,
      gap: gap,
      val: el.querySelector(".val"),
      ghost: el.querySelector(".ghost"),
      clip: el.querySelector(".clip"),
      num: el.querySelector("small b"),
      v: null,
      fillH: null,
      state: ""
    };
    ring(h, el.querySelector(".trk"), 0, 1);
    return h;
  }

  function set(h, frac, valueText, fillFrac, state) {
    frac = clamp(frac);
    ring(h, h.val, 0, frac);
    if (h.v !== null && frac < h.v) {
      ring(h, h.ghost, 0, h.v);
      clearTimeout(h.t);
      h.t = setTimeout(function () {
        ring(h, h.ghost, 0, frac);
      }, 450);
    } else if (frac !== h.v) ring(h, h.ghost, 0, frac);
    h.v = frac;
    var fh = 26 * clamp(fillFrac === undefined ? 1 : fillFrac);
    if (h.fillH !== fh) {
      h.fillH = fh;
      h.clip.setAttribute("y", (-1 + 26 - fh).toFixed(2));
      h.clip.setAttribute("height", fh.toFixed(2));
    }
    if (valueText !== undefined && h.num.textContent !== String(valueText)) h.num.textContent = valueText;
    if (h.state) h.el.classList.remove(h.state);
    if (state) h.el.classList.add(state);
    h.state = state || "";
  }

  window.PhosCore = { build: build, set: set };
})();
