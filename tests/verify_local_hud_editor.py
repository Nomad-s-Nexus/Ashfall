from pathlib import Path

root = Path(__file__).resolve().parents[1]
views = root / "deploy" / "PrismaUI_F4" / "views" / "Interface" / "FalloutAnomaly"

rad_hud = (root / "src" / "RadHud.cpp").read_text(encoding="utf-8")
rad_hud_header = (root / "src" / "RadHud.h").read_text(encoding="utf-8")
rad_hud_lifecycle = (root / "src" / "RadHudLifecycle.cpp").read_text(encoding="utf-8")
main_source = (root / "src" / "main.cpp").read_text(encoding="utf-8")
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

public_api = rad_hud_header.split('public:', 1)[1].split('private:', 1)[0]
assert 'bool StartPrismaViewRetry();' in public_api
views_ready = public_api.split('[[nodiscard]] bool ViewsReady() const', 1)[1]
for token in (
    '_viewsReady.load()',
    '_domReady.load()',
    '_toxicDomReady.load()',
    '_filterDomReady.load()',
):
    assert token in views_ready, f'HUD readiness must include replacement DOM state: {token}'

suppress_callback = rad_hud_lifecycle.split('void SuppressVanillaMetersOnPrismaThread', 1)[1].split(
    'void CreateRadHudViewOnPrismaThread', 1
)[0]
create_callback = rad_hud_lifecycle.split('void CreateRadHudViewOnPrismaThread', 1)[1].split(
    'bool RadHud::StartPrismaViewRetry()', 1
)[0]
for widget in ('HUDPlayerHealthMeter', 'HUDActionPointMeter'):
    restore = f'api11->SuppressHUDWidget("{widget}", false);'
    suppress = f'api11->SuppressHUDWidget("{widget}", true);'
    assert restore in create_callback
    assert suppress in suppress_callback
    assert create_callback.find(restore) < create_callback.find('hud.CreateView();')
assert 'if (!api11 || !RadHud::GetSingleton().ViewsReady()) return;' in suppress_callback
retry_worker = rad_hud_lifecycle.split('bool RadHud::StartPrismaViewRetry()', 1)[1]
assert 'if (!_threadApi) return false;' in retry_worker
assert 'if (_viewCreateRetry.exchange(true)) return true;' in retry_worker
assert retry_worker.rstrip().endswith('return true;\n}')
ready = retry_worker.find('RadHud::GetSingleton().ViewsReady()')
suppress_dispatch = retry_worker.find('DispatchToGameThread(SuppressVanillaMetersOnPrismaThread, api11)')
assert ready != -1 and suppress_dispatch != -1 and ready < suppress_dispatch

push_toxic = rad_hud.split('void RadHud::PushToxic', 1)[1].split('void RadHud::PushMask', 1)[0]
invalid_recovery = push_toxic.split('if (_view == 0 || !api->IsValid(_view)', 1)[1].split('SyncLocalHudLayout();', 1)[0]
assert 'StartPrismaViewRetry();' in invalid_recovery
assert 'if (!_threadApi)' in invalid_recovery
assert 'CreateView();' in invalid_recovery
assert invalid_recovery.find('StartPrismaViewRetry();') < invalid_recovery.find('CreateView();')
assert 'return;' in invalid_recovery

hud_wait = main_source.split('if (held >= 5000)', 1)[1].split('if (!createQueued)', 1)[0]
assert 'auto& hud = RadHud::GetSingleton();' in hud_wait
assert 'if (!hud.StartPrismaViewRetry())' in hud_wait
assert 'hud.CreateView();' in hud_wait
assert hud_wait.find('hud.StartPrismaViewRetry()') < hud_wait.find('hud.CreateView();')
assert 'RadHud::GetSingleton().CreateView();' not in hud_wait

session_reset = rad_hud.split('void RadHud::ResetDebugMenuForSession()', 1)[1].split(
    'void RadHud::ToggleDebugMenu()', 1
)[0]
for token in ('g_inWorld.store(false);', '_debugOpen = false;', 'SetLocalHudEditorPreview(false);', 'SetDebugMenu(false);'):
    assert token in session_reset
assert session_reset.find('g_inWorld.store(false);') < session_reset.find('SetDebugMenu(false);')
assert session_reset.find('SetLocalHudEditorPreview(false);') < session_reset.find('SetDebugMenu(false);')

assert 'a.rfind("hudlayout:", 0) == 0' in rad_debug
assert 'ApplyLocalHudLayout(a.substr(std::strlen("hudlayout:")))' in rad_debug
assert 'cmds.swap(_pendingDebugCmds);' in rad_debug
assert 'DispatchDebugCommand(c);' in rad_debug
drain_debug = rad_debug.split('void RadiationEngine::DrainDebugCommands()', 1)[1].split(
    'void RadiationEngine::DispatchDebugCommand', 1
)[0]
session_gate = drain_debug.find('if (_prevRads < 0.0f)')
mask_reset = drain_debug.find('_maskOverride = -1;')
shelter_reset = drain_debug.find('_debugShelter = false;')
pending_clear = drain_debug.find('_pendingDebugCmds.clear();')
swap = drain_debug.find('cmds.swap(_pendingDebugCmds);')
assert session_gate != -1 and mask_reset != -1 and shelter_reset != -1 and pending_clear != -1 and swap != -1
assert session_gate < mask_reset < pending_clear < swap
assert session_gate < shelter_reset < pending_clear < swap

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
assert 'api->InteropCall(_climateView, "setLocalHUDLayout", payload.c_str())' in rad_layout
assert 'api->InteropCall(_climateView, "releaseLocalHUDLayout", "")' in rad_layout
assert 'if (id == "rad_climate") return &g_layout.climate;' in rad_layout
assert 'if (action == "unit")' in rad_layout
assert 'hud_layout.json' in rad_layout

poll_input = radiation_engine.split("void RadiationEngine::PollInput()", 1)[1].split(
    "void RadiationEngine::EnsureEslLoaded()", 1
)[0]
assert poll_input.count("RadHud::GetSingleton().ToggleDebugMenu();") == 1
assert "RadHud::GetSingleton().ToggleDebugMenu();" not in hooks_input
assert "const_cast<RE::ButtonEvent*>(e)->handled = RE::InputEvent::HANDLED_RESULT::kStop;" in hooks_input

assert 'Fallback vitals: Health + Rads / Stamina (AP) / Tissue' in editor_js
assert 'Without PrismaHUD, Rad supplies Health with radiation loss, Stamina/AP, Tissue' in editor_js
assert 'window.radHudLayout(JSON.stringify(command))' in editor_js
assert 'window.radDebug("hudlayout:"' not in editor_js
assert 'localStorage' not in editor_js

for token in ('aria-label="HEALTH"', 'id="stamina"', 'id="rads"', 'hud_status.css'):
    assert token in toxic
assert 'window.setLocalHUDLayout=function(json)' in toxic
assert 'window.releaseLocalHUDLayout=function()' in toxic
assert 'fallback=!!d.fallback' in toxic

for html in (toxic, filter_html):
    assert 'window.setHUDPosition=function(json)' in html
    assert 'window.setEditorPreview=function(json)' in html
    assert 'window.setLocalHUDLayout=function(json)' in html
    assert 'window.releaseLocalHUDLayout=function()' in html
    assert 'rad_hud_local_widget.js' not in html

assert not (views / 'rad_hud_local_widget.js').exists()
assert '<script src="rad_hud_local_editor.js"></script>' in main_html
assert "cmd('hud:open')" in main_html

print("Rad native fallback HUD ownership, session reset, in-world gate, recovery routing, load-safe debug state, and single debug-hotkey guard passed")
