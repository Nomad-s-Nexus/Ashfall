from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
HUD = ROOT / "src" / "RadHud.cpp"
RADHUD = ROOT / "deploy" / "PrismaUI_F4" / "views" / "Interface" / "FalloutAnomaly" / "radhud.js"
RADHUD_HTML = ROOT / "deploy" / "PrismaUI_F4" / "views" / "Interface" / "FalloutAnomaly" / "radhud.html"
RADHUD_CSS = ROOT / "deploy" / "PrismaUI_F4" / "views" / "Interface" / "FalloutAnomaly" / "radhud.css"
EDITOR = ROOT / "deploy" / "PrismaUI_F4" / "views" / "Interface" / "FalloutAnomaly" / "rad_hud_local_editor.js"


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
    assert 'PushArmor' in (ROOT / "src" / "RadHud.h").read_text(encoding="utf-8")
    assert 'InteropCall(_view, "setArmorInfo"' in (ROOT / "src" / "RadHud.cpp").read_text(encoding="utf-8")
    assert 'temperatureUnit' in native
    assert 'setClimateUnit' in (ROOT / "deploy" / "PrismaUI_F4" / "views" / "Interface" / "FalloutAnomaly" / "hud_climate.html").read_text(encoding="utf-8")
    assert 'window.setHudLayoutStatus' in view
    print("HUD layout bridge contract verified")


if __name__ == "__main__":
    main()
