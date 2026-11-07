"""Contract for the Phosphor core footprint and status-widget default layouts; see docs/code-notes/Ashfall.md."""
from pathlib import Path
import re

ROOT = Path(__file__).resolve().parents[1]
VIEW = ROOT / "deploy" / "PrismaUI_F4" / "views" / "Interface" / "FalloutAnomaly"
SRC = ROOT / "src"

CELL_W, CELL_H, ROW_PITCH = 84.0, 98.0, 108.0
REF_W, REF_H = 1920.0, 1080.0
TOLERANCE = 1e-6


def check(condition, message):
    if not condition:
        raise SystemExit(f"HUD core layout contract failed: {message}")


def text(path):
    return path.read_text(encoding="utf-8")


def num(value):
    return float(value)


core_js = text(VIEW / "hud_core.js")
check("function build(el, icon, label, solid)" in core_js, "PhosCore.build must take no size flag")
check("n = 20" in core_js and "gap = 3.2" in core_js, "every core must use the same 20-segment ring")
check('"sm"' not in core_js and "small ?" not in core_js, "no small core variant may remain in hud_core.js")

css = text(VIEW / "hud_status.css")
check(re.search(r"#root\{[^}]*gap:8px", css), "#root must use the 8px cell gap")
core_rule = re.search(r"\.core\{([^}]*)\}", css)
check(core_rule and "width:84px" in core_rule.group(1) and "height:98px" in core_rule.group(1),
      ".core must be a fixed 84x98 cell")
small_rule = re.search(r"\.core small\{([^}]*)\}", css)
check(small_rule and "height:30px" in small_rule.group(1) and "width:84px" in small_rule.group(1),
      ".core small must be a fixed 84x30 label/value box under the 64px ring")
check(".core.sm" not in css, "the .core.sm size variant must be gone")
check(re.search(r"\.core svg\{[^}]*width:calc\(64px\*var\(--rs,1\)\)", css), "the ring must be 64px")
check(CELL_H == 64 + 4 + 30, "cell height must equal ring + gap + label/value box")

html_defs = {}
for widget, file in [("rad_toxic", "hud_toxic.html"), ("rad_needs", "hud_needs.html"),
                     ("rad_climate", "hud_climate.html"), ("rad_filter", "hud_filter.html")]:
    match = re.search(r"var DEF=\{xf:([\d.]+)/1920,yf:([\d.]+)/1080,wf:([\d.]+)/1920,hf:([\d.]+)/1080\}",
                      text(VIEW / file))
    check(match, f"{file} must declare DEF with fractions")
    xf, yf, wf, hf = (num(g) for g in match.groups())
    html_defs[widget] = (xf / REF_W, yf / REF_H, wf / REF_W, hf / REF_H)

editor = text(VIEW / "rad_hud_local_editor.js")
editor_defaults = {}
for widget in ["rad_toxic", "rad_filter", "rad_needs", "rad_climate"]:
    match = re.search(rf"{widget}: \{{ name: \"[^\"]*\", x: ([\d.]+) / 1920, y: ([\d.]+) / 1080,", editor)
    check(match, f"rad_hud_local_editor.js must default {widget}")
    editor_defaults[widget] = (num(match.group(1)) / REF_W, num(match.group(2)) / REF_H)

layout_cpp = text(SRC / "RadHudLayout.cpp")
layout_defaults = {}
for field, widget in [("toxic", "rad_toxic"), ("filter", "rad_filter"), ("needs", "rad_needs"),
                      ("climate", "rad_climate")]:
    match = re.search(rf"WidgetLayout {field}\{{ ([\d.]+)f / 1920\.0f, ([\d.]+)f / 1080\.0f,", layout_cpp)
    check(match, f"RadHudLayout.cpp LayoutState must default {field}")
    layout_defaults[widget] = (num(match.group(1)) / REF_W, num(match.group(2)) / REF_H)

rad_hud_cpp = text(SRC / "RadHud.cpp")
descriptor_text = {
    "rad_toxic": re.search(r"kToxicWidgetId, \"Toxicity\", ([\d.]+)f / 1920\.0f, ([\d.]+)f / 1080\.0f, "
                           r"([\d.]+)f / 1920\.0f, ([\d.]+)f / 1080\.0f", rad_hud_cpp),
    "rad_filter": re.search(r"kFilterWidgetId, \"Gas Mask Filter\", ([\d.]+)f / 1920\.0f, ([\d.]+)f / 1080\.0f, "
                            r"([\d.]+)f / 1920\.0f, ([\d.]+)f / 1080\.0f", rad_hud_cpp),
    "rad_climate": re.search(r"\"Climate\",\s*([\d.]+)f / 1920\.0f,\s*([\d.]+)f / 1080\.0f,\s*"
                             r"([\d.]+)f / 1920\.0f,\s*([\d.]+)f / 1080\.0f", text(SRC / "RadHudClimate.cpp")),
    "rad_needs": re.search(r"\"Needs\", ([\d.]+)f / 1920\.0f,\s*([\d.]+)f / 1080\.0f, "
                           r"([\d.]+)f / 1920\.0f, ([\d.]+)f / 1080\.0f", text(SRC / "RadHudNeeds.cpp")),
}
descriptors = {}
for widget, match in descriptor_text.items():
    check(match, f"RegisterWidget descriptor for {widget} must have fractional defaults")
    x, y, w, h = (num(g) for g in match.groups())
    descriptors[widget] = (x / REF_W, y / REF_H, w / REF_W, h / REF_H)

for widget in html_defs:
    html = html_defs[widget]
    check(abs(html[0] - editor_defaults[widget][0]) < TOLERANCE and abs(html[1] - editor_defaults[widget][1]) < TOLERANCE,
          f"{widget}: widget DEF and rad_hud_local_editor.js defaults differ")
    if widget in layout_defaults:
        check(abs(html[0] - layout_defaults[widget][0]) < TOLERANCE and abs(html[1] - layout_defaults[widget][1]) < TOLERANCE,
              f"{widget}: widget DEF and RadHudLayout.cpp LayoutState differ")
    if widget in descriptors:
        check(all(abs(a - b) < TOLERANCE for a, b in zip(html, descriptors[widget])),
              f"{widget}: widget DEF and RegisterWidget descriptor differ")

rows = {w: html_defs[w] for w in html_defs}
lefts = {round(rows[w][0] * REF_W, 6) for w in rows}
check(len(lefts) == 1, f"the four default rows must share one left edge, got {sorted(lefts)}")
ys = sorted(rows[w][1] * REF_H for w in rows)
pitches = [round(b - a, 6) for a, b in zip(ys, ys[1:])]
check(all(abs(p - ROW_PITCH) < 1e-6 for p in pitches), f"row pitch must be {ROW_PITCH:g}px, got {pitches}")
check(ROW_PITCH >= CELL_H + 10, "row pitch must leave a visible gap beyond the 98px cell")
check(ys[-1] + CELL_H <= REF_H, "the bottom default row must stay on screen")
for widget in rows:
    check(abs(rows[widget][3] * REF_H - CELL_H) < 1e-6, f"{widget} height must be one cell ({CELL_H:g}px)")

companion_text = text(SRC / "RadHudCompanion.cpp")
companion = {m.group(1): (num(m.group(2)), num(m.group(3)))
             for m in re.finditer(r"\{ \"(rad_\w+)\", ([\d.]+)f, ([\d.]+)f \}", companion_text)}
check(set(companion) == {"rad_toxic", "rad_needs", "rad_climate", "rad_filter"},
      f"companion table must place all four widgets, got {sorted(companion)}")
companion_lefts = {x for x, _ in companion.values()}
check(len(companion_lefts) == 1, "companion rows must share one left edge")
companion_ys = sorted(y for _, y in companion.values())
check(all(abs(b - a - ROW_PITCH) < 1e-6 for a, b in zip(companion_ys, companion_ys[1:])),
      f"companion row pitch must be {ROW_PITCH:g}px, got {companion_ys}")
for widget, (x, y) in companion.items():
    footprint_w = 268.0 if widget == "rad_needs" else 360.0
    right, bottom = x + footprint_w, y + CELL_H
    check(x >= 272.0 + 12.0, f"{widget} must stay right of Phosphor's HP/AP/RAD rings (x 56..272)")
    check(not (x < 390.0 and bottom > 820.0 and y < 915.0),
          f"{widget} overlaps Phosphor's XP/location text (x<390, y 820..915)")
    check(not (right > 660.0 and bottom > 970.0), f"{widget} overlaps the compass row (x>=660, y>970)")
    check(bottom <= REF_H, f"{widget} must stay on screen")

print("HUD core layout verified: one 84x98 core cell, 8px cell gap, shared left edge and 108px row pitch, "
      "defaults agree across DEF/editor/LayoutState/descriptors, companion zone clear of rings, XP text and compass row")
