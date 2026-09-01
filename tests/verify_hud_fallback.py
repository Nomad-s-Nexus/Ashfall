import sys
from pathlib import Path


def effective_max(permanent, temporary):
    return max(0.0, float(permanent) + float(temporary))


def vital_percent(current, maximum):
    current = max(0.0, float(current))
    maximum = max(0.0, float(maximum))
    return min(100.0, current / maximum * 100.0) if maximum > 0.0 else (100.0 if current > 0.0 else 0.0)


def toxic_state(fallback, tissue, permanent):
    tissue = min(1000.0, max(0.0, float(tissue)))
    permanent = min(tissue, max(0.0, float(permanent)))
    return {"tissue": tissue, "permanent": permanent, "visible": bool(fallback) or tissue > 0.0}


def widget_fallback(api_available, view_valid, registration_called):
    return not (api_available and view_valid and registration_called)


def check_behavior():
    health_max = effective_max(300, 50)
    ap_max = effective_max(100, 20)
    assert round(vital_percent(330, health_max), 5) == 94.28571
    assert round(vital_percent(110, ap_max), 5) == 91.66667
    assert vital_percent(0, 0) == 0.0
    assert vital_percent(10, -5) == 100.0
    assert vital_percent(360, health_max) == 100.0

    assert widget_fallback(False, False, False) is True
    assert widget_fallback(True, False, False) is True
    assert widget_fallback(True, True, True) is False

    assert toxic_state(False, 0, 0)["visible"] is False
    assert toxic_state(True, 0, 0)["visible"] is True

    tissue = toxic_state(False, 400, 120)
    assert tissue == {"tissue": 400.0, "permanent": 120.0, "visible": True}
    assert toxic_state(False, -1, 50) == {"tissue": 0.0, "permanent": 0.0, "visible": False}


def check_source(html_path, source_path):
    html = Path(html_path).read_text(encoding="utf-8")
    for marker in ('HEALTH', 'STAMINA (AP)', 'TISSUE', "fallback=!!d.fallback", "var show=fallback||t>0"):
        if marker not in html:
            raise SystemExit(f"missing HUD fallback contract: {marker}")

    source = Path(source_path).read_text(encoding="utf-8")
    for marker in (
        'player.GetModifier(RE::ACTOR_VALUE_MODIFIER::kTemporary, value)',
        'healthMax = EffectiveActorValueMax(*player, *values->health);',
        'apMax = EffectiveActorValueMax(*player, *values->actionPoints);',
        'g_hudApiForCb && g_apiForCb &&',
        'api->IsValid(_toxicView)',
        'const bool fallback = !_toxicWidgetRegistered.load();',
    ):
        if marker not in source:
            raise SystemExit(f"missing HUD fallback implementation: {marker}")


if len(sys.argv) != 3:
    raise SystemExit(f"usage: {Path(sys.argv[0]).name} HUD_TOXIC.html RADHUD.cpp")
check_behavior()
check_source(sys.argv[1], sys.argv[2])
print("HUD fallback behavior verified")
