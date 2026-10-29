"""Bake the HUD-safe clear zones into the Prisma visor art.

The visor overlay and its condensation fog are fullscreen images, so a mask-image would be needed to keep
the Scaleform HUD readable. Ultralight support for mask-image is unproven, so the clear zones are baked
into the PNG alpha instead: the top-left notification area, the centre (loot/prompt readouts) and the
bottom-centre XP bar stay transparent, while the visor edges and corners keep their original strength.

Usage: python tools/bake_visor_clear_zones.py
Regenerates gasmask_overlay.png (in place, alpha only) and visor_fog.png (new file) next to this script's
view directory. Keep the geometry in sync with docs/code-notes/Rad-Overhaul-Fallout-4.md and the
alpha-point assertions in tests/status_hud_tests.js.
"""

from pathlib import Path

from PIL import Image

VIEW = Path(__file__).resolve().parents[1] / "deploy/PrismaUI_F4/views/Interface/FalloutAnomaly"
OVERLAY = VIEW / "gasmask_overlay.png"
FOG = VIEW / "visor_fog.png"

# Constraint: these screen-normalised zones must stay clear - top-left messages, bottom-centre XP, centre loot/prompt.
CLEAR_RECTS = (
    (0.00, 0.46, 0.00, 0.22),
    (0.28, 0.72, 0.86, 1.00),
)
CLEAR_ELLIPSE = (0.50, 0.52, 0.36, 0.30)


def smoothstep(edge0, edge1, value):
    if edge1 <= edge0:
        return 1.0 if value >= edge1 else 0.0
    t = (value - edge0) / (edge1 - edge0)
    t = max(0.0, min(1.0, t))
    return t * t * (3.0 - 2.0 * t)


def clear_factor(x, y):
    """0 inside a HUD clear zone, 1 outside; smooth over a small band."""
    factors = []
    band = 0.05
    for x0, x1, y0, y1 in CLEAR_RECTS:
        d = max(x0 - x, x - x1, y0 - y, y - y1)
        factors.append(smoothstep(0.0, band, d))
    cx, cy, rx, ry = CLEAR_ELLIPSE
    r = (((x - cx) / rx) ** 2 + ((y - cy) / ry) ** 2) ** 0.5
    factors.append(smoothstep(0.0, 0.18, r - 1.0))
    return min(factors)


def bake_overlay():
    image = Image.open(OVERLAY).convert("RGBA")
    width, height = image.size
    pixels = image.load()
    for py in range(height):
        y = (py + 0.5) / height
        for px in range(width):
            x = (px + 0.5) / width
            r, g, b, a = pixels[px, py]
            if a:
                pixels[px, py] = (r, g, b, int(round(a * clear_factor(x, y))))
    image.save(OVERLAY)
    print(f"baked clear zones into {OVERLAY.name} ({width}x{height})")


def bake_fog():
    width, height = 480, 270
    image = Image.new("RGBA", (width, height), (0, 0, 0, 0))
    pixels = image.load()
    for py in range(height):
        y = (py + 0.5) / height
        for px in range(width):
            x = (px + 0.5) / width
            radius = (((x - 0.5) / 0.5) ** 2 + ((y - 0.5) / 0.5) ** 2) ** 0.5 / (2.0 ** 0.5)
            vignette = smoothstep(0.45, 0.92, radius)
            alpha = int(round(255 * 0.72 * vignette * clear_factor(x, y)))
            if alpha:
                pixels[px, py] = (205, 215, 215, alpha)
    image.save(FOG)
    print(f"wrote {FOG.name} ({width}x{height})")


if __name__ == "__main__":
    bake_overlay()
    bake_fog()
