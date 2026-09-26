#!/usr/bin/env python3
"""Generate src/sprites.h: every bitmap of the battle screen.

Geometry: the terminal draws graphics at the text dot pitch, and the 9-inch
CRT gives dots a 3:5 horizontal-to-vertical pitch. All pictures are therefore
designed in "physical" units (a dot is 3 units wide, a line 5 units tall) and
sampled onto the dot raster, so circles stay round and a ship looks the same
lying down or standing up. A 32x19-dot cell of the big grid is 96x95 units,
square on the tube; a 14x8-dot cell of the mini map is 42x40.

Big-grid tiles are 4 bytes x 19 rows and are copied whole into a cell: row 0
is the cell's top grid line and bit 7 of byte 0 its left grid line, so a ship
lying across several cells can paint over the grid lines between them.
Overlays (cursor, crater, aim) are OR-ed, AND-NOT-ed or XOR-ed on top.
Mini-map sprites are drawn at any dot position by a shifting blitter.

Rows are MSB = leftmost dot. Labels use the terminal's own character-ROM
glyphs from the font sheet in the sibling p2000c-emulator checkout.

  python3 tools/gen_sprites.py            -> src/sprites.h
  python3 tools/gen_sprites.py --preview  -> also build/sprites_preview.png
"""
import math
import random
import sys
from pathlib import Path

from PIL import Image, ImageDraw

ROOT = Path(__file__).resolve().parent.parent
FONT_SHEET = ROOT.parent / "p2000c-emulator/assets/font/P2000C font mini.png"

SX, SY = 3, 5                       # physical units per dot / per line
K = 4                               # canvas pixels per physical unit (supersampling)
CELL_W, CELL_H = 32, 19             # big-grid cell, grid lines included
MINI_W, MINI_H = 14, 8              # mini-map cell (no grid lines)

# name, length, beam (units) on the big grid, beam on the mini map
SHIPS = [("Vliegdekschip", 5, 80, 26),
         ("Slagschip", 4, 76, 24),
         ("Kruiser", 3, 70, 22),
         ("Onderzeeboot", 3, 54, 18),
         ("Torpedojager", 2, 58, 18)]


# --- dot rasters -----------------------------------------------------------------

class Dots:
    """A w x h dot raster of 0/1 values."""

    def __init__(self, w, h):
        self.w, self.h = w, h
        self.p = [[0] * w for _ in range(h)]

    def copy(self):
        d = Dots(self.w, self.h)
        d.p = [row[:] for row in self.p]
        return d

    def get(self, x, y):
        return self.p[y][x] if 0 <= x < self.w and 0 <= y < self.h else 0

    def set(self, x, y, v=1):
        if 0 <= x < self.w and 0 <= y < self.h:
            self.p[y][x] = v

    def op(self, other, mode):
        for y in range(self.h):
            for x in range(self.w):
                o = other.p[y][x]
                if mode == "or":
                    self.p[y][x] |= o
                elif mode == "clear":
                    self.p[y][x] &= 1 - o
                elif mode == "xor":
                    self.p[y][x] ^= o

    def crop(self, x0, y0, w, h):
        d = Dots(w, h)
        for y in range(h):
            for x in range(w):
                d.p[y][x] = self.get(x0 + x, y0 + y)
        return d

    def to_bytes(self):
        """Row-major bytes, MSB = leftmost dot, width rounded up to bytes."""
        wb = (self.w + 7) // 8
        out = bytearray()
        for row in self.p:
            for b in range(wb):
                v = 0
                for i in range(8):
                    x = b * 8 + i
                    v = (v << 1) | (row[x] if x < self.w else 0)
                out.append(v)
        return bytes(out)


def sample(w, h, draw, threshold=0.5):
    """Draws in physical units (draw(d, s) with s = scale) and samples to dots."""
    canvas = Image.new("L", (w * SX * K, h * SY * K), 0)
    draw(ImageDraw.Draw(canvas), K)
    small = canvas.resize((w, h), Image.BOX)
    d = Dots(w, h)
    px = small.load()
    for y in range(h):
        for x in range(w):
            d.p[y][x] = 1 if px[x, y] >= threshold * 255 else 0
    return d


def outline(mask, thick_x=2):
    """Edge dots of a mask: in the mask, with a 4-neighbour outside it.
    Horizontally the edge is thick_x dots, so it is as heavy as one line."""
    d = Dots(mask.w, mask.h)
    for y in range(mask.h):
        for x in range(mask.w):
            if not mask.p[y][x]:
                continue
            edge = not mask.get(x, y - 1) or not mask.get(x, y + 1)
            for k in range(1, thick_x + 1):
                edge = edge or not mask.get(x - k, y) or not mask.get(x + k, y)
            d.p[y][x] = int(edge)
    return d


def erode(mask):
    """Removes the outline (two dots at the sides, one line at top and bottom)."""
    d = mask.copy()
    d.op(outline(mask), "clear")
    return d


def dilate(mask, rx=1, ry=1):
    d = Dots(mask.w, mask.h)
    for y in range(mask.h):
        for x in range(mask.w):
            if any(mask.get(x + i, y + j) for i in range(-rx, rx + 1) for j in range(-ry, ry + 1)):
                d.p[y][x] = 1
    return d


# --- ship designs in ship coordinates ------------------------------------------------
#
# u runs from the stern (0) to the bow (L), v across the ship (-B/2 port,
# +B/2 starboard). A design is a list of (mode, shape) steps; shapes are
# functions of a transform T(u, v) -> physical (x, y).

def profile_points(L, B, prof):
    hb = B / 2
    top = [(t * L, -f * hb) for t, f in prof]
    bottom = [(t * L, f * hb) for t, f in reversed(prof)]
    return top + bottom


WARSHIP = [(0, 0.60), (0.012, 0.78), (0.035, 0.92), (0.08, 1.0), (0.56, 1.0), (0.66, 0.95),
           (0.76, 0.83), (0.85, 0.64), (0.92, 0.42), (0.97, 0.2), (1.0, 0.0)]
CARRIER = [(0, 0.80), (0.02, 0.92), (0.05, 1.0), (0.80, 1.0), (0.88, 0.9), (0.95, 0.72), (1.0, 0.55)]
SUB = [(t / 20, (1 - abs(2 * t / 20 - 1) ** 2.2) ** 0.55) for t in range(21)]


def poly(points):
    def shape(d, s, T):
        d.polygon([tuple(c * s for c in T(u, v)) for u, v in points], fill=255)
    return shape


def circle(u, v, r):
    def shape(d, s, T):
        x, y = T(u, v)
        d.ellipse([(x - r) * s, (y - r) * s, (x + r) * s, (y + r) * s], fill=255)
    return shape


def ellipse_uv(u, v, ru, rv):
    """Ellipse with radii along / across the ship."""
    def shape(d, s, T):
        pts = [T(u + ru * math.cos(a), v + rv * math.sin(a)) for a in [k * math.pi / 24 for k in range(48)]]
        d.polygon([(x * s, y * s) for x, y in pts], fill=255)
    return shape


def rect(u0, v0, u1, v1):
    return poly([(u0, v0), (u1, v0), (u1, v1), (u0, v1)])


def turret(u, r, forward, twin=0.34, length=1.9, width=0.24):
    """Gun turret at (u, 0): a round house with two barrels towards the bow
    (forward) or the stern."""
    sign = 1 if forward else -1
    shapes = [circle(u, 0, r)]
    for off in (-twin * r, twin * r):
        shapes.append(rect(u, off - width * r, u + sign * length * r, off + width * r))
    return shapes


def erode_steps(shape, thr=0.5):
    return ("inset", shape, thr)


def turret_solid(u, r, forward):
    """Turret on a lit deck: a dark ring around a lit house, dark twin barrels."""
    sign = 1 if forward else -1
    steps = [("clear", circle(u, 0, r + 4), 0.4), ("fill", circle(u, 0, r), 0.5)]
    for off in (-0.36 * r, 0.36 * r):
        steps.append(("clear", rect(u + sign * r * 0.55, off - 2.6, u + sign * (r + 2.0 * r), off + 2.6), 0.4))
    steps.append(("clear", circle(u - sign * r * 0.25, 0, r * 0.28), 0.5))      # sighting hood
    return steps


def design(kind, L, B, mini=False):
    """Steps (mode, shape, threshold) for one ship: a lit hull with dark detail."""
    hb = B / 2
    if kind == 0:
        hull = poly(profile_points(L, B, CARRIER))
    elif kind == 3:
        hull = poly(profile_points(L, B, SUB))
    else:
        hull = poly(profile_points(L, B, WARSHIP))
    if mini:
        return hull, [("fill", hull, 0.5)]
    steps = [("fill", hull, 0.5), ("inset", hull, 0.5)]
    if kind == 0:                                       # aircraft carrier
        # flight deck: dark deck with lit edge, dashed centre line, landing lane, island
        for k in range(9):
            u0 = L * (0.07 + k * 0.095)
            steps.append(("clear", rect(u0, -2.2, u0 + L * 0.045, 2.2), 0.4))
        steps.append(("clear", poly([(L * 0.04, hb * 0.56), (L * 0.06, hb * 0.40),
                                     (L * 0.50, -hb * 0.62), (L * 0.48, -hb * 0.46)]), 0.4))
        steps.append(("clear", rect(L * 0.48, hb * 0.38, L * 0.68, hb), 0.5))
        steps.append(("fill", rect(L * 0.50, hb * 0.52, L * 0.66, hb * 0.98), 0.5))
        steps.append(("clear", rect(L * 0.555, hb * 0.66, L * 0.605, hb * 0.80), 0.5))
        for u0 in (0.73, 0.85):                          # two parked aircraft near the bow
            uc = L * u0
            steps.append(("clear", rect(uc - 11, -hb * 0.62, uc + 11, -hb * 0.44), 0.4))
            steps.append(("clear", rect(uc + 2, -hb * 0.88, uc + 7, -hb * 0.18), 0.4))
    elif kind == 1:                                     # battleship
        r = hb * 0.40
        for u0, fwd in ((0.745, True), (0.625, True), (0.17, False)):
            steps += turret_solid(L * u0, r, fwd)
        steps.append(("clear", rect(L * 0.36, -hb * 0.52, L * 0.53, hb * 0.52), 0.5))
        steps.append(("fill", rect(L * 0.38, -hb * 0.36, L * 0.51, hb * 0.36), 0.5))
        steps.append(("clear", ellipse_uv(L * 0.46, 0, L * 0.022, hb * 0.16), 0.5))
        steps.append(("clear", ellipse_uv(L * 0.30, 0, L * 0.04, hb * 0.30), 0.5))
        for side in (-1, 1):                            # secondary guns along the sides
            for u0 in (0.29, 0.41, 0.53):
                steps.append(("clear", circle(L * u0, side * hb * 0.74, hb * 0.12), 0.45))
    elif kind == 2:                                     # cruiser
        r = hb * 0.36
        for u0, fwd in ((0.77, True), (0.19, False)):
            steps += turret_solid(L * u0, r, fwd)
        steps.append(("clear", rect(L * 0.49, -hb * 0.50, L * 0.64, hb * 0.50), 0.5))
        steps.append(("fill", rect(L * 0.51, -hb * 0.34, L * 0.62, hb * 0.34), 0.5))
        steps.append(("clear", ellipse_uv(L * 0.35, 0, L * 0.035, hb * 0.28), 0.5))
        steps.append(("clear", ellipse_uv(L * 0.43, 0, L * 0.035, hb * 0.28), 0.5))
    elif kind == 3:                                     # submarine
        steps.append(("clear", ellipse_uv(L * 0.60, 0, L * 0.11, hb * 0.56), 0.4))
        steps.append(("fill", ellipse_uv(L * 0.60, 0, L * 0.085, hb * 0.34), 0.5))
        steps.append(("clear", rect(L * 0.10, -1.4, L * 0.46, 1.4), 0.35))
        steps.append(("clear", rect(L * 0.74, -1.4, L * 0.92, 1.4), 0.35))
        steps.append(("fill", poly([(0, -hb * 1.0), (L * 0.05, 0), (0, hb * 1.0)]), 0.5))
    else:                                               # destroyer
        r = hb * 0.40
        steps += turret_solid(L * 0.80, r, True)
        steps += turret_solid(L * 0.15, r, False)
        steps.append(("clear", rect(L * 0.55, -hb * 0.54, L * 0.69, hb * 0.54), 0.5))
        steps.append(("fill", rect(L * 0.57, -hb * 0.36, L * 0.67, hb * 0.36), 0.5))
        steps.append(("clear", ellipse_uv(L * 0.44, 0, L * 0.05, hb * 0.32), 0.5))
        steps.append(("clear", rect(L * 0.29, -hb * 0.62, L * 0.34, hb * 0.62), 0.4))
    return hull, steps


def render_ship(kind, length, beam, horizontal, cell_w, cell_h, margin, mini=False):
    """Renders a ship on its footprint of `length` cells. Returns (hull mask, picture)."""
    fw, fh = (length * cell_w, cell_h) if horizontal else (cell_w, length * cell_h)
    L = (length * cell_w * SX if horizontal else length * cell_h * SY) - 2 * margin
    grid = 0 if mini else 1                             # the grid line occupies dot/line 0
    if horizontal:
        cx0 = (grid + margin / SX) * SX                # physical x of the stern
        cy = (grid + (cell_h - grid) / 2) * SY

        def T(u, v):
            return (cx0 + u, cy + v)
    else:
        cxv = (grid + (cell_w - grid) / 2) * SX
        top = (grid + margin / SY) * SY

        def T(u, v):
            return (cxv + v, top + (L - u))            # bow up
    hull, steps = design(kind, L, beam, mini)
    mask = sample(fw, fh, lambda d, s: hull(d, s, T))
    pic = Dots(fw, fh)
    for mode, shape, thr in steps:
        layer = sample(fw, fh, lambda d, s: shape(d, s, T), thr)
        if mode == "outline":
            pic.op(outline(layer), "or")
        elif mode == "fill":
            pic.op(layer, "or")
        elif mode == "clear":
            pic.op(layer, "clear")
        elif mode == "inset":                           # dark line just inside the edge
            pic.op(outline(erode(layer)), "clear")
    return mask, pic


# --- big-grid tiles ------------------------------------------------------------------

WAVE = ["..###......###..",
        ".#...#....#...#.",
        "#.....####.....#"]
WAVE = ["..##.....",
        ".#..#..#.",
        "......#.."]
WAVE = [".##...",
        "#..#.#",
        "....#."]


def grid_tile():
    t = Dots(CELL_W, CELL_H)
    for x in range(CELL_W):
        t.set(x, 0)
    for y in range(CELL_H):
        t.set(0, y)
    return t


def stamp(t, pattern, x0, y0, mode=1):
    for dy, row in enumerate(pattern):
        for dx, ch in enumerate(row):
            if ch == "#":
                t.set(x0 + dx, y0 + dy, mode)


WAVE_SPOTS = [((5, 4), (18, 11)), ((16, 3), (5, 12)), ((10, 7), (22, 14))]


def wave_layout():
    """Wave layout per cell: random, but never the same as the cell to the
    left or above, so the sea does not look tiled."""
    rnd = random.Random(1941)
    layout = []
    for cell in range(100):
        taken = set()
        if cell % 10:
            taken.add(layout[cell - 1])
        if cell >= 10:
            taken.add(layout[cell - 10])
        layout.append(rnd.choice([v for v in range(3) if v not in taken]))
    return layout


def water_tile(variant):
    t = grid_tile()
    for x, y in WAVE_SPOTS[variant]:
        stamp(t, WAVE, x, y)
    return t


def cell_centre():
    """Physical centre of a cell's interior."""
    return ((1 + (CELL_W - 1) / 2) * SX, (1 + (CELL_H - 1) / 2) * SY)


def rings_tile(radii, dot=True, base=None):
    """Splash: concentric circles (round on the CRT) around the cell centre."""
    t = base.copy() if base else grid_tile()
    cx, cy = cell_centre()
    for r, w in radii:
        def ring(d, s, r=r, w=w):
            d.ellipse([(cx - r) * s, (cy - r) * s, (cx + r) * s, (cy + r) * s], fill=255)
            d.ellipse([(cx - r + w) * s, (cy - r + w) * s, (cx + r - w) * s, (cy + r - w) * s], fill=0)
        t.op(sample(CELL_W, CELL_H, ring, 0.45), "or")
    if dot:
        t.op(sample(CELL_W, CELL_H, lambda d, s: d.ellipse([(cx - 6) * s, (cy - 6) * s, (cx + 6) * s, (cy + 6) * s], fill=255)), "or")
    return t


def star(cx, cy, r_out, r_in, points, seed, jitter=0.25):
    rnd = random.Random(seed)
    pts = []
    for k in range(points * 2):
        a = math.pi * k / points + rnd.uniform(-0.12, 0.12)
        r = (r_out if k % 2 == 0 else r_in) * (1 + rnd.uniform(-jitter, jitter))
        pts.append((cx + r * math.cos(a), cy + r * math.sin(a)))
    return pts


def fire_tile():
    """A hit on an unknown ship: a burning star with a dark heart."""
    t = grid_tile()
    cx, cy = cell_centre()
    burst = sample(CELL_W, CELL_H, lambda d, s: d.polygon([(x * s, y * s) for x, y in star(cx, cy, 40, 17, 9, 7)], fill=255))
    heart = sample(CELL_W, CELL_H, lambda d, s: d.polygon([(x * s, y * s) for x, y in star(cx, cy, 20, 9, 7, 3)], fill=255))
    core = sample(CELL_W, CELL_H, lambda d, s: d.ellipse([(cx - 6) * s, (cy - 6) * s, (cx + 6) * s, (cy + 6) * s], fill=255))
    t.op(burst, "or")
    t.op(heart, "clear")
    t.op(core, "or")
    return t


def boom_tile():
    """Animation frame: the explosion at its largest."""
    t = grid_tile()
    cx, cy = cell_centre()
    burst = sample(CELL_W, CELL_H, lambda d, s: d.polygon([(x * s, y * s) for x, y in star(cx, cy, 47, 24, 11, 11, 0.15)], fill=255))
    inner = sample(CELL_W, CELL_H, lambda d, s: d.polygon([(x * s, y * s) for x, y in star(cx, cy, 30, 15, 8, 5)], fill=255))
    t.op(burst, "or")
    t.op(outline(inner), "clear")
    return t


def splash_tile():
    """Animation frame: the column of water thrown up by a miss (a ring of
    droplets around a solid plume)."""
    t = grid_tile()
    cx, cy = cell_centre()
    plume = sample(CELL_W, CELL_H, lambda d, s: d.polygon([(x * s, y * s) for x, y in star(cx, cy, 24, 14, 8, 9, 0.2)], fill=255))
    t.op(plume, "or")
    for k in range(10):
        a = 2 * math.pi * k / 10 + 0.2
        x, y = cx + 38 * math.cos(a), cy + 36 * math.sin(a)
        t.op(sample(CELL_W, CELL_H, lambda d, s, x=x, y=y: d.ellipse([(x - 4) * s, (y - 4) * s, (x + 4) * s, (y + 4) * s], fill=255), 0.3), "or")
    return t


def crater():
    """Wreck overlay: a ragged dark shell hole (AND NOT) with burning debris (OR)."""
    cx, cy = cell_centre()
    hole_pts = star(cx + 2, cy, 30, 13, 8, 21, 0.35)
    mask = sample(CELL_W, CELL_H, lambda d, s: d.polygon([(x * s, y * s) for x, y in hole_pts], fill=255))
    ink = Dots(CELL_W, CELL_H)
    for x, y in ((14, 8), (15, 8), (19, 11), (20, 11), (13, 12), (17, 6), (18, 13)):
        ink.set(x, y)
    for y in range(CELL_H):                                 # never touch the grid lines
        mask.set(0, y, 0)
    for x in range(CELL_W):
        mask.set(x, 0, 0)
    return mask, ink


def cursor_overlay():
    """Corner brackets inside the cell, XOR-ed on whatever the cell shows."""
    t = Dots(CELL_W, CELL_H)
    x0, x1, y0, y1 = 2, CELL_W - 2, 2, CELL_H - 2
    for x in list(range(x0, x0 + 6)) + list(range(x1 - 5, x1 + 1)):
        t.set(x, y0)
        t.set(x, y1)
    for y in list(range(y0, y0 + 4)) + list(range(y1 - 3, y1 + 1)):
        for x in (x0, x0 + 1, x1 - 1, x1):
            t.set(x, y)
    return t


def aim_overlay():
    """Animation frame: a gun sight closing on the cell (XOR)."""
    t = Dots(CELL_W, CELL_H)
    cx, cy = cell_centre()
    ring = sample(CELL_W, CELL_H, lambda d, s: (d.ellipse([(cx - 30) * s, (cy - 30) * s, (cx + 30) * s, (cy + 30) * s], fill=255),
                                                   d.ellipse([(cx - 25) * s, (cy - 25) * s, (cx + 25) * s, (cy + 25) * s], fill=0)), 0.45)
    t.op(ring, "or")
    mx, my = 16, 9
    for x in list(range(4, 12)) + list(range(21, 29)):
        t.set(x, my + 1)
    for y in list(range(2, 7)) + list(range(13, 18)):
        t.set(mx, y)
        t.set(mx + 1, y)
    return t


def invert_overlay():
    t = Dots(CELL_W, CELL_H)
    for y in range(1, CELL_H):
        for x in range(1, CELL_W):
            t.set(x, y)
    return t


def ship_tiles():
    """Tiles of every ship, both orientations, and the hull masks (for the preview)."""
    tiles, hulls = [], []
    for kind, (name, n, beam, _) in enumerate(SHIPS):
        for horizontal in (True, False):
            mask, pic = render_ship(kind, n, beam, horizontal, CELL_W, CELL_H, 9)
            fw, fh = pic.w, pic.h
            full = Dots(fw, fh)
            for x in range(fw):                         # grid lines of the footprint...
                full.set(x, 0)
            for y in range(fh):
                full.set(0, y)
            if horizontal:
                for k in range(1, n):
                    for y in range(fh):
                        full.set(k * CELL_W, y)
            else:
                for k in range(1, n):
                    for x in range(fw):
                        full.set(x, k * CELL_H)
            full.op(dilate(mask, 2, 1), "clear")        # ...stop at the hull, with a gap
            full.op(pic, "or")
            for k in range(n):
                if horizontal:
                    tiles.append(full.crop(k * CELL_W, 0, CELL_W, CELL_H))
                else:
                    tiles.append(full.crop(0, k * CELL_H, CELL_W, CELL_H))
            hulls.append(mask)
    return tiles, hulls


# --- mini map ----------------------------------------------------------------------

def mini_ships():
    """Per ship and orientation: (intact silhouette, sunk ghost) on the footprint.
    A sunk ship is its silhouette in a checkerboard dither, dim on the tube."""
    out = []
    for kind, (name, n, _, beam) in enumerate(SHIPS):
        for horizontal in (True, False):
            mask, pic = render_ship(kind, n, beam, horizontal, MINI_W, MINI_H, 5, mini=True)
            ghost = Dots(pic.w, pic.h)
            for y in range(pic.h):
                for x in range(pic.w):
                    ghost.p[y][x] = pic.p[y][x] & ((x + y) & 1)
            ghost.op(outline(mask, 1), "or")
            out.append((pic, ghost))
    return out


MINI_HIT = ["#...#", ".#.#.", "..#..", ".#.#.", "#...#"]
MINI_HIT = ["##.##", "..#..", "##.##"]
MINI_MISS = [".###.", "#...#", ".###."]


# --- glyphs ------------------------------------------------------------------------

def glyph(sheet, code):
    rows = []
    for gy in range(12):
        bits = 0
        for gx in range(8):
            bits = (bits << 1) | int(sheet.getpixel(((code & 15) * 12 + gx, (code >> 4) * 12 + gy))[1] != 0)
        rows.append(bits)
    return rows


def number_label(sheet, n):
    """'1'..'10' right-aligned in 16 dots: the last lit column is dot 11."""
    d = Dots(16, 12)
    text = str(n)
    x = 16 - 8 * len(text) - 3
    for ch in text:
        for gy, bits in enumerate(glyph(sheet, ord(ch))):
            for gx in range(8):
                if bits & (0x80 >> gx):
                    d.set(x + gx, gy)
        x += 7 if ch == "1" else 8
    return d


# --- output ------------------------------------------------------------------------

def c_bytes(data, per_line=19 * 4):
    return ", ".join(f"0x{b:02X}" for b in data)


def c_array(name, data, comment):
    return f"/* {comment} */\nstatic const unsigned char {name}[{len(data)}] = {{ {c_bytes(data)} }};\n"


def build():
    sheet = Image.open(FONT_SHEET).convert("RGB")
    ships, _ = ship_tiles()
    mask, rim = crater()
    minis = mini_ships()
    out = ["/* Generated by tools/gen_sprites.py -- do not edit. */",
           "#ifndef SPRITES_H", "#define SPRITES_H", "",
           "#define TILE_BYTES 76                        /* 4 bytes x 19 rows */", ""]
    out.append("/* water, three wave layouts */")
    out.append(f"static const unsigned char tile_water[3][TILE_BYTES] = {{")
    for v in range(3):
        out.append("    { " + c_bytes(water_tile(v).to_bytes()) + " },")
    out.append("};\n")
    out.append(c_array("wave_layout", bytes(wave_layout()), "wave layout (0-2) of each cell"))
    out.append(c_array("tile_miss", rings_tile([(28, 5), (15, 4)], True).to_bytes(), "a miss: splash rings"))
    out.append(c_array("tile_fire", fire_tile().to_bytes(), "a hit on a ship not yet sunk: fire"))
    out.append(c_array("tile_boom", boom_tile().to_bytes(), "animation: explosion"))
    out.append(c_array("tile_splash", splash_tile().to_bytes(), "animation: plume of a miss"))
    out.append("/* ship segments: per ship (fleet order), horizontal (stern left, bow right)")
    out.append(" * then vertical (bow up), segment 0 = left / top cell */")
    out.append(f"static const unsigned char tile_ship[{len(ships)}][TILE_BYTES] = {{")
    for t in ships:
        out.append("    { " + c_bytes(t.to_bytes()) + " },")
    out.append("};\n")
    first, idx = [], 0
    for name, n, _, _ in SHIPS:
        first.append(idx)
        idx += 2 * n
    out.append("/* index of each ship's first horizontal segment in tile_ship */")
    out.append("static const unsigned char ship_tile_first[5] = { " + ", ".join(map(str, first)) + " };\n")
    out.append(c_array("over_crater_mask", mask.to_bytes(), "wreck: hole (AND NOT)"))
    out.append(c_array("over_crater_ink", rim.to_bytes(), "wreck: ragged rim (OR)"))
    out.append(c_array("over_cursor", cursor_overlay().to_bytes(), "cursor brackets (XOR)"))
    out.append(c_array("over_aim", aim_overlay().to_bytes(), "animation: gun sight (XOR)"))
    out.append(c_array("over_invert", invert_overlay().to_bytes(), "invalid placement: inverse video (XOR)"))

    # mini-map ships in one blob: per ship and orientation intact then sunk
    blob, table = bytearray(), []
    for k, (pic, sunk) in enumerate(minis):
        wb = (pic.w + 7) // 8
        table.append((len(blob), wb, pic.h))
        blob += pic.to_bytes()
        blob += sunk.to_bytes()
    out.append("/* mini-map ships: per ship, horizontal then vertical: the silhouette, then")
    out.append(" * directly after it the dithered ghost shown once the ship is sunk */")
    out.append(c_array("mini_ship_data", bytes(blob), "silhouettes and ghosts"))
    out.append("static const unsigned int mini_ship_offset[10] = { " + ", ".join(str(o) for o, _, _ in table) + " };")
    out.append("static const unsigned int mini_ship_wh[10] = { " +
               ", ".join(f"0x{h:02X}{w:02X}" for _, w, h in table) + " };   /* WH(bytes, rows) */\n")
    hit = Dots(5, 3)
    stamp(hit, MINI_HIT, 0, 0)
    out.append(c_array("mini_hit", hit.to_bytes(), "mini map: hit (XOR), 5x3 dots"))
    miss = Dots(5, 3)
    stamp(miss, MINI_MISS, 0, 0)
    out.append(c_array("mini_miss", miss.to_bytes(), "mini map: miss, a 5x3-dot ring"))

    out.append("/* character-ROM glyphs A-J: 1 byte x 12 rows */")
    out.append("static const unsigned char glyph_letter[10][12] = {")
    for ch in "ABCDEFGHIJ":
        out.append("    { " + ", ".join(f"0x{b:02X}" for b in glyph(sheet, ord(ch))) + f" }}, /* {ch} */")
    out.append("};\n")
    out.append("/* row numbers 1-10, right-aligned in 2 bytes x 12 rows */")
    out.append("static const unsigned char glyph_number[10][24] = {")
    for n in range(1, 11):
        out.append("    { " + c_bytes(number_label(sheet, n).to_bytes()) + f" }}, /* {n} */")
    out.append("};\n")
    out.append("#endif")
    (ROOT / "src/sprites.h").write_text("\n".join(out) + "\n")
    print("wrote src/sprites.h")


# --- preview -----------------------------------------------------------------------

def preview(out_path):
    """A sheet of all tiles plus a mock battle field, rendered like the screenshots."""
    ships, _ = ship_tiles()
    mask, rim = crater()
    minis = mini_ships()
    W, H = 512, 252
    img = Dots(W, H)

    def put(t, x, y, mode="or"):
        for yy in range(t.h):
            for xx in range(t.w):
                v = t.p[yy][xx]
                if mode == "or" and v:
                    img.set(x + xx, y + yy)
                elif mode == "copy":
                    img.set(x + xx, y + yy, v)
                elif mode == "xor" and v:
                    img.set(x + xx, y + yy, 1 - img.get(x + xx, y + yy))
                elif mode == "clear" and v:
                    img.set(x + xx, y + yy, 0)

    # big grid 10x10 at x=16, y=22
    gx, gy = 16, 22
    rnd = random.Random(3)
    board = {}
    idx = 0
    placements = [(0, True, 1, 1), (1, False, 7, 2), (2, True, 3, 8), (3, False, 0, 4), (4, True, 5, 5)]
    first = []
    for name, n, _, _ in SHIPS:
        first.append(idx)
        idx += 2 * n
    for kind, horiz, c, r in placements:
        n = SHIPS[kind][1]
        for k in range(n):
            cc, rr = (c + k, r) if horiz else (c, r + k)
            board[(cc, rr)] = first[kind] + (0 if horiz else n) + k
    for r in range(10):
        for c in range(10):
            x, y = gx + c * CELL_W, gy + r * CELL_H
            if (c, r) in board:
                put(ships[board[(c, r)]], x, y, "copy")
                if (c + r) % 3 == 0:
                    put(mask, x, y, "clear")
                    put(rim, x, y)
            else:
                put(water_tile(wave_layout()[r * 10 + c]), x, y, "copy")
    for c, r in ((4, 3), (9, 9), (2, 0)):
        put(rings_tile([(28, 5), (15, 4)], True), gx + c * CELL_W, gy + r * CELL_H, "copy")
    put(fire_tile(), gx + 8 * CELL_W, gy + 7 * CELL_H, "copy")
    put(boom_tile(), gx + 9 * CELL_W, gy + 0 * CELL_H, "copy")
    put(splash_tile(), gx + 9 * CELL_W, gy + 1 * CELL_H, "copy")
    put(cursor_overlay(), gx + 5 * CELL_W, gy + 3 * CELL_H, "xor")
    put(aim_overlay(), gx + 9 * CELL_W, gy + 3 * CELL_H, "xor")
    for x in range(gx, gx + 10 * CELL_W + 1):
        img.set(x, gy + 10 * CELL_H)
        img.set(x, gy - 2)
        img.set(x, gy + 10 * CELL_H + 2)
    for y in range(gy - 2, gy + 10 * CELL_H + 3):
        img.set(gx + 10 * CELL_W, y) if y >= gy else None
        img.set(gx - 2, y)
        img.set(gx + 10 * CELL_W + 2, y)
    sheet = Image.open(FONT_SHEET).convert("RGB")
    for c, ch in enumerate("ABCDEFGHIJ"):
        g = Dots(8, 12)
        for yy, bits in enumerate(glyph(sheet, ord(ch))):
            for xx in range(8):
                g.p[yy][xx] = int(bool(bits & (0x80 >> xx)))
        put(g, gx + c * CELL_W + 12, 6)
    for r in range(10):
        put(number_label(sheet, r + 1), 0, gy + r * CELL_H + 4)
    # mini map at x=362, y=38
    mx, my = 362, 38
    for kind, horiz, c, r in [(0, False, 8, 2), (1, True, 1, 1), (2, True, 4, 8), (3, False, 1, 4), (4, True, 4, 5)]:
        pic, sunk = minis[kind * 2 + (0 if horiz else 1)]
        put(sunk if kind == 4 else pic, mx + c * MINI_W, my + r * MINI_H)
    hit = Dots(5, 3)
    stamp(hit, MINI_HIT, 0, 0)
    put(hit, mx + 2 * MINI_W + 5, my + 1 * MINI_H + 2, "xor")
    miss = Dots(5, 3)
    stamp(miss, MINI_MISS, 0, 0)
    for c, r in ((0, 0), (6, 3), (9, 9)):
        put(miss, mx + c * MINI_W + 4, my + r * MINI_H + 2)
    for r in range(11):
        for c in range(11):
            img.set(mx + c * MINI_W - 1 if c else mx - 1, my + r * MINI_H - 1 if r else my - 1)
    for x in range(mx - 2, mx + 10 * MINI_W + 2):
        img.set(x, my - 2)
        img.set(x, my + 10 * MINI_H + 1)
    for y in range(my - 2, my + 10 * MINI_H + 2):
        img.set(mx - 2, y)
        img.set(mx + 10 * MINI_W + 1, y)
    # render: one dot -> 3x5 block
    pic = Image.new("L", (W, H), 0)
    pic.putdata([255 if img.p[y][x] else 0 for y in range(H) for x in range(W)])
    pic = pic.resize((W * SX, H * SY), Image.NEAREST)
    Image.merge("RGB", [pic.point(lambda v, c=c: c if v else 0) for c in (51, 255, 51)]).save(out_path)
    print(f"wrote {out_path}")


if __name__ == "__main__":
    build()
    if "--preview" in sys.argv:
        (ROOT / "build").mkdir(exist_ok=True)
        preview(ROOT / "build/sprites_preview.png")
