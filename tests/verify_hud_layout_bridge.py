from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
HUD = ROOT / "src" / "RadHud.cpp"
RADHUD = ROOT / "deploy" / "PrismaUI_F4" / "views" / "Interface" / "FalloutAnomaly" / "radhud.js"
EDITOR = ROOT / "deploy" / "PrismaUI_F4" / "views" / "Interface" / "FalloutAnomaly" / "rad_hud_local_editor.js"


def main() -> None:
    native = HUD.read_text(encoding="utf-8")
    view = RADHUD.read_text(encoding="utf-8")
    editor = EDITOR.read_text(encoding="utf-8")
    assert 'RegisterJSListener(v, "radHudLayout", OnRadHudLayoutCommand);' in native
    assert 'InteropCall(view, "setLocalHUDLayout", payload);' in native
    assert 'InteropCall(view, "setEditorPreview", preview ? "1" : "0");' in native
    assert 'InteropCall(_view, "OpenLocalHudEditor", LocalHudLayoutJson().c_str());' in native
    assert 'window.radHudLayout(JSON.stringify(command));' in editor
    assert 'window.setHudLayoutStatus' in view
    print("HUD layout bridge contract verified")


if __name__ == "__main__":
    main()
