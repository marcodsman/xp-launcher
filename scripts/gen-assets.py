#!/usr/bin/env python3
"""Asset pipeline for xp-launcher (runs on the Linux laptop).

Reads games.json and pre-renders EVERY screen state of the launcher as a
full 1024x768 BMP: gradients, glow, drawn icons, antialiased type, shadows.
Everything is drawn at 2x and LANCZOS-downscaled, so the whole UI gets
supersampled antialiasing for free. The XP box never composites anything —
it just blits one bitmap per frame (see src/main.c).

Outputs:
  assets/home_{0,1,2}.bmp     home screen, one per selected tile
  assets/list_{i}.bmp         games list, one per selected row
  assets/launch.bmp           "starting..." interstitial shown while a game boots
  assets/games.cfg            index|exe|args|cwd lines the C side parses
"""
import json
import os
from PIL import Image, ImageDraw, ImageFilter, ImageFont

W, H = 1024, 768
SS = 2                       # supersampling factor
SW, SH = W * SS, H * SS

FONT_BOLD = "/usr/share/fonts/truetype/noto/NotoSans-Bold.ttf"
FONT_REG = "/usr/share/fonts/truetype/noto/NotoSans-Regular.ttf"
# Noto Sans has no arrow glyphs (they render as tofu); DejaVu does.
FONT_SYM = "/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf"

BG_TOP = (16, 20, 38)
BG_BOT = (4, 5, 10)
FG = (235, 238, 245)
DIM = (110, 118, 138)
ACCENT = (86, 156, 214)

SECTIONS = [
    ("GAMES",  (24, 130, 90),  (10, 52, 36)),
    ("MUSIC",  (142, 84, 168), (54, 30, 66)),
    ("MOVIES", (52, 110, 178), (20, 42, 70)),
]

os.chdir(os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
os.makedirs("assets", exist_ok=True)


def font(path, size):
    return ImageFont.truetype(path, size * SS)


def tracked_text(draw, xy, text, fnt, fill, tracking=0, anchor=None):
    """Draw text with letter-spacing (tracking in supersampled px)."""
    if not tracking:
        draw.text(xy, text, font=fnt, fill=fill, anchor=anchor)
        return
    widths = [draw.textlength(c, font=fnt) for c in text]
    total = sum(widths) + tracking * (len(text) - 1)
    x, y = xy
    if anchor and "m" in anchor[0]:
        x -= total / 2
    for c, w in zip(text, widths):
        draw.text((x, y), c, font=fnt, fill=fill, anchor="l" + (anchor[1] if anchor else "a"))
        x += w + tracking


def background():
    """Vertical gradient + soft glow behind the header + faint vignette."""
    grad = Image.new("RGB", (1, SH))
    for y in range(SH):
        t = y / (SH - 1)
        grad.putpixel((0, y), tuple(
            round(a + (b - a) * t) for a, b in zip(BG_TOP, BG_BOT)))
    img = grad.resize((SW, SH))

    glow = Image.new("RGB", (SW, SH), (0, 0, 0))
    gd = ImageDraw.Draw(glow)
    gd.ellipse((SW * 0.15, -SH * 0.35, SW * 0.85, SH * 0.28),
               fill=(26, 42, 74))
    glow = glow.filter(ImageFilter.GaussianBlur(90 * SS))
    img = Image.blend(img, Image.composite(glow, img, glow.convert("L").point(lambda v: min(255, v * 3))), 0.5)
    return img


def header(draw):
    tracked_text(draw, (SW // 2, 72 * SS), "PERFORMA",
                 font(FONT_BOLD, 40), FG, tracking=10 * SS, anchor="mm")
    tracked_text(draw, (SW // 2, 112 * SS), "ENTERTAINMENT SYSTEM",
                 font(FONT_REG, 15), DIM, tracking=8 * SS, anchor="mm")
    lw = 150 * SS
    draw.rectangle((SW // 2 - lw, 136 * SS, SW // 2 + lw, 138 * SS),
                   fill=(*ACCENT, 255))


def hints(draw, items):
    x = SW // 2
    fnt_k = font(FONT_SYM, 13)
    fnt_v = font(FONT_REG, 13)
    total = 0
    gap = 14 * SS
    widths = []
    for k, v in items:
        wk = draw.textlength(k, font=fnt_k)
        wv = draw.textlength(v, font=fnt_v)
        widths.append((wk, wv))
        total += wk + 8 * SS + wv + gap * 2
    x = (SW - total) / 2
    y = SH - 40 * SS
    for (k, v), (wk, wv) in zip(items, widths):
        x += gap
        draw.text((x, y), k, font=fnt_k, fill=ACCENT, anchor="lm")
        x += wk + 8 * SS
        draw.text((x, y), v, font=fnt_v, fill=DIM, anchor="lm")
        x += wv + gap


def icon_gamepad(d, cx, cy, s, color):
    r = s * 0.42
    d.rounded_rectangle((cx - s, cy - r, cx + s, cy + r),
                        radius=r, fill=color)
    dp = s * 0.52
    t = s * 0.16
    px = cx - s * 0.50
    d.rectangle((px - dp / 2, cy - t, px + dp / 2, cy + t), fill=BG_TOP)
    d.rectangle((px - t, cy - dp / 2, px + t, cy + dp / 2), fill=BG_TOP)
    bx = cx + s * 0.50
    br = s * 0.13
    d.ellipse((bx - s * 0.24 - br, cy + s * 0.08 - br,
               bx - s * 0.24 + br, cy + s * 0.08 + br), fill=BG_TOP)
    d.ellipse((bx + s * 0.24 - br, cy - s * 0.08 - br,
               bx + s * 0.24 + br, cy - s * 0.08 + br), fill=BG_TOP)


def icon_note(d, cx, cy, s, color):
    hr = s * 0.30
    stem = s * 0.14
    top = cy - s * 0.75
    for dx in (-s * 0.42, s * 0.42):
        x = cx + dx
        d.ellipse((x - hr, cy + s * 0.45 - hr, x + hr, cy + s * 0.45 + hr),
                  fill=color)
        d.rectangle((x + hr - stem, top, x + hr, cy + s * 0.45), fill=color)
    d.rectangle((cx - s * 0.42 + hr - stem, top,
                 cx + s * 0.42 + hr, top + s * 0.28), fill=color)


def icon_film(d, cx, cy, s, color):
    d.rounded_rectangle((cx - s, cy - s * 0.72, cx + s, cy + s * 0.72),
                        radius=s * 0.12, fill=color)
    hole = s * 0.11
    for i in range(4):
        y = cy - s * 0.54 + i * (s * 1.08 / 3)
        for x in (cx - s * 0.82, cx + s * 0.82):
            d.ellipse((x - hole, y - hole, x + hole, y + hole), fill=BG_TOP)
    d.rectangle((cx - s * 0.58, cy - s * 0.40, cx + s * 0.58, cy + s * 0.40),
                fill=BG_TOP)
    d.polygon([(cx - s * 0.18, cy - s * 0.26), (cx - s * 0.18, cy + s * 0.26),
               (cx + s * 0.30, cy)], fill=color)


ICONS = [icon_gamepad, icon_note, icon_film]


def draw_card(img, i, hot):
    """One section card composited onto img (supersampled coords)."""
    cw, ch = 236 * SS, 330 * SS
    gap = 44 * SS
    total = 3 * cw + 2 * gap
    x = (SW - total) // 2 + i * (cw + gap)
    y = 236 * SS
    grow = 10 * SS if hot else 0
    x -= grow // 2
    y -= grow
    w, h = cw + grow, ch + grow

    top, bot = SECTIONS[i][1], SECTIONS[i][2]
    if not hot:
        top = tuple(c * 45 // 100 for c in top)
        bot = tuple(c * 45 // 100 for c in bot)

    if hot:
        halo = Image.new("L", img.size, 0)
        hd = ImageDraw.Draw(halo)
        hd.rounded_rectangle((x - 8 * SS, y - 8 * SS, x + w + 8 * SS, y + h + 8 * SS),
                             radius=22 * SS, fill=110)
        halo = halo.filter(ImageFilter.GaussianBlur(16 * SS))
        img.paste(Image.new("RGB", img.size, (170, 200, 255)), (0, 0), halo)

    shadow = Image.new("L", img.size, 0)
    sd = ImageDraw.Draw(shadow)
    sd.rounded_rectangle((x + 4 * SS, y + 10 * SS, x + w + 4 * SS, y + h + 14 * SS),
                         radius=20 * SS, fill=120)
    shadow = shadow.filter(ImageFilter.GaussianBlur(10 * SS))
    img.paste(Image.new("RGB", img.size, (0, 0, 0)), (0, 0), shadow)

    card = Image.new("RGB", (w, h))
    for yy in range(h):
        t = yy / (h - 1)
        card.paste(tuple(round(a + (b - a) * t) for a, b in zip(top, bot)),
                   (0, yy, w, yy + 1))
    mask = Image.new("L", (w, h), 0)
    ImageDraw.Draw(mask).rounded_rectangle((0, 0, w - 1, h - 1),
                                           radius=20 * SS, fill=255)
    img.paste(card, (x, y), mask)

    d = ImageDraw.Draw(img)
    if hot:
        d.rounded_rectangle((x, y, x + w - 1, y + h - 1), radius=20 * SS,
                            outline=(225, 235, 250), width=3 * SS)
    icon_color = FG if hot else tuple(c * 60 // 100 for c in FG)
    ICONS[i](d, x + w // 2, y + h // 2 - 26 * SS, 46 * SS, icon_color)
    d.text((x + w // 2, y + h - 52 * SS), SECTIONS[i][0],
           font=font(FONT_BOLD, 26 if hot else 24),
           fill=FG if hot else DIM, anchor="mm")


def save(img, name):
    img.resize((W, H), Image.LANCZOS).save(f"assets/{name}.bmp")
    print(f"assets/{name}.bmp")


def home_screens():
    for sel in range(3):
        img = background()
        d = ImageDraw.Draw(img)
        header(d)
        for i in range(3):
            if i != sel:
                draw_card(img, i, hot=False)
        draw_card(img, sel, hot=True)   # hot card last so halo overlays neighbors
        d = ImageDraw.Draw(img)
        hints(d, [("← →", "Select"), ("ENTER", "Start"), ("ESC", "Exit")])
        save(img, f"home_{sel}")


def list_screens(games):
    n = len(games)
    avail = (SH - 40 * SS - 30 * SS) - 196 * SS   # keep clear of the hint bar
    row_h = min(46 * SS, avail // max(n, 1))
    list_w = 620 * SS
    x0 = (SW - list_w) // 2
    y0 = 196 * SS
    for sel in range(n):
        img = background()
        d = ImageDraw.Draw(img)
        header(d)
        d.text((x0, 158 * SS), "GAMES", font=font(FONT_BOLD, 20),
               fill=FG, anchor="lm")
        d.rectangle((x0, 176 * SS, x0 + 56 * SS, 179 * SS), fill=ACCENT)
        for i, g in enumerate(games):
            y = y0 + i * row_h
            if i == sel:
                d.rounded_rectangle(
                    (x0 - 18 * SS, y, x0 + list_w + 18 * SS, y + row_h - 8 * SS),
                    radius=8 * SS, fill=(36, 62, 96))
                d.rectangle((x0 - 18 * SS, y, x0 - 12 * SS, y + row_h - 8 * SS),
                            fill=ACCENT)
            d.text((x0, y + (row_h - 8 * SS) // 2), g["name"],
                   font=font(FONT_BOLD if i == sel else FONT_REG, 17),
                   fill=FG if i == sel else DIM, anchor="lm")
        hints(d, [("↑ ↓", "Select"), ("ENTER", "Play"),
                  ("ESC", "Back")])
        save(img, f"list_{sel}")


def launch_screen():
    img = background()
    d = ImageDraw.Draw(img)
    header(d)
    d.text((SW // 2, SH // 2), "STARTING…", font=font(FONT_BOLD, 30),
           fill=FG, anchor="mm")
    save(img, "launch")


def games_cfg(cfg):
    """Emit the file the C side parses: key|exe|args|cwd (cwd defaults to exe dir)."""
    def cwd_of(e):
        return e.get("cwd") or e["exe"].rsplit("\\", 1)[0]
    lines = []
    lines.append(f"music|{cfg['music']['exe']}|{cfg['music'].get('args', '')}|{cwd_of(cfg['music'])}")
    lines.append(f"movies|{cfg['movies']['exe']}|{cfg['movies'].get('args', '')}|{cwd_of(cfg['movies'])}")
    for g in cfg["games"]:
        lines.append(f"game|{g['exe']}|{g.get('args', '')}|{cwd_of(g)}")
    with open("assets/games.cfg", "w", newline="\r\n") as f:
        f.write("\n".join(lines) + "\n")
    print(f"assets/games.cfg ({len(cfg['games'])} games)")


with open("games.json") as f:
    config = json.load(f)

home_screens()
list_screens(config["games"])
launch_screen()
games_cfg(config)
