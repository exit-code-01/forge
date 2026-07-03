#!/usr/bin/env python3
"""Generates assets/sounds/kick.wav - a procedural 'thump' (sine sweep with
exponential decay). Same provenance policy as gen_torus.py: no external
assets, deterministic, regenerate with:  python tools/gen_kick_wav.py
"""

import math
import os
import struct
import wave

RATE = 22050
DURATION = 0.22


def main() -> None:
    out_path = os.path.join(os.path.dirname(__file__), "..", "assets", "sounds", "kick.wav")
    os.makedirs(os.path.dirname(out_path), exist_ok=True)

    frames = bytearray()
    n = int(RATE * DURATION)
    phase = 0.0
    for i in range(n):
        t = i / RATE
        freq = 160.0 * math.exp(-t * 9.0) + 45.0   # pitch drops fast: thump
        phase += 2.0 * math.pi * freq / RATE
        amp = math.exp(-t * 18.0)                   # fast decay
        sample = int(30000 * amp * math.sin(phase))
        frames += struct.pack("<h", sample)

    with wave.open(out_path, "wb") as w:
        w.setnchannels(1)
        w.setsampwidth(2)
        w.setframerate(RATE)
        w.writeframes(bytes(frames))
    print(f"wrote {os.path.normpath(out_path)}: {n} samples")


if __name__ == "__main__":
    main()
