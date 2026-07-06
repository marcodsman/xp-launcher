#!/usr/bin/env python3
"""Synthesize the launcher's UI sounds (pure stdlib, runs on the laptop).

Tasteful, short, console-style blips + a quiet ambient pad loop. Output is
16-bit mono WAV at 22050 Hz (what SDL2_mixer opens on the box). Kept small
and subtle on purpose — UI sound should be felt, not noticed.

Outputs into assets/snd/:  move, select, back, launch, ambient  (.wav)
"""
import math
import os
import struct
import wave

SR = 22050
os.chdir(os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
os.makedirs("assets/snd", exist_ok=True)


def write_wav(name, samples):
    # clamp + convert to int16
    frames = bytearray()
    for s in samples:
        v = max(-1.0, min(1.0, s))
        frames += struct.pack("<h", int(v * 32767))
    with wave.open(f"assets/snd/{name}.wav", "wb") as w:
        w.setnchannels(1)
        w.setsampwidth(2)
        w.setframerate(SR)
        w.writeframes(bytes(frames))
    print(f"assets/snd/{name}.wav  ({len(samples)/SR:.2f}s)")


def env(n, i, attack=0.01, release=0.5):
    """Attack/exp-decay envelope, 0..1, position i of n samples."""
    t = i / SR
    dur = n / SR
    a = min(1.0, t / attack) if attack > 0 else 1.0
    # exponential decay over the release fraction of the tail
    r = math.exp(-(t / (dur * release)))
    return a * r


def tone(freq, dur, amp=0.3, attack=0.005, release=0.45, harm=0.0):
    n = int(dur * SR)
    out = []
    for i in range(n):
        t = i / SR
        s = math.sin(2 * math.pi * freq * t)
        if harm:
            s += harm * math.sin(2 * math.pi * freq * 2 * t)
        out.append(amp * env(n, i, attack, release) * s / (1 + harm))
    return out


def seq(*chunks):
    """Concatenate sample chunks."""
    out = []
    for c in chunks:
        out += c
    return out


def mix(a, b):
    """Overlay two chunks (sum, pad shorter)."""
    n = max(len(a), len(b))
    return [(a[i] if i < len(a) else 0) + (b[i] if i < len(b) else 0)
            for i in range(n)]


def silence(dur):
    return [0.0] * int(dur * SR)


# --- navigation move: soft, quick tick ------------------------------------
write_wav("move", tone(1180, 0.045, amp=0.22, attack=0.002, release=0.30,
                       harm=0.25))

# --- select / confirm: two rising notes -----------------------------------
write_wav("select", seq(
    tone(784, 0.055, amp=0.26, release=0.5),
    tone(1047, 0.075, amp=0.26, release=0.4, harm=0.15)))

# --- back / cancel: two falling notes, softer -----------------------------
write_wav("back", seq(
    tone(587, 0.05, amp=0.22, release=0.5),
    tone(440, 0.07, amp=0.22, release=0.4)))

# --- launch: rising sweep + a soft power chord swell ----------------------
def launch_sound():
    n = int(0.42 * SR)
    sweep = []
    for i in range(n):
        t = i / SR
        f = 300 + (1300 - 300) * (t / 0.42) ** 1.5
        a = min(1.0, t / 0.05) * math.exp(-(t / 0.30))
        sweep.append(0.18 * a * math.sin(2 * math.pi * f * t))
    # chord swell (root, fifth, octave) fading in under the sweep
    chord = []
    for i in range(n):
        t = i / SR
        a = min(1.0, t / 0.18) * math.exp(-(t / 0.5))
        s = (math.sin(2 * math.pi * 261.6 * t)
             + math.sin(2 * math.pi * 392.0 * t)
             + 0.6 * math.sin(2 * math.pi * 523.3 * t))
        chord.append(0.10 * a * s / 2.6)
    return mix(sweep, chord)


write_wav("launch", launch_sound())


# --- ambient pad loop: slow, quiet, seamless ------------------------------
def ambient(dur=16.0):
    n = int(dur * SR)
    # A minor-ish pad: partials whose cycles fit the loop for seamlessness
    base = [110.0, 164.81, 220.0, 329.63]     # A2 E3 A3 E4
    detune = [1.0, 1.003, 0.997, 1.005]
    out = []
    for i in range(n):
        t = i / SR
        # slow amplitude LFO, full cycle over the loop -> seamless
        lfo = 0.5 + 0.5 * math.sin(2 * math.pi * (1.0 / dur) * t)
        s = 0.0
        for f, d in zip(base, detune):
            s += math.sin(2 * math.pi * f * d * t)
            s += 0.4 * math.sin(2 * math.pi * f * d * 2.001 * t)
        out.append(0.07 * (0.4 + 0.6 * lfo) * s / len(base))
    # short equal-power crossfade of tail into head for a clean seam
    xf = int(0.4 * SR)
    for i in range(xf):
        a = i / xf
        out[i] = out[i] * a + out[n - xf + i] * (1 - a)
    return out[:n - xf]


write_wav("ambient", ambient())
