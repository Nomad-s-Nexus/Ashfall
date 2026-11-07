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

    print("verify_debug_menu_close: OK")
    return 0


if __name__ == "__main__":
    sys.exit(main())
