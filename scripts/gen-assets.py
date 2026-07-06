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
import colorsys
import hashlib
import json
import os
import re
import textwrap
from PIL import Image, ImageDraw, ImageFilter, ImageFont, ImageOps

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

# Now-Playing progress bar geometry, in LOGICAL 1024x768 space (the C side
# renders with SDL_RenderSetLogicalSize(1024,768) and draws the live fill at
# these exact coords over the pre-rendered track). Keep the two in sync.
PROG = {"x": 150, "y": 636, "w": 724, "h": 10}

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


# --------------------------------------------------------------------------
# Cover art. Real box art lives in covers/<slug>.{jpg,png} (drop one in to
# override); when absent we draw a clean procedural card so every game still
# looks intentional. Either way the hero panel crops-to-fill and overlays a
# scrim + the title, so the two paths look consistent.
COVERS_DIR = "covers"


def slug_of(name):
    return re.sub(r"[^a-z0-9]+", "-", name.lower()).strip("-")


def game_colors(name):
    """Deterministic per-game hue → (top, bottom, accent) RGB tuples."""
    hue = (int(hashlib.md5(name.encode()).hexdigest(), 16) % 360) / 360.0

    def rgb(h, s, v):
        return tuple(round(c * 255) for c in colorsys.hsv_to_rgb(h, s, v))
    return rgb(hue, 0.45, 0.40), rgb(hue, 0.62, 0.12), rgb(hue, 0.55, 0.90)


def procedural_cover(name, w, h):
    """Designed 'box art' fallback: hue gradient + diagonal sheen + a soft
    spotlight behind a shadowed monogram + a corner accent. Reads as an
    intentional cover, not a placeholder."""
    top, bot, accent = game_colors(name)
    card = Image.new("RGB", (1, h))
    for y in range(h):
        t = y / (h - 1)
        card.putpixel((0, y), tuple(round(a + (b - a) * t)
                                    for a, b in zip(top, bot)))
    card = card.resize((w, h))

    # diagonal sheen: a faint brighter band across the upper third
    sheen = Image.new("L", (w, h), 0)
    ImageDraw.Draw(sheen).polygon(
        [(0, int(h * 0.10)), (w, int(-h * 0.10)),
         (w, int(h * 0.18)), (0, int(h * 0.38))], fill=42)
    card.paste(Image.new("RGB", (w, h), (255, 255, 255)), (0, 0),
               sheen.filter(ImageFilter.GaussianBlur(max(2, w // 40))))

    # soft spotlight behind the monogram
    spot = Image.new("L", (w, h), 0)
    cx, cy, rr = w // 2, int(h * 0.44), int(w * 0.34)
    ImageDraw.Draw(spot).ellipse((cx - rr, cy - rr, cx + rr, cy + rr),
                                 fill=70)
    card.paste(Image.new("RGB", (w, h),
               tuple(min(255, c + 40) for c in accent)), (0, 0),
               spot.filter(ImageFilter.GaussianBlur(max(4, w // 12))))

    # corner accent wedge
    d = ImageDraw.Draw(card)
    d.polygon([(0, 0), (int(w * 0.28), 0), (0, int(h * 0.20))], fill=accent)

    # monogram with a soft drop shadow
    mono = name[0].upper()
    f = font(FONT_BOLD, int(150 * w / (236 * SS)) if w < 236 * SS else 150)
    shadow = Image.new("L", (w, h), 0)
    ImageDraw.Draw(shadow).text((cx + 4, cy + 6), mono, font=f, fill=180,
                                anchor="mm")
    card.paste(Image.new("RGB", (w, h), (0, 0, 0)), (0, 0),
               shadow.filter(ImageFilter.GaussianBlur(6)))
    d.text((cx, cy), mono, font=f,
           fill=tuple(min(255, c + 60) for c in accent), anchor="mm")
    return card


def cover_image(game, w, h):
    """Real art (cropped-to-fill) if present, else a procedural card."""
    slug = game.get("slug") or slug_of(game["name"])
    for ext in ("jpg", "jpeg", "png"):
        p = os.path.join(COVERS_DIR, f"{slug}.{ext}")
        if os.path.exists(p):
            im = Image.open(p).convert("RGB")
            return ImageOps.fit(im, (w, h), Image.LANCZOS)
    return procedural_cover(game["name"], w, h)


def rounded_mask(w, h, r):
    m = Image.new("L", (w, h), 0)
    ImageDraw.Draw(m).rounded_rectangle((0, 0, w - 1, h - 1), radius=r, fill=255)
    return m


def hero_panel(img, game, x, y, w, h, tag="GAME"):
    """Big featured-art panel for the selected item: art, bottom scrim,
    title, accent frame. Composited onto img at supersampled coords."""
    _, _, accent = game_colors(game["name"])
    r = 22 * SS

    # drop shadow
    shadow = Image.new("L", img.size, 0)
    ImageDraw.Draw(shadow).rounded_rectangle(
        (x + 6 * SS, y + 12 * SS, x + w + 6 * SS, y + h + 14 * SS),
        radius=r, fill=130)
    img.paste(Image.new("RGB", img.size, (0, 0, 0)), (0, 0),
              shadow.filter(ImageFilter.GaussianBlur(12 * SS)))

    art = cover_image(game, w, h)
    mask = rounded_mask(w, h, r)

    # bottom scrim so the title reads over any art
    scrim = Image.new("L", (w, h), 0)
    sd = ImageDraw.Draw(scrim)
    for yy in range(h):
        t = (yy / (h - 1) - 0.45) / 0.55
        sd.line((0, yy, w, yy), fill=max(0, min(230, int(230 * t))))
    art.paste(Image.new("RGB", (w, h), (0, 0, 0)), (0, 0), scrim)

    img.paste(art, (x, y), mask)

    d = ImageDraw.Draw(img)
    # title, wrapped, sitting on the scrim near the bottom
    tf = font(FONT_BOLD, 30)
    lines = textwrap.wrap(game["name"], width=15) or [game["name"]]
    lh = (tf.getbbox("Ag")[3] - tf.getbbox("Ag")[1]) + 6 * SS
    ty = y + h - 34 * SS - len(lines) * lh
    for ln in lines:
        d.text((x + 34 * SS, ty), ln, font=tf, fill=FG)
        ty += lh
    # a small accent tag above the title
    d.text((x + 36 * SS, y + h - 34 * SS - len(lines) * lh - 26 * SS),
           tag, font=font(FONT_BOLD, 13), fill=accent)
    # accent frame
    d.rounded_rectangle((x, y, x + w - 1, y + h - 1), radius=r,
                        outline=accent, width=2 * SS)


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


def list_screens(games, section="GAMES", prefix="list", tag="GAME"):
    """Two-column dashboard: scrolling list on the left, big featured cover
    of the selected item on the right. One BMP per selection (no engine
    change). The list window scrolls to keep the selection visible."""
    n = len(games)
    if n == 0:
        return
    # left list column
    x0 = 60 * SS
    list_w = 430 * SS
    row_h = 44 * SS
    top_y = 214 * SS
    bot_y = SH - 78 * SS               # clear of the hint bar
    visible = max(1, (bot_y - top_y) // row_h)
    # right hero panel (mirror the list column width for a balanced split)
    hx = x0 + list_w + 44 * SS
    hy = 196 * SS
    hw = SW - hx - 60 * SS
    hh = bot_y - hy

    for sel in range(n):
        img = background()
        d = ImageDraw.Draw(img)
        header(d)
        _, _, accent = game_colors(games[sel]["name"])

        d.text((x0, 158 * SS), section, font=font(FONT_BOLD, 20),
               fill=FG, anchor="lm")
        d.rectangle((x0, 176 * SS, x0 + 56 * SS, 179 * SS), fill=accent)
        d.text((x0 + list_w, 162 * SS), f"{sel + 1}/{n}",
               font=font(FONT_REG, 14), fill=DIM, anchor="rm")

        # scroll so the selection stays in view
        first = 0
        if n > visible:
            first = min(max(0, sel - visible // 2), n - visible)
        for row in range(first, min(n, first + visible)):
            g = games[row]
            y = top_y + (row - first) * row_h
            if row == sel:
                d.rounded_rectangle(
                    (x0 - 18 * SS, y, x0 + list_w + 18 * SS, y + row_h - 8 * SS),
                    radius=8 * SS, fill=tuple(c * 40 // 100 for c in accent))
                d.rectangle((x0 - 18 * SS, y, x0 - 12 * SS, y + row_h - 8 * SS),
                            fill=accent)
            d.text((x0, y + (row_h - 8 * SS) // 2), g["name"],
                   font=font(FONT_BOLD if row == sel else FONT_REG, 17),
                   fill=FG if row == sel else DIM, anchor="lm")

        hero_panel(img, games[sel], hx, hy, hw, hh, tag=tag)
        hints(d, [("↑ ↓", "Select"), ("ENTER", "Play"), ("ESC", "Back")])
        save(img, f"{prefix}_{sel}")


def nowplaying_screens(songs):
    """One full-screen Now-Playing state per song: large album art, title,
    NOW PLAYING tag, an empty progress-bar track (the C side fills it live),
    and transport hints. prefix 'np'."""
    n = len(songs)
    for sel in range(n):
        s = songs[sel]
        img = background()
        d = ImageDraw.Draw(img)
        header(d)
        _, _, accent = game_colors(s["name"])

        # large album art, centered upper area
        art_w = art_h = 300 * SS
        ax = (SW - art_w) // 2
        ay = 190 * SS
        r = 20 * SS
        shadow = Image.new("L", img.size, 0)
        ImageDraw.Draw(shadow).rounded_rectangle(
            (ax + 6 * SS, ay + 12 * SS, ax + art_w + 6 * SS, ay + art_h + 14 * SS),
            radius=r, fill=140)
        img.paste(Image.new("RGB", img.size, (0, 0, 0)), (0, 0),
                  shadow.filter(ImageFilter.GaussianBlur(14 * SS)))
        art = cover_image(s, art_w, art_h)
        img.paste(art, (ax, ay), rounded_mask(art_w, art_h, r))
        d = ImageDraw.Draw(img)
        d.rounded_rectangle((ax, ay, ax + art_w - 1, ay + art_h - 1),
                            radius=r, outline=accent, width=2 * SS)

        # NOW PLAYING tag + title, centered under the art
        ty = ay + art_h + 30 * SS
        d.text((SW // 2, ty), "NOW PLAYING", font=font(FONT_BOLD, 13),
               fill=accent, anchor="mm")
        d.text((SW // 2, ty + 34 * SS), s["name"], font=font(FONT_BOLD, 26),
               fill=FG, anchor="mm")

        # empty progress-bar track (C fills the accent portion live)
        px, py = PROG["x"] * SS, PROG["y"] * SS
        pw, ph = PROG["w"] * SS, PROG["h"] * SS
        d.rounded_rectangle((px, py, px + pw, py + ph), radius=ph // 2,
                            fill=(48, 54, 72))

        hints(d, [("◀ ▶", "Prev / Next"), ("ENTER", "Play / Pause"),
                  ("ESC", "Back")])
        save(img, f"np_{sel}")


def attract_screens(games):
    """Arcade attract loop: one full-bleed art frame per game with a scrim,
    title, and PRESS START. The C side cycles these while the box is idle.
    prefix 'attract'."""
    for i, g in enumerate(games):
        _, _, accent = game_colors(g["name"])
        img = cover_image(g, SW, SH)          # crop-to-fill the whole screen
        # darken for legibility: global dim + stronger bottom scrim
        img = Image.blend(img, Image.new("RGB", img.size, (6, 8, 16)), 0.35)
        scrim = Image.new("L", (SW, SH), 0)
        sd = ImageDraw.Draw(scrim)
        for yy in range(SH):
            t = (yy / (SH - 1) - 0.5) / 0.5
            sd.line((0, yy, SW, yy), fill=max(0, min(200, int(200 * t))))
        img.paste(Image.new("RGB", (SW, SH), (0, 0, 0)), (0, 0), scrim)

        d = ImageDraw.Draw(img)
        tracked_text(d, (SW // 2, 70 * SS), "PERFORMA ENTERTAINMENT SYSTEM",
                     font(FONT_REG, 15), DIM, tracking=6 * SS, anchor="mm")
        d.text((SW // 2, SH - 150 * SS), g["name"], font=font(FONT_BOLD, 40),
               fill=FG, anchor="mm")
        tracked_text(d, (SW // 2, SH - 90 * SS), "PRESS  START",
                     font(FONT_BOLD, 22), accent, tracking=6 * SS, anchor="mm")
        save(img, f"attract_{i}")


def launch_screen():
    img = background()
    d = ImageDraw.Draw(img)
    header(d)
    d.text((SW // 2, SH // 2), "STARTING…", font=font(FONT_BOLD, 30),
           fill=FG, anchor="mm")
    save(img, "launch")


# --------------------------------------------------------------------------
# Media (movies/music). Scanned from the XP share at gen time (we're on the
# laptop with it mounted); paths are rewritten to the box's C:\ view. Each
# item becomes a launch of the section player with the file as its argument.
MEDIA_LOCAL = "/media/Acer_Notebook/media"
MEDIA_BOX = "C:\\XP_Share\\media"
VIDEO_EXT = {".mp4", ".avi", ".mkv", ".wmv", ".mpg", ".mpeg", ".mov", ".m4v"}
AUDIO_EXT = {".mp3", ".wav", ".flac", ".ogg", ".m4a", ".wma"}


def scan_media(sub, exts):
    d = os.path.join(MEDIA_LOCAL, sub)
    if not os.path.isdir(d):
        return []
    out = []
    for f in sorted(os.listdir(d)):
        stem, ext = os.path.splitext(f)
        if ext.lower() in exts:
            out.append({"name": stem, "box": f"{MEDIA_BOX}\\{sub}\\{f}"})
    return out


def games_cfg(cfg, movies, songs):
    """Emit the file the C side parses: key|exe|args|cwd.
    Games launch directly; movie/song lines launch the section player with
    the media file (quoted) as the argument."""
    def cwd_of(e):
        return e.get("cwd") or e["exe"].rsplit("\\", 1)[0]
    lines = []
    # home-tile default players (fallback if a section has no scanned media)
    lines.append(f"music|{cfg['music']['exe']}|{cfg['music'].get('args', '')}|{cwd_of(cfg['music'])}")
    lines.append(f"movies|{cfg['movies']['exe']}|{cfg['movies'].get('args', '')}|{cwd_of(cfg['movies'])}")
    for g in cfg["games"]:
        lines.append(f"game|{g['exe']}|{g.get('args', '')}|{cwd_of(g)}")
    mplayer, mcwd = cfg["movies"]["exe"], cwd_of(cfg["movies"])
    for m in movies:
        lines.append(f'movie|{mplayer}|"{m["box"]}"|{mcwd}')
    splayer, scwd = cfg["music"]["exe"], cwd_of(cfg["music"])
    for s in songs:
        lines.append(f'song|{splayer}|"{s["box"]}"|{scwd}')
    with open("assets/games.cfg", "w", newline="\r\n") as f:
        f.write("\n".join(lines) + "\n")
    print(f"assets/games.cfg ({len(cfg['games'])} games, "
          f"{len(movies)} movies, {len(songs)} songs)")


with open("games.json") as f:
    config = json.load(f)

movies = scan_media("movies", VIDEO_EXT)
songs = scan_media("music", AUDIO_EXT)

home_screens()
list_screens(config["games"])
attract_screens(config["games"])
if movies:
    list_screens(movies, section="MOVIES", prefix="movie", tag="MOVIE")
if songs:
    list_screens(songs, section="MUSIC", prefix="song", tag="SONG")
    nowplaying_screens(songs)
launch_screen()
games_cfg(config, movies, songs)
