from pathlib import Path
import re
import sys

ROOT = Path(__file__).resolve().parents[1]
SRC = ROOT / "src"


def text(path: Path) -> str:
    return path.read_text(encoding="utf-8")


def fail(msg: str) -> None:
    print(f"verify_load_settle: FAIL - {msg}")
    sys.exit(1)


def require(cond: bool, msg: str) -> None:
    if not cond:
        fail(msg)


def body(src: str, signature: str) -> str:
    start = src.find(signature)
    require(start >= 0, f"missing definition: {signature}")
    brace = src.find("{", start)
    depth = 0
    for i in range(brace, len(src)):
        if src[i] == "{":
            depth += 1
        elif src[i] == "}":
            depth -= 1
            if depth == 0:
                return src[brace:i + 1]
    fail(f"unterminated body: {signature}")
    return ""


def main() -> int:
    policy = text(SRC / "RadCorrectnessPolicy.h")
    hud_h = text(SRC / "RadHud.h")
    hud_cpp = text(SRC / "RadHud.cpp")
    life = text(SRC / "RadHudLifecycle.cpp")
    climate = text(SRC / "RadHudClimate.cpp")
    needs = text(SRC / "RadHudNeeds.cpp")
    layout = text(SRC / "RadHudLayout.cpp")
    rad = text(SRC / "RadiationEngine.cpp")
    main_cpp = text(SRC / "main.cpp")

    require("kLoadSettleMs = 3000" in policy, "kLoadSettleMs must be 3000 ms")
    require("kEngineTickMs = 250" in policy, "kEngineTickMs must be 250 ms")
    require("kCompanionBridgeMaxTicks = 40" in policy, "companion bridge cap must be 40 ticks (10 s)")
    require("bool LoadSettleElapsed(" in policy, "LoadSettleElapsed predicate missing")
    require("bool CompanionBridgeReady(" in policy, "CompanionBridgeReady predicate missing")
    require("bool IsLiveDomReady(" in policy, "IsLiveDomReady predicate missing")

    require("void BeginLoadSettle() noexcept;" in hud_h, "RadHud must declare BeginLoadSettle()")
    require("void TickLoadSettle();" in hud_h, "RadHud must declare TickLoadSettle()")
    require(body(life, "void RadHud::BeginLoadSettle() noexcept").count("_ticksSinceLoad.store(0)") == 1,
            "BeginLoadSettle must reset the tick counter")
    tick = body(life, "void RadHud::TickLoadSettle()")
    require("_ticksSinceLoad.fetch_add(1)" in tick, "TickLoadSettle must advance the tick counter")
    require("ScanPhosphorViews()" in tick, "TickLoadSettle must run the bounded PhosphorHUD probe")
    require(main_cpp.count("RadHud::GetSingleton().BeginLoadSettle();") == 2,
            "main.cpp must call BeginLoadSettle() on both PostLoad and NewGame")

    rad_update = body(rad, "void RadiationEngine::Update()")
    require("TickLoadSettle();" in rad_update, "RadiationEngine::Update must tick the load settle")
    require("PumpDomReady();" in rad_update, "RadiationEngine::Update must pump deferred DOM-ready work")
    require(rad_update.index("TickLoadSettle();") < rad_update.index("PumpDomReady();"),
            "the settle tick must run before the DOM-ready pump in the same tick")

    bridge = body(life, "bool RadHud::BridgeReady()")
    require("LoadSettled()" in bridge and "CompanionBridgeReady(" in bridge,
            "BridgeReady must require the settle and the companion gate")
    require("EnumerateViews(" in body(life, "bool RadHud::ScanPhosphorViews()"),
            "PhosphorHUD probe must use IVPrismaUI4::EnumerateViews")
    require("PhosphorHUD" in life, "PhosphorHUD view detection must match on the view html path")

    note = body(life, "void RadHud::NoteDomReady(int slot, uint64_t view) noexcept")
    for forbidden in ("api->", "Show(", "Hide(", "RegisterJSListener", "InteropCall", "RegisterWidget", "PushTheme"):
        require(forbidden not in note, f"NoteDomReady must not call Prisma: found '{forbidden}'")
    require("_domLive[slot]" in note, "NoteDomReady must check the slot's live view")
    require("_domPending[slot].store(view" in note, "NoteDomReady must publish the handle to the pending slot")

    pump = body(life, "void RadHud::PumpDomReady()")
    require("PrismaWindowOpen()" in pump, "PumpDomReady must be gated by the settle and bridge")
    for slot in ("kDomSlotMain", "kDomSlotToxic", "kDomSlotFilter", "kDomSlotClimate", "kDomSlotNeeds", "kDomSlotDebug"):
        require(slot in pump, f"PumpDomReady must consume {slot}")
    require(pump.count("IsLiveDomReady(") == 6, "every pending DOM-ready handle must be checked for liveness")
    for fin in ("FinalizeMainDomReady", "FinalizeToxicDomReady", "FinalizeFilterDomReady",
                "FinalizeClimateDomReady", "FinalizeNeedsDomReady", "FinalizeDebugDomReady"):
        require(fin + "(" in pump, f"PumpDomReady must call {fin}")

    debug_fin = body(hud_cpp, "void RadHud::FinalizeDebugDomReady(uint64_t view)")
    require(debug_fin.index("RegisterJSListener") < debug_fin.index("_debugDomReady.store(true"),
            "FinalizeDebugDomReady must register the listener before publishing ready")

    create = body(hud_cpp, "void RadHud::CreateView()")
    require("!LoadSettled()" in create, "CreateView must return early inside the settle window")
    require("!LoadSettled()" in body(climate, "void RadHud::EnsureClimateView()"),
            "EnsureClimateView must not run inside the settle window")
    require("!hud.LoadSettled()" in body(life, "void CreateRadHudViewOnPrismaThread(void* userData)"),
            "the retry worker must not create views inside the settle window")
    require("LoadSettled()" in needs, "PushNeeds must wait for the settle before creating its view")
    require("BridgeReady()" in layout, "SyncLocalHudLayout must wait for the layout bridge")
    require("BridgeReady()" in needs, "the needs widget must join the layout only after the bridge")

    retired = ("g_domReadyFlag", "g_apiForCb", "g_toxicDomReadyFlag", "g_hudApiForCb",
               "g_toxicWidgetRegisteredFlag", "g_filterDomReadyFlag", "g_climateDomReadyFlag",
               "g_climateWidgetRegisteredFlag", "g_climateApiForCb", "g_climateHudApiForCb")
    for path in sorted(SRC.glob("*.cpp")) + sorted(SRC.glob("*.h")):
        content = text(path)
        for name in retired:
            require(re.search(r"\b" + name + r"\b", content) is None,
                    f"retired global '{name}' still referenced in {path.name}")

    print("verify_load_settle: PASS")
    return 0


if __name__ == "__main__":
    sys.exit(main())
