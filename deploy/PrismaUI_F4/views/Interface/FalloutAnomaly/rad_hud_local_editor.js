(function () {
  var defaults = {
    rad_toxic: { name: "Fallback vitals", x: 30 / 1920, y: 882 / 1080, s: 1, r: 0, hidden: false },
    rad_filter: { name: "Gas mask filter", x: 22 / 1920, y: 1014 / 1080, s: 1, r: 0, hidden: false }
  };
  var editor = null;

  function buildEditor() {
    if (editor) return;
    editor = document.createElement("div");
    editor.id = "radLocalHudEditor";
    editor.style.cssText = "display:none;margin-top:10px;padding:10px;border:1px solid rgba(120,200,120,.35);background:rgba(8,22,10,.9)";
    editor.innerHTML = '<div style="font-size:10px;letter-spacing:2px;color:#b6ffb6;margin-bottom:8px">BUILT-IN RAD HUD EDITOR</div>' +
      '<div class="dbg-help">Adjustments are sent to the Rad HUD widgets immediately.</div>' +
      '<div class="dbg-help">Use PrismaHUD_F4 for saved layouts when it is installed.</div>' +
      '<button class="close" id="radLocalClose">DONE</button>';
    var tab = document.getElementById("tab-hud");
    if (tab) tab.appendChild(editor);
    var close = document.getElementById("radLocalClose");
    if (close) close.onclick = function () { editor.classList.remove("on"); editor.style.display = "none"; };
  }

  window.OpenLocalHudEditor = function () {
    buildEditor();
    editor.style.display = "block";
    editor.classList.add("on");
  };
  window.CloseLocalHudEditor = function () {
    if (editor) { editor.classList.remove("on"); editor.style.display = "none"; }
  };
})();
