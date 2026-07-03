#!/usr/bin/env python3
"""Generate label BMPs for the launcher (Pillow, Linux side).

White text on pure magenta; the app color-keys magenta (255,0,255) to
transparent. Plain 24-bit RGB BMPs so SDL_LoadBMP reads them directly.
"""
import os
from PIL import Image, ImageDraw, ImageFont

FONT = "/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf"
MAGENTA = (255, 0, 255)
PAD = 8

os.chdir(os.path.join(os.path.dirname(__file__), ".."))
os.makedirs("assets", exist_ok=True)

def label(name, text, size):
    font = ImageFont.truetype(FONT, size)
    l, t, r, b = font.getbbox(text)
    img = Image.new("RGB", (r - l + 2 * PAD, b - t + 2 * PAD), MAGENTA)
    draw = ImageDraw.Draw(img)
    # No antialiasing: AA edges blend toward magenta and survive the colorkey
    # as a pink fringe. Hard edges read as period-correct on a TV anyway.
    draw.fontmode = "1"
    draw.text((PAD - l, PAD - t), text, font=font, fill=(255, 255, 255))
    img.save(f"assets/{name}.bmp")

label("games", "GAMES", 48)
label("music", "MUSIC", 48)
label("movies", "MOVIES", 48)
label("title", "PERFORMA ENTERTAINMENT SYSTEM", 36)
print("assets:", sorted(os.listdir("assets")))
