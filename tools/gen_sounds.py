#!/usr/bin/env python3
"""Generates assets/sounds/*.wav - all of VAULT's procedural SFX.
Loudness is BAKED into each sample (the lean-P8 Audio has one engine-level
volume knob; per-voice mixing arrives with the week-5 audio pass, ADR-020).
Deterministic; regenerate with:  python tools/gen_sounds.py
Replaces gen_kick_wav.py (kick.wav recipe unchanged).
"""

import math
import os
import random
import struct
import wave

RATE = 22050


def render(name, duration, sample_fn):
    out = os.path.join(os.path.dirname(__file__), "..", "assets", "sounds", name)
    os.makedirs(os.path.dirname(out), exist_ok=True)
    n = int(RATE * duration)
    frames = bytearray()
    for i in range(n):
        t = i / RATE
        frames += struct.pack("<h", max(-32767, min(32767, int(32767 * sample_fn(t)))))
    with wave.open(out, "wb") as w:
        w.setnchannels(1)
        w.setsampwidth(2)
        w.setframerate(RATE)
        w.writeframes(bytes(frames))
    print(f"wrote {os.path.normpath(out)} ({n} samples)")


def main() -> None:
    rng = random.Random(42)
    noise = [rng.uniform(-1.0, 1.0) for _ in range(RATE)]

    # kick: the original thump (throw impacts, crate drops).
    phase = [0.0]
    def kick(t):
        freq = 160.0 * math.exp(-t * 9.0) + 45.0
        phase[0] += 2.0 * math.pi * freq / RATE
        return 0.92 * math.exp(-t * 18.0) * math.sin(phase[0])
    render("kick.wav", 0.22, kick)

    # grab: short rising blip - "the glove locked on".
    gphase = [0.0]
    def grab(t):
        freq = 200.0 + 2600.0 * t
        gphase[0] += 2.0 * math.pi * freq / RATE
        env = min(t / 0.012, 1.0) * math.exp(-t * 24.0)
        return 0.36 * env * math.sin(gphase[0])
    render("grab.wav", 0.14, grab)

    # throw: airy whoosh (high-passed noise burst).
    def throw(t):
        i = int(t * RATE)
        hp = noise[i % RATE] - noise[(i - 1) % RATE]  # cheap high-pass
        return 0.55 * hp * math.exp(-t * 16.0)
    render("throw.wav", 0.18, throw)

    # jump: quick up-chirp.
    jphase = [0.0]
    def jump(t):
        freq = 140.0 + 1100.0 * t
        jphase[0] += 2.0 * math.pi * freq / RATE
        return 0.34 * math.exp(-t * 20.0) * math.sin(jphase[0])
    render("jump.wav", 0.14, jump)

    # land: low thud, softer + shorter than kick.
    lphase = [0.0]
    def land(t):
        freq = 110.0 * math.exp(-t * 12.0) + 50.0
        lphase[0] += 2.0 * math.pi * freq / RATE
        return 0.55 * math.exp(-t * 26.0) * math.sin(lphase[0])
    render("land.wav", 0.15, land)

    # step: tiny filtered tick, quiet by design (it repeats constantly).
    def step(t):
        i = int(t * RATE)
        hp = noise[i % RATE] - 0.6 * noise[(i - 2) % RATE]
        return 0.16 * hp * math.exp(-t * 60.0)
    render("step.wav", 0.06, step)


if __name__ == "__main__":
    main()
