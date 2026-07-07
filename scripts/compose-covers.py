#!/usr/bin/env python3
"""Compose portrait covers for the 3 open-source/homebrew games that have no
commercial box art, using their own official assets:
  - SuperTux : the game's Tux icon + logo (from its repo), on a snowy sky
  - SRB2     : the official srb2banner key art, on a Greenflower-green sky
  - xp-craft : an in-game voxel screenshot (Marco's own game)

Source art is fetched to scratch/art by the caller; output goes to
covers/<slug>.jpg at 600x900 (same 2:3 as the Steam/GOG covers).
"""
import os
import subprocess
import sys
from PIL import Image, ImageDraw, ImageFont, ImageFilter

ART = sys.argv[1] if len(sys.argv) > 1 else "/tmp/xpl-cover-art"
os.chdir(os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
os.makedirs("covers", exist_ok=True)
os.makedirs(ART, exist_ok=True)

# Official, freely-licensed source assets (fetched if missing).
SOURCES = {
    "supertux-icon.png": "https://raw.githubusercontent.com/SuperTux/supertux/master/data/images/engine/icons/supertux-256x256.png",
    "supertux-logo.png": "https://raw.githubusercontent.com/SuperTux/supertux/master/data/images/engine/menu/logo.png",
    "srb2banner.png": "https://raw.githubusercontent.com/STJr/SRB2/master/srb2banner.png",
}
for name, url in SOURCES.items():
    p = os.path.join(ART, name)
    if not os.path.exists(p):
        subprocess.run(["curl", "-sSL", "--max-time", "40", "-o", p, url], check=False)
W, H = 600, 900
FB = "/usr/share/fonts/truetype/noto/NotoSans-Bold.ttf"


def vgrad(top, bot):
    g = Image.new("RGB", (1, H))
    for y in range(H):
        t = y / (H - 1)
        g.putpixel((0, y), tuple(round(a + (b - a) * t) for a, b in zip(top, bot)))
    return g.resize((W, H))


def paste_fit(canvas, img, cx, cy, maxw, maxh):
    im = img.copy()
    im.thumbnail((maxw, maxh), Image.LANCZOS)
    x, y = int(cx - im.width / 2), int(cy - im.height / 2)
    canvas.paste(im, (x, y), im if im.mode == "RGBA" else None)


def title(d, text, y, size=54, fill=(255, 255, 255)):
    f = ImageFont.truetype(FB, size)
    # soft shadow for legibility
    d.text((W // 2 + 2, y + 3), text, font=f, fill=(0, 0, 0), anchor="mm")
    d.text((W // 2, y), text, font=f, fill=fill, anchor="mm")


# --- SuperTux: snowy sky + Tux + logo ------------------------------------
def supertux():
    c = vgrad((150, 195, 240), (30, 60, 120))
    d = ImageDraw.Draw(c)
    # a few soft snow dots
    for x, y, r in [(90, 140, 6), (500, 200, 5), (150, 640, 7), (470, 700, 6),
                    (300, 90, 5), (80, 400, 5), (520, 460, 6)]:
        d.ellipse((x - r, y - r, x + r, y + r), fill=(255, 255, 255))
    tux = Image.open(os.path.join(ART, "supertux-icon.png")).convert("RGBA")
    paste_fit(c, tux, W // 2, 400, 420, 460)
    logo = Image.open(os.path.join(ART, "supertux-logo.png")).convert("RGBA")
    paste_fit(c, logo, W // 2, 720, 480, 200)
    c.save("covers/supertux.jpg", quality=90)
    print("covers/supertux.jpg")


# --- SRB2: GFZ green sky + official banner -------------------------------
def srb2():
    c = vgrad((120, 200, 240), (36, 130, 70))     # sky blue -> GFZ green
    banner = Image.open(os.path.join(ART, "srb2banner.png")).convert("RGBA")
    # banner already contains Sonic + the SONIC ROBO BLAST 2 logo
    paste_fit(c, banner, W // 2, H // 2, 560, 560)
    c.save("covers/sonic-robo-blast-2.jpg", quality=90)
    print("covers/sonic-robo-blast-2.jpg")


# --- xp-craft: a custom isometric voxel scene (it's a Minecraft-like) -----
def iso_cube(d, cx, cy, s, h, top, left, right):
    """Draw an isometric cube; (cx,cy) is the apex of the top diamond."""
    tp, rt, bt, lf = (cx, cy), (cx + s, cy + s // 2), (cx, cy + s), (cx - s, cy + s // 2)
    d.polygon([tp, rt, bt, lf], fill=top)
    d.polygon([lf, bt, (cx, cy + s + h), (cx - s, cy + s // 2 + h)], fill=left)
    d.polygon([bt, rt, (cx + s, cy + s // 2 + h), (cx, cy + s + h)], fill=right)


def xpcraft(_shot=None):
    c = vgrad((135, 206, 235), (95, 155, 205))          # daytime sky
    d = ImageDraw.Draw(c)
    d.ellipse((430, 70, 520, 160), fill=(255, 236, 150))  # sun
    s, h = 46, 40
    GRASS = ((120, 190, 74), (86, 120, 52), (104, 142, 62))   # grass-topped
    DIRT = ((150, 110, 70), (96, 70, 44), (120, 88, 56))
    WOOD = ((120, 86, 52), (80, 56, 34), (100, 70, 44))
    LEAF = ((70, 150, 62), (44, 100, 42), (56, 124, 52))
    bx, by = 300, 430
    # isometric ground patch, drawn back-to-front
    tiles = sorted([(gx, gy) for gx in range(4) for gy in range(4)],
                   key=lambda t: t[0] + t[1])
    for gx, gy in tiles:
        cx = bx + (gx - gy) * s
        cy = by + (gx + gy) * (s // 2)
        iso_cube(d, cx, cy, s, h, *GRASS)
        iso_cube(d, cx, cy + h, s, h, *DIRT)            # a dirt layer below
    # a little tree on the back-centre tile
    tx, ty = bx + (1 - 1) * s, by + (1 + 1) * (s // 2) - 2 * h
    for i in range(2):
        iso_cube(d, tx, ty - i * h, s, h, *WOOD)
    for lgx in (-1, 0, 1):
        for lgy in (-1, 0, 1):
            iso_cube(d, tx + (lgx - lgy) * s, ty - 2 * h + (lgx + lgy) * (s // 2),
                     s, h, *LEAF)
    # bottom scrim + title
    scrim = Image.new("L", (W, H), 0)
    sd = ImageDraw.Draw(scrim)
    for y in range(H):
        t = (y / (H - 1) - 0.62) / 0.38
        sd.line((0, y, W, y), fill=max(0, min(200, int(200 * t))))
    c.paste(Image.new("RGB", (W, H), (0, 0, 0)), (0, 0), scrim)
    title(ImageDraw.Draw(c), "xp-craft", H - 66, size=62, fill=(150, 230, 150))
    c.save("covers/xp-craft.jpg", quality=90)
    print("covers/xp-craft.jpg")


supertux()
srb2()
xpcraft(sys.argv[2] if len(sys.argv) > 2 else "/tmp/gt/01_Jazz2.png")
