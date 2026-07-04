#!/usr/bin/env python3
"""Generate src/launcher.ico — the green card + gamepad pill from the home
screen, so the desktop shortcut on XP looks like the launcher."""
import os
from PIL import Image, ImageDraw

S = 256

os.chdir(os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))

img = Image.new("RGBA", (S, S), (0, 0, 0, 0))
d = ImageDraw.Draw(img)
d.rounded_rectangle((8, 8, S - 8, S - 8), radius=44, fill=(24, 130, 90))

cx, cy = S // 2, S // 2
s = 74
r = int(s * 0.42)
d.rounded_rectangle((cx - s, cy - r, cx + s, cy + r), radius=r,
                    fill=(235, 238, 245))
dp, t = s * 0.5, s * 0.16
px = cx - s * 0.48
d.rectangle((px - dp / 2, cy - t, px + dp / 2, cy + t), fill=(24, 130, 90))
d.rectangle((px - t, cy - dp / 2, px + t, cy + dp / 2), fill=(24, 130, 90))
bx, br = cx + s * 0.48, s * 0.13
d.ellipse((bx - s * 0.22 - br, cy - br, bx - s * 0.22 + br, cy + br),
          fill=(24, 130, 90))
d.ellipse((bx + s * 0.22 - br, cy - br, bx + s * 0.22 + br, cy + br),
          fill=(24, 130, 90))

img.save("src/launcher.ico", sizes=[(16, 16), (24, 24), (32, 32), (48, 48)])
print("src/launcher.ico")
