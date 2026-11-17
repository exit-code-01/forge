#!/usr/bin/env python3
"""Generates the Spillway's own sounds (Location 3, ADR-034) - own work, no samples:
  assets/sounds/water_loop.wav     4.0 s seamless loop: low-passed noise with slow swells (water running)
  assets/sounds/splash.wav         0.6 s: a noise burst that darkens, plus two bubble blips
  assets/sounds/turbine_start.wav  3.2 s: a rising turbine spool-up that settles into a hum
Deterministic, stdlib only. Regenerate with:  python tools/gen_spillway_sounds.py
"""

import math
import os
import random
import struct
import wave

RATE = 22050


def write(name, samples):
    out = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "assets", "sounds", name)
    os.makedirs(os.path.dirname(out), exist_ok=True)
    with wave.open(out, "wb") as w:
        w.setnchannels(1)
        w.setsampwidth(2)
        w.setframerate(RATE)
        w.writeframes(b"".join(struct.pack("<h", max(-32767, min(32767, int(32767 * s)))) for s in samples))
    print("wrote %s (%d samples)" % (os.path.normpath(out), len(samples)))


def lowpass_loop(x, fc):
    """One-pole low-pass applied circularly (two passes, keep the second): seamless at the wrap."""
    a = math.exp(-2.0 * math.pi * fc / RATE)
    y = [0.0] * len(x)
    s = 0.0
    for _ in range(2):
        for i, v in enumerate(x):
            s = a * s + (1.0 - a) * v
            y[i] = s
    return y


def water_loop():
    rng = random.Random(7)
    n = int(RATE * 4.0)
    base = [rng.uniform(-1.0, 1.0) for _ in range(n)]
    body = lowpass_loop(base, 1400.0)
    rumble = lowpass_loop([rng.uniform(-1.0, 1.0) for _ in range(n)], 260.0)
    hiss = [b - l for b, l in zip(base, lowpass_loop(base, 3800.0))]
    out = []
    for i in range(n):
        t = i / RATE
        # whole numbers of cycles over 4 s -> the swells loop cleanly
        swell = 0.78 + 0.14 * math.sin(2 * math.pi * 0.25 * t) + 0.08 * math.sin(2 * math.pi * 0.75 * t + 1.3)
        out.append(swell * (2.6 * body[i] + 2.2 * rumble[i] + 0.35 * hiss[i]))
    peak = max(abs(v) for v in out)
    return [0.34 * v / peak for v in out]


def splash():
    rng = random.Random(11)
    n = int(RATE * 0.6)
    out = []
    s = 0.0
    for i in range(n):
        t = i / RATE
        fc = 5200.0 * math.exp(-t * 7.0) + 500.0
        a = math.exp(-2.0 * math.pi * fc / RATE)
        s = a * s + (1.0 - a) * rng.uniform(-1.0, 1.0)
        env = min(t / 0.008, 1.0) * math.exp(-t * 5.5)
        v = 3.2 * s * env
        for t0, f0 in ((0.07, 520.0), (0.16, 760.0)):  # two bubble blips
            u = t - t0
            if u > 0:
                v += 0.18 * math.exp(-u * 38.0) * math.sin(2 * math.pi * (f0 + 900.0 * u) * u)
        out.append(v)
    peak = max(abs(v) for v in out)
    return [0.7 * v / peak for v in out]


def turbine_start():
    n = int(RATE * 3.2)
    out = []
    ph = 0.0
    for i in range(n):
        t = i / RATE
        f = 28.0 + 62.0 * (1.0 - math.exp(-t * 1.1))
        ph += 2 * math.pi * f / RATE
        env = min(t / 0.6, 1.0) * min((3.2 - t) / 0.5, 1.0)
        v = 0.55 * math.sin(ph) + 0.28 * math.sin(2 * ph) + 0.16 * math.sin(3 * ph + 0.5) + 0.07 * math.sin(5 * ph)
        v += 0.05 * math.sin(2 * math.pi * 7.0 * t) * math.sin(ph * 4)  # blade flutter
        out.append(0.55 * env * v)
    return out


def main():
    write("water_loop.wav", water_loop())
    write("splash.wav", splash())
    write("turbine_start.wav", turbine_start())


if __name__ == "__main__":
    main()
