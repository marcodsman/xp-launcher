#!/usr/bin/env python3
"""Synthesize sample music for the launcher's Music section (pure stdlib).

The box had no music at all; this makes a few pleasant, royalty-free demo
tracks (we generate them, so there's nothing to license). 16-bit mono WAV,
which foobar2000 plays natively. Writes straight to the box's music folder.

Usage: gen-music.py [output_dir]   (default: the XP share's media/music)
"""
import math
import os
import struct
import sys
import wave

SR = 22050
OUT = sys.argv[1] if len(sys.argv) > 1 else "/media/Acer_Notebook/media/music"
os.makedirs(OUT, exist_ok=True)


def midi(n):
    return 440.0 * 2 ** ((n - 69) / 12.0)


def render(buf, start, freq, dur, amp=0.2, wave_kind="square", a=0.01, d=0.1,
           s=0.7, r=0.08):
    """Add one ADSR note to the float buffer at time `start` (seconds)."""
    n = int(dur * SR)
    i0 = int(start * SR)
    rel = int(r * SR)
    for i in range(n + rel):
        idx = i0 + i
        if idx >= len(buf):
            break
        t = i / SR
        ph = freq * t
        if wave_kind == "square":
            v = 1.0 if (ph % 1.0) < 0.5 else -1.0
        elif wave_kind == "tri":
            f = ph % 1.0
            v = 4 * abs(f - 0.5) - 1
        elif wave_kind == "saw":
            v = 2 * (ph % 1.0) - 1
        else:  # sine
            v = math.sin(2 * math.pi * ph)
        # ADSR
        tt = i / SR
        if i >= n:                       # release
            e = s * math.exp(-(i - n) / max(1, rel) * 4)
        elif tt < a:
            e = tt / a
        elif tt < a + d:
            e = 1 - (1 - s) * (tt - a) / d
        else:
            e = s
        buf[idx] += amp * e * v


def save(name, buf):
    peak = max(1e-6, max(abs(x) for x in buf))
    norm = min(1.0, 0.85 / peak)
    frames = bytearray()
    for x in buf:
        frames += struct.pack("<h", int(max(-1, min(1, x * norm)) * 32767))
    path = os.path.join(OUT, name)
    with wave.open(path, "wb") as w:
        w.setnchannels(1)
        w.setsampwidth(2)
        w.setframerate(SR)
        w.writeframes(bytes(frames))
    print(f"{path}  ({len(buf)/SR:.0f}s)")


# --- Track 1: upbeat chiptune -------------------------------------------
def chiptune():
    bpm = 132
    beat = 60.0 / bpm
    length = beat * 64
    buf = [0.0] * int(length * SR)
    # C major pentatonic-ish lead phrase (MIDI notes), 16 steps, looped
    lead = [72, 76, 79, 76, 74, 77, 72, 0, 71, 74, 79, 77, 76, 72, 74, 0]
    bass = [48, 48, 55, 55, 53, 53, 43, 43]
    for loop in range(4):
        base = loop * 16 * (beat / 2)
        for i, n in enumerate(lead):
            if n:
                render(buf, base + i * (beat / 2), midi(n), beat / 2 * 0.9,
                       amp=0.18, wave_kind="square", d=0.05, s=0.6)
        for i, n in enumerate(bass):
            render(buf, base + i * beat, midi(n), beat * 0.95,
                   amp=0.22, wave_kind="tri", d=0.08, s=0.8)
    return buf


# --- Track 2: calm sine pad ---------------------------------------------
def ambient_track():
    length = 44.0
    buf = [0.0] * int(length * SR)
    chords = [[57, 60, 64], [53, 57, 60], [55, 59, 62], [50, 53, 57]]
    step = length / len(chords)
    for i, ch in enumerate(chords):
        for n in ch:
            render(buf, i * step, midi(n), step * 1.05, amp=0.14,
                   wave_kind="sine", a=0.8, d=0.5, s=0.85, r=0.8)
            render(buf, i * step, midi(n + 12), step * 1.05, amp=0.05,
                   wave_kind="sine", a=1.0, d=0.5, s=0.7, r=0.8)
    return buf


# --- Track 3: bass groove + arp -----------------------------------------
def groove():
    bpm = 108
    beat = 60.0 / bpm
    length = beat * 48
    buf = [0.0] * int(length * SR)
    bassline = [40, 40, 47, 45, 40, 40, 43, 45]
    arp = [64, 67, 71, 67]
    for loop in range(6):
        base = loop * 8 * beat
        for i, n in enumerate(bassline):
            render(buf, base + i * beat, midi(n), beat * 0.5, amp=0.24,
                   wave_kind="saw", d=0.06, s=0.5)
        for i in range(16):
            n = arp[i % len(arp)]
            render(buf, base + i * (beat / 2), midi(n), beat / 2 * 0.6,
                   amp=0.10, wave_kind="square", d=0.03, s=0.4)
    return buf


save("Chiptune Menu.wav", chiptune())
save("Ambient Drift.wav", ambient_track())
save("Bassline Groove.wav", groove())
