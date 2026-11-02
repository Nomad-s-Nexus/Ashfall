from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SRC = ROOT / "src"
VIEW = ROOT / "deploy" / "PrismaUI_F4" / "views" / "Interface" / "FalloutAnomaly" / "hud_climate.html"

header = (SRC / "RadHud.h").read_text(encoding="utf-8")
hud = (SRC / "RadHudClimate.cpp").read_text(encoding="utf-8")
climate = (SRC / "ClimateRuntime.cpp").read_text(encoding="utf-8")
html = VIEW.read_text(encoding="utf-8")

for token in (
    "void PushClimate(const RadClimatePolicy::ClimateState& state,",
    "void EnsureClimateView();",
    "uint64_t _climateView = 0;",
    "std::atomic<bool> _climateDomReady{ false };",
    "std::atomic<bool> _climateWidgetRegistered{ false };",
):
    assert token in header, f"climate HUD header contract missing: {token}"

for token in (
    "Interface/FalloutAnomaly/hud_climate.html",
    'constexpr const char* kClimateWidgetId = "rad_climate";',
    "api->CreateView(kClimateViewPath",
    "hudApi->RegisterWidget(_climateView, CompanionPlaced(kClimateDesc));",
    "hudApi->ApplyLayout(_climateView);",
    'api->InteropCall(_climateView, "updateClimate", payload.c_str());',
    "if (payload == _climatePayload)",
    "api->Hide(_climateView);",
    "api->Show(_climateView);",
):
    assert token in hud, f"climate HUD runtime contract missing: {token}"

for token in (
    "RadHud::GetSingleton().PushClimate(_state, environment, result);",
):
    assert token in climate, f"climate model-to-HUD bridge missing: {token}"

for token in (
    "window.updateClimate",
    "window.setEditorPreview",
    "window.setHUDPosition",
    "window.reportVisibility",
    "rad_climate",
    "CHILLED",
    "WET",
    "WARM",
    "NEAR HEAT",
):
    assert token in html, f"climate HUD HTML contract missing: {token}"

for token in (
    "std::thread",
    "sleep_for",
    "ModActorValue",
    "DamageActorValue",
    "SetDelete",
    "ForceWeather",
    "RemoveItem",
):
    assert token not in hud, f"climate HUD must remain UI-only and non-polling: {token}"

print("climate HUD verified: lazy Prisma view, positionable widget, change-only pushes, no gameplay mutation")
