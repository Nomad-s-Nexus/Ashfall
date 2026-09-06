from pathlib import Path

root = Path(__file__).resolve().parents[1]
views = root / "deploy" / "PrismaUI_F4" / "views" / "Interface" / "FalloutAnomaly"

rad_hud = (root / "src" / "RadHud.cpp").read_text(encoding="utf-8")
rad_hud_header = (root / "src" / "RadHud.h").read_text(encoding="utf-8")
rad_debug = (root / "src" / "RadEngine_Debug.cpp").read_text(encoding="utf-8")
rad_layout = (root / "src" / "RadHudLayout.cpp").read_text(encoding="utf-8")
hooks_input = (root / "src" / "Hooks_Input.cpp").read_text(encoding="utf-8")
radiation_engine = (root / "src" / "RadiationEngine.cpp").read_text(encoding="utf-8")
editor_js = (views / "rad_hud_local_editor.js").read_text(encoding="utf-8")
toxic = (views / "hud_toxic.html").read_text(encoding="utf-8")
filter_html = (views / "hud_filter.html").read_text(encoding="utf-8")
main_html = (views / "radhud.html").read_text(encoding="utf-8")

assert 'BindGameThreadUIEvent' not in rad_hud
assert 'RegisterJSListener(v, "radDebug", OnRadDebugCommand)' in rad_hud
assert '_toxicWidgetRegistered.store(_hudApi != nullptr);' in rad_hud
assert 'const bool fallback = !_toxicWidgetRegistered.load();' in rad_hud
assert rad_hud.count('SyncLocalHudLayout();') >= 2
assert 'HUD layout editor requires PrismaHUD_F4' not in rad_hud
assert 'void RadHud::OpenHudLayoutEditor()' not in rad_hud

views_ready = rad_hud_header.split('[[nodiscard]] bool ViewsReady() const', 1)[1].split('private:', 1)[0]
for token in (
    '_viewsReady.load()',
    '_domReady.load()',
    '_toxicDomReady.load()',
    '_filterDomReady.load()',
):
    assert token in views_ready, f'HUD readiness must include replacement DOM state: {token}'

assert 'a.rfind("hudlayout:", 0) == 0' in rad_debug
assert 'ApplyLocalHudLayout(a.substr(std::strlen("hudlayout:")))' in rad_debug
assert 'cmds.swap(_pendingDebugCmds);' in rad_debug
assert 'DispatchDebugCommand(c);' in rad_debug

assert rad_layout.count('void RadHud::OpenHudLayoutEditor()') == 1
assert 'RegisterJSListener(_view, "radHudLayout", OnRadHudLayoutCommand)' in rad_layout
assert 'RadiationEngine::GetSingleton().HandleDebugCommand(command.c_str())' in rad_layout
assert 'if (action == "sync")' in rad_layout
assert 'if (_hudApi)' in rad_layout
assert '_hudEditorApi = PRISMA_HUD_API::RequestEditorAPI();' in rad_layout
assert 'editor->OpenEditor();' in rad_layout
assert 'api->InteropCall(_view, "OpenLocalHudEditor", layout.c_str())' in rad_layout
assert 'api->InteropCall(_toxicView, "setLocalHUDLayout", payload.c_str())' in rad_layout
assert 'api->InteropCall(_filterView, "setLocalHUDLayout", payload.c_str())' in rad_layout
assert 'api->InteropCall(_toxicView, "releaseLocalHUDLayout", "")' in rad_layout
assert 'api->InteropCall(_filterView, "releaseLocalHUDLayout", "")' in rad_layout
assert 'hud_layout.json' in rad_layout

poll_input = radiation_engine.split("void RadiationEngine::PollInput()", 1)[1].split(
    "namespace\n{\n    constexpr std::uint32_t kStormClockRecord", 1
)[0]
assert poll_input.count("RadHud::GetSingleton().ToggleDebugMenu();") == 1
assert "RadHud::GetSingleton().ToggleDebugMenu();" not in hooks_input
assert "const_cast<RE::ButtonEvent*>(e)->handled = RE::InputEvent::HANDLED_RESULT::kStop;" in hooks_input

assert 'Fallback vitals: Health + Rads / Stamina (AP) / Tissue' in editor_js
assert 'Without PrismaHUD, Rad supplies Health with radiation loss, Stamina/AP, Tissue' in editor_js
assert 'window.radHudLayout(JSON.stringify(command))' in editor_js
assert 'window.radDebug("hudlayout:"' not in editor_js
assert 'localStorage' not in editor_js

for token in ('>HEALTH<', '>STAMINA (AP)<', '>TISSUE<', 'id="hRads"'):
    assert token in toxic
assert 'window.setLocalHUDLayout=function(json)' in toxic
assert 'window.releaseLocalHUDLayout=function()' in toxic
assert 'setHealthVital(hFill,hRads,hNum' in toxic
assert 'applyFallbackPresentation(_fallbackState||_preview)' in toxic

for html in (toxic, filter_html):
    assert 'window.setHUDPosition=function(json)' in html
    assert 'window.setEditorPreview=function(json)' in html
    assert 'window.setLocalHUDLayout=function(json)' in html
    assert 'window.releaseLocalHUDLayout=function()' in html
    assert 'rad_hud_local_widget.js' not in html

assert not (views / 'rad_hud_local_widget.js').exists()
assert '<script src="rad_hud_local_editor.js"></script>' in main_html
assert "cmd('hud:open')" in main_html

print("Rad native fallback HUD ownership, readiness, and single debug-hotkey guard passed")
