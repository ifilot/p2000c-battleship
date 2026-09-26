#!/usr/bin/env python3
"""Generate the title bitmap (512x252 monochrome) as RLE data in src/splash.h.

The picture is designed on a 1536x1260 canvas (the CRT's 3:5 dot pitch) and
reduced to dots. Motif: a 1940s battleship at night firing a broadside, with
muzzle flashes, gun smoke and the fall of shot throwing up columns of water
on the horizon, under a moon whose light glitters on the sea; the title in
the terminal's own character-ROM glyphs scaled up. Also writes
build/splash.png, a preview rendered like the game screenshots.
"""
import math
import random
from pathlib import Path

from PIL import Image, ImageDraw

ROOT = Path(__file__).resolve().parent.parent
FONT_SHEET = ROOT.parent / "p2000c-emulator/assets/font/P2000C font mini.png"
W, H = 512, 252
SX, SY = 3, 5                     # dot pitch
CW, CH = W * SX, H * SY


def glyph_rows(sheet, code):
    return [[sheet.getpixel(((code & 15) * 12 + gx, (code >> 4) * 12 + gy))[1] != 0
             for gx in range(8)] for gy in range(12)]


def draw_text(dots, sheet, text, x, y, scale_x, scale_y, spacing=0, value=1):
    """Draws ROM glyphs into the dot image, scaled per axis."""
    for ch in text:
        rows = glyph_rows(sheet, ord(ch))
        for gy in range(12):
            for gx in range(8):
                if rows[gy][gx]:
                    for dy in range(scale_y):
                        for dx in range(scale_x):
                            px, py = x + gx * scale_x + dx, y + gy * scale_y + dy
                            if 0 <= px < W and 0 <= py < H:
                                dots.putpixel((px, py), value)
        x += 8 * scale_x + spacing


def D(x, y):
    """Dot coordinates -> canvas coordinates."""
    return (x * SX, y * SY)


def P(points):
    return [D(x, y) for x, y in points]


def reduce_to_dots(canvas, need=5):
    """A dot is lit when enough of its 3x5 canvas block is."""
    dots = Image.new("1", (W, H), 0)
    small = canvas.resize((W, H), Image.BOX)
    px = small.load()
    for y in range(H):
        for x in range(W):
            if px[x, y] * SX * SY >= 255 * need:
                dots.putpixel((x, y), 1)
    return dots


def smoke(dots, cx, cy, rx, ry, density, seed):
    """Gun or funnel smoke: a stipple of dots thinning out towards the edge
    (in dot coordinates, applied after the reduction)."""
    rnd = random.Random(seed)
    for _ in range(int(rx * ry * density)):
        a = rnd.uniform(0, 2 * math.pi)
        r = rnd.random() ** 0.8
        x, y = int(cx + r * rx * math.cos(a)), int(cy + r * ry * math.sin(a))
        if 0 <= x < W and 0 <= y < H and rnd.random() > r * 0.6:
            dots.putpixel((x, y), 1)


def flash(d, cx, cy, r_out, r_in, n, seed, stretch=1.0):
    """Muzzle flash: a star, longer in the direction of fire (to the right)."""
    rnd = random.Random(seed)
    pts = []
    for k in range(2 * n):
        a = math.pi * k / n + rnd.uniform(-0.1, 0.1)
        r = (r_out if k % 2 == 0 else r_in) * rnd.uniform(0.7, 1.15)
        rx = r * (stretch if math.cos(a) > 0 else 0.6)
        pts.append((cx + rx * math.cos(a), cy + r * 0.6 * math.sin(a)))
    d.polygon(P(pts), fill=255)


def plume(d, cx, base, height, width, seed):
    """A column of water thrown up by a falling shell: solid, widening
    upwards, with spray thrown off the top."""
    rnd = random.Random(seed)
    steps = 8
    left = [(cx - width * (0.45 + 0.55 * t ** 1.5) - rnd.uniform(0, 0.8), base - height * t) for t in [k / steps for k in range(steps + 1)]]
    right = [(cx + width * (0.45 + 0.55 * t ** 1.5) + rnd.uniform(0, 0.8), base - height * t) for t in [k / steps for k in range(steps + 1)]]
    top = [(cx + width * math.cos(a), base - height - width * 0.9 * math.sin(a) * rnd.uniform(0.6, 1.2))
           for a in [math.pi * k / 6 for k in range(7)]]
    d.polygon(P(left + top + right[::-1]), fill=255)
    for k in range(10):                                    # spray
        sx = cx + rnd.uniform(-2.2, 2.2) * width
        sy = base - height - width * rnd.uniform(1.2, 2.6)
        d.rectangle([*D(sx, sy), sx * SX + 3, sy * SY + 5], fill=255)


def battleship(d, lw):
    """Side view, bow to the right, waterline at y = 182; all turrets trained
    to starboard-forward, barrels raised."""
    wl = 182
    # hull: sheer rising to a raked, flared bow; rounded cruiser stern
    hull = [(34, wl), (390, wl), (404, 170), (416, 156), (422, 152), (398, 155), (340, 160),
            (250, 162), (110, 162), (40, 161), (26, 162), (24, 168), (28, 174)]
    d.polygon(P(hull), fill=255)
    d.line(P([(38, wl - 5), (392, wl - 5)]), fill=0, width=lw)          # boot topping
    for x in range(56, 384, 8):                                         # portholes
        d.rectangle([*D(x, 168), x * SX + 3, 168 * SY + 4], fill=0)
    # turret: sloped front, flat roof, barrels raised towards the bow
    def turret(x0, x1, base, top, gun_from, gun_to):
        d.polygon(P([(x0, base), (x0 + 2, top), (x1 - 6, top), (x1, base - 3), (x1, base)]), fill=255)
        d.line(P([gun_from, gun_to]), fill=255, width=lw * 2)
    d.rectangle([*D(80, 158), *D(110, 162)], fill=255)                  # barbette C
    turret(78, 114, 158, 151, (110, 153), (150, 139))                     # C, over the stern
    d.rectangle([*D(318, 152), *D(346, 161)], fill=255)                 # barbette B
    turret(314, 350, 152, 144, (346, 146), (392, 128))                    # B, superfiring
    turret(350, 386, 160, 152, (382, 154), (428, 138))                    # A
    # superstructure: deckhouse, upper deck with windows, bridge tower
    d.polygon(P([(146, 162), (150, 150), (300, 150), (306, 162)]), fill=255)
    d.rectangle([*D(172, 140), *D(288, 150)], fill=255)
    for x in range(178, 286, 9):
        d.rectangle([*D(x, 143), x * SX + 9, 145 * SY + 4], fill=0)
    d.polygon(P([(250, 140), (256, 112), (284, 112), (290, 140)]), fill=255)       # tower
    for x in (260, 268, 276):
        d.rectangle([*D(x, 118), x * SX + 5, 121 * SY + 4], fill=0)
    d.rectangle([*D(244, 108), *D(296, 112)], fill=255)                              # bridge wings
    d.rectangle([*D(260, 98), *D(282, 108)], fill=255)                               # director
    d.rectangle([*D(252, 101), *D(290, 103)], fill=255)                              # rangefinder
    d.line(P([(271, 98), (271, 62)]), fill=255, width=lw)                          # foremast
    d.line(P([(263, 72), (279, 72)]), fill=255, width=lw)
    d.line(P([(265, 82), (277, 82)]), fill=255, width=lw)
    # funnel, raked aft, with its cap
    d.polygon(P([(196, 140), (202, 116), (226, 116), (232, 140)]), fill=255)
    d.rectangle([*D(200, 113), *D(228, 116)], fill=255)
    d.line(P([(200, 118), (228, 118)]), fill=0, width=lw)
    # mainmast
    d.line(P([(178, 140), (176, 92)]), fill=255, width=lw)
    d.line(P([(169, 100), (183, 100)]), fill=255, width=lw)
    d.line(P([(176, 92), (271, 62)]), fill=255, width=2)                             # aerial
    # secondary turrets along the deckhouse
    for x in (160, 296):
        d.polygon(P([(x - 7, 150), (x - 5, 145), (x + 7, 145), (x + 9, 150)]), fill=255)
        d.line(P([(x + 6, 147), (x + 20, 142)]), fill=255, width=lw)


def design():
    canvas = Image.new("L", (CW, CH), 0)
    d = ImageDraw.Draw(canvas)
    lw = 5
    rnd = random.Random(1944)

    horizon = 170
    # moon, low on the left, and its glitter path on the sea
    mx, my, rx, ry = 44, 92, 20, 12
    d.ellipse([*D(mx - rx, my - ry), *D(mx + rx, my + ry)], fill=255)
    d.ellipse([*D(mx - rx + 9, my - ry - 2), *D(mx + rx + 9, my + ry - 2)], fill=0)      # crescent
    for y in range(horizon + 3, H, 4):
        w_ = 3 + (y - horizon) // 5
        for _ in range(2):
            x = mx + rnd.uniform(-w_, w_)
            d.line(P([(x - 3, y), (x + 3, y)]), fill=255, width=lw)
    # stars
    for _ in range(40):
        x, y = rnd.uniform(4, 508), rnd.uniform(62, 150)
        if 20 < x < 70 and 76 < y < 108:
            continue
        d.rectangle([*D(x, y), x * SX + 3, y * SY + 5], fill=255)
    # horizon and waves: sparse dashes, denser towards the viewer
    d.line(P([(0, horizon), (W, horizon)]), fill=255, width=lw)
    y, step = horizon + 6, 6.0
    while y < 232:
        x = rnd.uniform(0, 20)
        while x < W:
            length = rnd.uniform(6, 16)
            d.line(P([(x, y), (x + length, y)]), fill=255, width=lw)
            x += length + rnd.uniform(14, 40)
        step *= 1.12
        y += step
    # fall of shot around an enemy far away on the horizon
    d.polygon(P([(462, horizon), (466, horizon - 4), (480, horizon - 4), (483, horizon - 9),
                 (486, horizon - 4), (494, horizon - 4), (498, horizon)]), fill=255)
    plume(d, 450, horizon, 26, 6.0, 5)
    plume(d, 476, horizon, 36, 7.0, 6)
    plume(d, 503, horizon, 20, 5.0, 7)
    # the battleship; the waves stop at its outline
    ship = Image.new("L", (CW, CH), 0)
    battleship(ImageDraw.Draw(ship), lw)
    canvas.paste(ship, (0, 0), ship)
    # the broadside: flashes at the muzzles
    flash(d, 400, 125, 18, 6, 7, 1, 1.5)
    flash(d, 436, 135, 16, 6, 7, 2, 1.5)
    flash(d, 158, 136, 13, 5, 7, 3, 1.4)
    # the bow wave
    for k in range(3):
        d.line(P([(392 - k * 9, 183 + k * 3), (414 - k * 5, 185 + k * 3)]), fill=255, width=lw)

    dots = reduce_to_dots(canvas)
    smoke(dots, 420, 112, 26, 9, 0.9, 11)                 # gun smoke drifting off
    smoke(dots, 176, 124, 18, 7, 0.9, 12)
    smoke(dots, 190, 100, 30, 8, 0.6, 13)                 # funnel smoke, blown aft
    smoke(dots, 150, 92, 30, 7, 0.35, 14)
    sheet = Image.open(FONT_SHEET).convert("RGB")
    draw_text(dots, sheet, "ZEESLAG", 88, 4, 6, 4)
    draw_text(dots, sheet, "PHILIPS P2000C", 144, 52, 2, 1)
    for y in range(238, 252):
        for x in range(W):
            dots.putpixel((x, y), 0)
    draw_text(dots, sheet, "druk op een toets", 368, 240, 1, 1)
    return dots


def to_bytes(dots):
    data = bytearray(W // 8 * H)
    for y in range(H):
        for x in range(W):
            if dots.getpixel((x, y)):
                data[y * 64 + x // 8] |= 0x80 >> (x % 8)
    return bytes(data)


def rle(data):
    """(count, value) pairs, count 1..255."""
    out = bytearray()
    i = 0
    while i < len(data):
        j = i
        while j < len(data) and data[j] == data[i] and j - i < 255:
            j += 1
        out += bytes([j - i, data[i]])
        i = j
    return bytes(out)


def preview(data, out):
    img = Image.new("L", (W, H), 0)
    img.putdata([255 if data[y * 64 + x // 8] & (0x80 >> (x % 8)) else 0 for y in range(H) for x in range(W)])
    img = img.resize((W * SX, H * SY), Image.NEAREST)
    Image.merge("RGB", [img.point(lambda v, c=c: c if v else 0) for c in (51, 255, 51)]).save(out)


if __name__ == "__main__":
    data = to_bytes(design())
    packed = rle(data)
    spans = sum(1 for y in range(H) if any(data[y * 64:(y + 1) * 64]))
    lit = sum(bin(b).count("1") for b in data)
    body = ",".join(f"0x{b:02X}" for b in packed)
    (ROOT / "src/splash.h").write_text(
        "/* Generated by tools/gen_splash.py -- do not edit. Title bitmap, 512x252,\n"
        " * run-length encoded as (count, value) pairs. */\n#ifndef SPLASH_H\n#define SPLASH_H\n\n"
        f"#define SPLASH_RLE_SIZE {len(packed)}\nstatic const unsigned char splash_rle[SPLASH_RLE_SIZE] = {{\n{body}\n}};\n\n#endif\n")
    (ROOT / "build").mkdir(exist_ok=True)
    preview(data, ROOT / "build/splash.png")
    print(f"splash: {lit} dots lit, {spans} non-empty lines, RLE {len(packed)} bytes; preview build/splash.png")
