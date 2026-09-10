from pathlib import Path
from html.parser import HTMLParser


ROOT = Path(__file__).resolve().parents[1]
HUD = ROOT / "src" / "RadHud.cpp"
RADHUD = ROOT / "deploy" / "PrismaUI_F4" / "views" / "Interface" / "FalloutAnomaly" / "radhud.js"
RADHUD_HTML = ROOT / "deploy" / "PrismaUI_F4" / "views" / "Interface" / "FalloutAnomaly" / "radhud.html"
RADHUD_CSS = ROOT / "deploy" / "PrismaUI_F4" / "views" / "Interface" / "FalloutAnomaly" / "radhud.css"
EDITOR = ROOT / "deploy" / "PrismaUI_F4" / "views" / "Interface" / "FalloutAnomaly" / "rad_hud_local_editor.js"


class ArmorStructure(HTMLParser):
    def __init__(self):
        super().__init__()
        self.nodes = {}

    def handle_starttag(self, tag, attrs):
        attrs = dict(attrs)
        if "id" in attrs:
            assert attrs["id"] not in self.nodes, f"duplicate ID: {attrs['id']}"
            self.nodes[attrs["id"]] = (tag, attrs)


def main() -> None:
    native = HUD.read_text(encoding="utf-8")
    view = RADHUD.read_text(encoding="utf-8")
    html = RADHUD_HTML.read_text(encoding="utf-8")
    css = RADHUD_CSS.read_text(encoding="utf-8")
    editor = EDITOR.read_text(encoding="utf-8")
    assert 'RegisterJSListener(v, "radHudLayout", OnRadHudLayoutCommand);' in native
    assert 'InteropCall(view, "setLocalHUDLayout", payload);' in native
    assert 'InteropCall(view, "setEditorPreview", preview ? "1" : "0");' in native
    assert 'InteropCall(_view, "OpenLocalHudEditor", LocalHudLayoutJson().c_str());' in native
    assert 'widget == "rad_climate" ? &_climateLayout' in native
    assert 'window.radHudLayout(JSON.stringify(command));' in editor
    assert 'rad_climate' in editor
    assert 'data-unit-toggle' in editor
    assert 'rad-local-unit-row' in editor
    assert 'id="radLocalUnit"' not in editor
    assert 'setUnit(temperatureUnit === "F" ? "C" : "F", true)' in editor
    assert 'setArmorInfo' in view
    assert 'id="tabbtn-armor"' in html
    assert 'id="tab-armor"' in html
    assert 'armor_terminal_schematic.png' in css
    assert (RADHUD_CSS.parent / "armor_terminal_schematic.png").is_file()
    structure = ArmorStructure()
    structure.feed(html)
    for label in ("armorInsulationLabel", "armorEffectiveLabel", "armorPowerLabel"):
        assert structure.nodes[label][0] == "h3"
        assert f'aria-labelledby="{label}"' in html
    for region in ("head", "torso", "leftArm", "rightArm", "leftLeg", "rightLeg"):
        tag, attrs = structure.nodes[f"armorRegion-{region}"]
        assert tag == "section" and attrs["class"] == "armor-region"
        assert f"#armorRegion-{region}" in css
    assert html.count('class="armor-region"') == 6
    assert 'class="armor-figure" role="img"' in html
    assert 'grid-column: 2; grid-row: 1 / 4;' in css
    for table in ("armorList", "armorNonThermalList"):
        assert structure.nodes[table][0] == "tbody"
    assert 'NON-THERMAL EQUIPMENT' in html and '.armor-nonthermal .armor-records td' in css
    assert 'THERMAL EQUIPMENT' in html and 'READ ONLY' in html
    armor_view = view.split("window.setArmorInfo = function(json)", 1)[1].split("window.setHudLayoutStatus", 1)[0]
    for forbidden in ("innerHTML", "setInterval", "setTimeout", "requestAnimationFrame", "cmd(", "radDebug", "PrismaHUD"):
        assert forbidden not in armor_view
    armor_native = native.split("void RadHud::PushArmor(", 1)[1].split("std::string RadHud::LocalHudLayoutJson", 1)[0]
    assert 'if (serialized == _armorPayload)\n        return;' in armor_native
    assert '!_debugOpen' in armor_native and '_hudApi' not in armor_native
    for field in ("insulation", "effectiveInsulation", "powerArmor", "pieces", "formID", "name", "warmth", "override",
                  "head", "torso", "leftArm", "rightArm", "leftLeg", "rightLeg"):
        assert f'"{field}"' in armor_native
    assert 'PushArmor' in (ROOT / "src" / "RadHud.h").read_text(encoding="utf-8")
    assert 'InteropCall(_view, "setArmorInfo"' in (ROOT / "src" / "RadHud.cpp").read_text(encoding="utf-8")
    assert 'temperatureUnit' in native
    assert 'setClimateUnit' in (ROOT / "deploy" / "PrismaUI_F4" / "views" / "Interface" / "FalloutAnomaly" / "hud_climate.html").read_text(encoding="utf-8")
    assert 'window.setHudLayoutStatus' in view
    print("HUD layout bridge contract verified")


if __name__ == "__main__":
    main()
