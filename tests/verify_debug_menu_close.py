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

    print("verify_debug_menu_close: OK")
    return 0


if __name__ == "__main__":
    sys.exit(main())
