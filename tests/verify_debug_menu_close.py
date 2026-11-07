"""Contract test: the Shift+F8 debug menu can always be closed.

Paths covered:
  * CLOSE button -> closeDebugMenu() -> local hide + radDebug('close')
  * Escape keydown while the panel is open -> closeDebugMenu()
  * native 'close' bypasses the engine queue (OnRadDebugCommand -> RequestDebugClose)
  * PumpDeferredDebug applies the close every frame from PollInput
  * Shift+F8 is accepted while the menu is open even when the game window is not foreground
"""
from pathlib import Path
import re
import sys

ROOT = Path(__file__).resolve().parents[1]
VIEW = ROOT / "deploy" / "PrismaUI_F4" / "views" / "Interface" / "FalloutAnomaly"


def text(path: Path) -> str:
    return path.read_text(encoding="utf-8")


def fail(msg: str) -> None:
    print(f"verify_debug_menu_close: FAIL - {msg}")
    sys.exit(1)


def require(cond: bool, msg: str) -> None:
    if not cond:
        fail(msg)


def main() -> int:
    html = text(VIEW / "radhud.html")
    js = text(VIEW / "radhud.js")
    hud_cpp = text(ROOT / "src" / "RadHud.cpp")
    hud_h = text(ROOT / "src" / "RadHud.h")
    rad_cpp = text(ROOT / "src" / "RadiationEngine.cpp")

    require('onclick="closeDebugMenu()"' in html, "CLOSE button must call closeDebugMenu()")
    require("function closeDebugMenu()" in js, "radhud.js must define closeDebugMenu()")
    m = re.search(r"function closeDebugMenu\(\)\s*\{(.*?)\n\}", js, re.S)
    require(m is not None, "closeDebugMenu body not found")
    body = m.group(1)
    require('window.SetDebugMenu(0)' in body, "closeDebugMenu must hide the panel locally first")
    require('cmd("close")' in body, "closeDebugMenu must send radDebug('close') to native")

    require(re.search(r'e\.key\s*===\s*"Escape"', js) is not None, "radhud.js must handle the Escape key")
    require(re.search(r"debugPanelOpen\(\)\s*\)\s*closeDebugMenu\(\)", js) is not None,
            "Escape must only close when the debug panel is open")

    require('std::string_view(arg) == "close"' in hud_cpp, "OnRadDebugCommand must intercept 'close'")
    require("RequestDebugClose();" in hud_cpp, "'close' must call RequestDebugClose()")
    require("void RequestDebugClose() noexcept;" in hud_h, "RadHud must declare RequestDebugClose()")
    require("std::atomic_bool _debugCloseRequested" in hud_h, "RadHud must hold an atomic close request")
    pump = re.search(r"void RadHud::PumpDeferredDebug\(\)\s*\{(.*?)\n    if \(!_debugTogglePending\)", hud_cpp, re.S)
    require(pump is not None, "PumpDeferredDebug close branch not found")
    require("_debugCloseRequested.exchange(false)" in pump.group(1), "PumpDeferredDebug must consume the close request")
    require("SetDebugMenu(false);" in pump.group(1), "PumpDeferredDebug must close the menu")

    require("RadHud::GetSingleton().DebugMenuOpen()" in rad_cpp,
            "PollInput must allow the toggle when the debug menu is open")
    require(re.search(r"dbgKeyDown && !_f8WasDown && lshift && debugToggleAllowed", rad_cpp) is not None,
            "Shift+F8 edge must use debugToggleAllowed")
    require("bool DebugMenuOpen() const noexcept" in hud_h, "RadHud must expose DebugMenuOpen()")

    require("SetDebugViewOwnsEscape(_view, true);" in hud_cpp, "open must claim Escape for the debug view")
    require("SetDebugViewOwnsEscape(_view, false);" in hud_cpp, "close must release Escape ownership")
    require("IVPrismaUI9" in hud_cpp and "SetViewOwnsEscape(view, owns)" in hud_cpp,
            "Escape ownership must use the PrismaUI V9 SetViewOwnsEscape call")
    require("if (!open && _view != 0 && api->IsValid(_view))" in hud_cpp,
            "SetDebugMenu(false) must release focus when the DOM is not ready")

    req_m = re.search(r"void RadHud::RequestDebugClose\(\) noexcept\s*\{(.*?)\n\}", hud_cpp, re.S)
    require(req_m is not None, "RequestDebugClose body not found")
    require("Hooks::QueueMainThreadTask" in req_m.group(1), "RequestDebugClose must queue a main-thread close task")
    watch = re.search(r"void RadHud::ReleaseStaleDebugFocus\(\) noexcept\s*\{(.*?)\n\}", hud_cpp, re.S)
    require(watch is not None, "ReleaseStaleDebugFocus must be defined")
    require("!api->HasFocus(_view)" in watch.group(1) and "api->Unfocus(_view);" in watch.group(1)
            and "SetDebugViewOwnsEscape(_view, false);" in watch.group(1),
            "watchdog must Unfocus and release Escape only when the closed view still has focus")
    require("RadHud::GetSingleton().ReleaseStaleDebugFocus();" in rad_cpp,
            "RadiationEngine::Update must run the stale-focus watchdog")
    require("void ReleaseStaleDebugFocus() noexcept;" in hud_h, "RadHud must declare ReleaseStaleDebugFocus()")

    # Constraint: focus rejection is bounded, falls back to FocusOverlay, and gives up with one player message.
    require("constexpr ULONGLONG kDebugFocusGiveUpMs = 2000;" in hud_cpp, "focus give-up must be capped at 2 s")
    require("constexpr ULONGLONG kDebugFocusRetryMs = 150;" in hud_cpp, "focus retry interval must stay 150 ms")
    require("_debugFocusFailedSinceMs = now;" in hud_cpp, "first focus rejection must stamp the retry window")
    require("now - _debugFocusFailedSinceMs >= kDebugFocusGiveUpMs" in hud_cpp,
            "focus rejection must compare elapsed time against the give-up cap")
    give_up = hud_cpp.split("now - _debugFocusFailedSinceMs >= kDebugFocusGiveUpMs", 1)[1].split("_debugDesiredOpen = true;", 1)[0]
    for token in ("_debugDesiredOpen = false;", "_debugTogglePending = false;", "_debugNextFocusRetryMs = 0;",
                  "_debugFocusFailedSinceMs = 0;", "RE::SendHUDMessage::ShowHUDMessage(", "return;"):
        require(token in give_up, f"focus give-up branch must contain {token}")
    require("RequestPluginAPI<PRISMA_UI_API::IVPrismaUI10>()" in hud_cpp, "FocusOverlay must come from the V10 interface")
    require("overlay->FocusOverlay(view, false, false)" in hud_cpp, "FocusOverlay fallback must be called")
    require("if (!focusAccepted) focusAccepted = FocusDebugOverlayFallback(_view);" in hud_cpp,
            "FocusOverlay must be tried only after a rejected Focus")
    require("overlay != nullptr &&" in hud_cpp, "FocusOverlay fallback must null-check the interface")

    # Constraint: the view is kPanel only while the menu is open, and never declared kPanel at view creation.
    require(hud_cpp.count("SetDebugViewRole(_view, PRISMA_UI_API::ViewRole::kPanel);") == 1,
            "kPanel must be declared exactly once, on successful open")
    require(hud_cpp.count("SetDebugViewRole(_view, PRISMA_UI_API::ViewRole::kWidget);") == 2,
            "kWidget must be declared on close and on focus failure")
    require("roles->SetViewRole(view, role);" in hud_cpp, "SetViewRole must go through the V10 interface")
    require("if (auto* roles = PRISMA_UI_API::RequestPluginAPI<PRISMA_UI_API::IVPrismaUI10>())" in hud_cpp,
            "SetViewRole must null-check the V10 interface")
    create_body = hud_cpp.split("void RadHud::CreateView()", 1)[1].split("\nvoid RadHud::", 1)[0]
    require("kPanel" not in create_body, "view creation must not declare kPanel")

    # Constraint: the panel is hidden in the first paint (no 'on' class in markup, display:none in CSS).
    css = text(VIEW / "radhud.css")
    require('<div id="debugPanel">' in html, "#debugPanel must start without the 'on' class")
    require(re.search(r"#debugPanel\s*\{[^}]*display:\s*none;", css, re.S) is not None,
            "#debugPanel must default to display:none")
    require(re.search(r"#debugPanel\.on\s*\{\s*display:\s*flex;", css) is not None,
            "#debugPanel.on must be the only display:flex state")

    # Constraint: JS never self-opens the panel; the only SetDebugMenu(1) call sits behind the #preview=1 gate.
    require(js.count("SetDebugMenu(1)") == 1, "radhud.js must call SetDebugMenu(1) exactly once (preview gate)")
    require(js.count('panel.classList.add("on")') == 1, "only SetDebugMenu may add the 'on' class to #debugPanel")
    dom_idx = js.index('window.addEventListener("DOMContentLoaded", function() {')
    dom_body = js[dom_idx:js.index("window.showRadDeck", dom_idx)]
    require("window.SetDebugMenu(0)" in dom_body, "DOMContentLoaded must hide the panel on load")
    require(dom_body.index('preview=1') < dom_body.index("SetDebugMenu(1)"),
            "SetDebugMenu(1) on load must sit behind the explicit preview=1 hash gate")
    require(dom_body.index('preview=1') < dom_body.index("showTab(initialTab)"),
            "tab selection on load must sit behind the preview=1 hash gate")
    require(re.search(r"\bstartTour\(\)", js) is not None and js.count("startTour()") == 1,
            "the showcase tour must start only from its button")
    require(html.count("RadShowcase.startTour()") == 1, "the showcase tour must start only from its button")

    # Constraint: native pushes the closed state on DOM-ready and a fresh view starts with native open state false.
    require('a->InteropCall(v, "SetDebugMenu", "0");' in hud_cpp, "DOM-ready must push the closed debug state")
    require(create_body.count("_debugOpen = false;") >= 1, "CreateView must reset native open state for a new view")
    require(hud_cpp.index('a->InteropCall(v, "SetDebugMenu", "0");') < hud_cpp.index("flag->store(true"),
            "DOM-ready closed-state push must happen before the DOM-ready flag is published")

    rad_h = text(ROOT / "src" / "RadiationEngine.h")
    mask_cpp = text(ROOT / "src" / "RadEngine_Mask.cpp")
    debug_cpp = text(ROOT / "src" / "RadEngine_Debug.cpp")
    hooks_cpp = text(ROOT / "src" / "Hooks.cpp")
    require("std::atomic<int> _filterReplaceKey" in rad_h, "ReplaceKey must be an atomic read by the input producer")
    require("std::atomic<int> _conditionRepairKey" in rad_h, "RepairKey must be an atomic read by the input producer")
    require("std::atomic<std::uint32_t> _polledInputBits" in rad_h, "PollInput bits must be an atomic")
    require("std::uint32_t ReadInputBits() const noexcept;" in rad_h, "RadiationEngine must expose ReadInputBits()")
    require("_polledInputBits.store(ReadInputBits());" in rad_cpp, "PollInput must store the key bits")
    poll_body = rad_cpp.split("void RadiationEngine::PollInput()", 1)[1]
    require(poll_body.find("_polledInputBits.store(") < poll_body.find("const bool dbgKeyDown"),
            "PollInput must store the key bits before any early return")
    require("GetAsyncKeyState(" not in poll_body.split("_polledInputBits.store(", 1)[1],
            "PollInput must derive every key edge from the single stored bit read")
    require("_filterReplaceKey.load(), _conditionRepairKey.load()" in rad_cpp,
            "ReadInputBits must load the atomic mask keys")
    require("engine.ReadInputBits() != engine.PolledInputBits() || RadHud::GetSingleton().DebugWorkPending()" in hooks_cpp,
            "producer must queue the poll only on a key-bit change or pending debug work")
    producer = hooks_cpp.split("void PlayerCharacter_Update_Hook(", 1)[1]
    require(producer.find("engine.ReadInputBits()") < producer.find("QueueInputPoll();"),
            "producer must test the key-bit edge before queueing the poll")
    require("std::remove_if(_pendingDebugCmds.begin(), _pendingDebugCmds.end()" in debug_cpp or
            "std::remove_if(_pendingDebugCmds.begin(), _pendingDebugCmds.end(), [&](const std::string& queued)" in debug_cpp,
            "hudlayout commands must be coalesced per widget in RadEngine_Debug.cpp")
    require("queued.rfind(kHudLayoutPrefix, 0) == 0 && HudLayoutWidgetKey(queued) == widget" in debug_cpp,
            "coalescing must match hudlayout entries by widget key")
    require("if (replacedSave) command = WithHudLayoutSave(command);" in debug_cpp,
            "a coalesced hudlayout replacement must keep a save that it replaces")
    require('body["save"] = true;' in debug_cpp,
            "WithHudLayoutSave must set save:true on the replacement command")
    require("constexpr std::size_t kMaxPendingDebugCmds = 32;" in debug_cpp,
            "pending debug command queue must stay bounded")
    require("GetFileAttributesExA(kIni, GetFileExInfoStandard, &attrs)" in mask_cpp,
            "ReloadHotkeysFromIni must stat the INI write time")
    require("if (stamp == _hotkeyIniWriteTime)" in mask_cpp and "_hotkeyIniWriteTime = stamp;" in mask_cpp,
            "ReloadHotkeysFromIni must skip when the write time is unchanged")
    require("_filterReplaceKey.load()" in mask_cpp and "_conditionRepairKey.load()" in mask_cpp,
            "hotkey log line must load the atomic keys")

    # Constraint: every native debug-state change reaches the page through PushDebugPanelState, and the 4 Hz tick re-syncs it.
    set_m = re.search(r"void RadHud::SetDebugMenu\(bool open\)\s*\{(.*?)\n\}", hud_cpp, re.S)
    require(set_m is not None, "SetDebugMenu body not found")
    require('InteropCall(_view, "SetDebugMenu"' not in set_m.group(1),
            "SetDebugMenu must push the page state only through PushDebugPanelState")
    require(set_m.group(1).count("PushDebugPanelState(") == 3,
            "SetDebugMenu must push on open, close and focus give-up")
    require("if (open) return;" in set_m.group(1),
            "SetDebugMenu must drop an open request when the DOM is not ready")
    push_m = re.search(r"void RadHud::PushDebugPanelState\(bool open\) noexcept\s*\{(.*?)\n\}", hud_cpp, re.S)
    require(push_m is not None and 'InteropCall(_view, "SetDebugMenu", open ? "1" : "0");' in push_m.group(1)
            and "_debugPushedOpen = open ? 1 : 0;" in push_m.group(1),
            "PushDebugPanelState must send the state and record what the page received")
    sync_m = re.search(r"void RadHud::SyncDebugPanelState\(\) noexcept\s*\{(.*?)\n\}", hud_cpp, re.S)
    require(sync_m is not None and "_debugPushedOpen == (_debugOpen ? 1 : 0)" in sync_m.group(1)
            and "PushDebugPanelState(_debugOpen);" in sync_m.group(1),
            "SyncDebugPanelState must re-send the native state only when it differs from the delivered state")
    require("RadHud::GetSingleton().SyncDebugPanelState();" in rad_cpp,
            "RadiationEngine::Update must run the panel sync")
    require(rad_cpp.index("RadHud::GetSingleton().SyncDebugPanelState();")
            < rad_cpp.index("RadHud::GetSingleton().ReleaseStaleDebugFocus();"),
            "the panel sync must run before the stale-focus check")
    require("void SyncDebugPanelState() noexcept;" in hud_h and "void PushDebugPanelState(bool open) noexcept;" in hud_h,
            "RadHud must declare the sync and push helpers")
    require(create_body.count("_debugPushedOpen = -1;") == 1,
            "CreateView must reset the delivered debug state for a new view")

    # Constraint: load and new game clear pending close requests and re-baseline the Shift+F8 edge from the held key state.
    reset_m = re.search(r"void RadHud::ResetDebugMenuForSession\(\)\s*\{(.*?)\n\}", hud_cpp, re.S)
    require(reset_m is not None and "_debugCloseRequested.store(false);" in reset_m.group(1)
            and "RadiationEngine::GetSingleton().ResetDebugKeyBaseline();" in reset_m.group(1),
            "ResetDebugMenuForSession must clear the pending close and re-baseline Shift+F8")
    require("void ResetDebugKeyBaseline() noexcept;" in rad_h, "RadiationEngine must declare ResetDebugKeyBaseline()")
    require("_f8WasDown = (ReadInputBits() & 1u) != 0;" in rad_cpp,
            "ResetDebugKeyBaseline must capture the held Shift+F8 state")

    print("verify_debug_menu_close: OK")
    return 0


if __name__ == "__main__":
    sys.exit(main())
