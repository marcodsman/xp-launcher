#!/usr/bin/env python3
"""Best-effort real box-art fetcher for the launcher (run on the laptop).

Only games with a *confident* source are listed — Steam's CDN for titles
that are actually on Steam (the old Mortal Kombats / Jazz2 / Heart of
Darkness aren't, so fuzzy-searching them would grab the wrong game). Saves
portrait art to covers/<slug>.jpg; anything not fetched falls back to the
designed procedural cover automatically. Re-runnable; skips existing files.
"""
import json
import os
import subprocess
import urllib.parse

os.chdir(os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
os.makedirs("covers", exist_ok=True)

UA = "Mozilla/5.0"


def curl(url, out=None):
    cmd = ["curl", "-sSL", "--max-time", "30", "-A", UA]
    if out:
        cmd += ["-o", out]
    cmd.append(url)
    r = subprocess.run(cmd, capture_output=not out, text=True)
    return r.stdout if not out else (r.returncode == 0)


def steam_appid(query):
    """First (most-relevant) Steam app for a query, or None."""
    u = "https://steamcommunity.com/actions/SearchApps/" + urllib.parse.quote(query)
    try:
        data = json.loads(curl(u))
        return (data[0]["appid"], data[0]["name"]) if data else None
    except Exception:
        return None


def is_jpeg(path):
    try:
        with open(path, "rb") as f:
            return f.read(2) == b"\xff\xd8"
    except OSError:
        return False


def fetch_steam_cover(appid, slug):
    """Try the portrait library capsule, then the landscape header."""
    for asset in ("library_600x900.jpg", "header.jpg"):
        url = f"https://cdn.cloudflare.steamstatic.com/steam/apps/{appid}/{asset}"
        out = f"covers/{slug}.jpg"
        if curl(url, out) and is_jpeg(out) and os.path.getsize(out) > 4000:
            return asset
        if os.path.exists(out):
            os.remove(out)
    return None


# slug (matches gen-assets slug_of) -> Steam search query
STEAM = {
    "quake-2": "Quake II",
    "grim-fandango": "Grim Fandango",
    "abe-s-oddysee": "Oddworld Abe's Oddysee",
    # Legacy of Kain: Defiance isn't on Steam (search returns the wrong LoK
    # game) — sourced from GOG below, which has the actual 2003 original.
}

for slug, query in STEAM.items():
    if os.path.exists(f"covers/{slug}.jpg"):
        print(f"{slug}: already have it")
        continue
    hit = steam_appid(query)
    if not hit:
        print(f"{slug}: no Steam match for '{query}'")
        continue
    appid, name = hit
    asset = fetch_steam_cover(appid, slug)
    print(f"{slug}: {name} (appid {appid}) -> "
          f"{asset if asset else 'NO IMAGE'}")


def gog_cover(query, guard, reject=()):
    """GOG's vertical cover for the first result whose title contains
    `guard` and none of `reject` (all case-insensitive). GOG sells the
    box's actual versions, so this is the exact matching art."""
    u = ("https://catalog.gog.com/v1/catalog?limit=8&query="
         + urllib.parse.quote("like:" + query))
    try:
        data = json.loads(curl(u))
    except Exception:
        return None
    for p in data.get("products", []):
        t = p.get("title", "").lower()
        if guard in t and not any(r in t for r in reject):
            url = p.get("coverVertical") or p.get("coverHorizontal")
            if url:
                return p["title"], url
    return None


# slug -> (query, must-contain guard, reject substrings)
GOG = {
    "jazz-jackrabbit-2": ("Jazz Jackrabbit 2", "jazz jackrabbit 2", ()),
    "mortal-kombat": ("Mortal Kombat", "1+2+3", ()),
    "mortal-kombat-4": ("Mortal Kombat 4", "mortal kombat 4", ()),
    "mortal-kombat-trilogy": ("Mortal Kombat Trilogy", "mortal kombat trilogy", ()),
    "the-heart-of-darkness": ("Heart of Darkness", "the heart of darkness",
                              ("unrated", "legacy of kain", "hearts of iron",
                               "victoria")),
    "legacy-of-kain-defiance": ("Legacy of Kain Defiance", "defiance (2003)", ()),
    "opentyrian": ("Tyrian 2000", "tyrian", ()),   # OpenTyrian plays Tyrian
}

for slug, (query, guard, reject) in GOG.items():
    if os.path.exists(f"covers/{slug}.jpg"):
        print(f"{slug}: already have it")
        continue
    hit = gog_cover(query, guard, reject)
    if not hit:
        print(f"{slug}: no GOG match for '{query}'")
        continue
    title, url = hit
    ok = curl(url, f"covers/{slug}.jpg")
    good = ok and is_jpeg(f"covers/{slug}.jpg") and os.path.getsize(f"covers/{slug}.jpg") > 4000
    print(f"{slug}: {title} -> {'ok' if good else 'NO IMAGE'}")
    if not good and os.path.exists(f"covers/{slug}.jpg"):
        os.remove(f"covers/{slug}.jpg")

# MK 1+2+3 is one GOG product; reuse its cover for the II and 3 entries.
import shutil
if os.path.exists("covers/mortal-kombat.jpg"):
    for s in ("mortal-kombat-ii", "mortal-kombat-3"):
        if not os.path.exists(f"covers/{s}.jpg"):
            shutil.copy("covers/mortal-kombat.jpg", f"covers/{s}.jpg")
            print(f"{s}: reused MK1+2+3 cover")

print("\ncovers/:", sorted(f for f in os.listdir("covers") if f.endswith(".jpg")))
