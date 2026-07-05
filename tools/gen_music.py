#!/usr/bin/env python3
"""Generates VAULT's two looping beds (week 5 audio pass):
  assets/sounds/ambient.wav - low facility room tone (HVAC hum + air)
  assets/sounds/music.wav    - slow minor pad + sparse bell arpeggio

Both are SEAMLESS loops: rendered a touch long, then the tail is equal-power
crossfaded back over the head so sample[n-1] -> sample[0] is continuous (no
click at the loop point). Persistent looping voices carry their own volume
now (forge.audio.loop + set_volume), so loudness is NOT baked hot here -
scene.lua sets the mix. Deterministic; regenerate with:
    python tools/gen_music.py
"""

import math
import os
import random
import struct
import wave

RATE = 22050


def render_loop(name, duration, sample_fn, xfade=0.5):
    """Render `duration` seconds that loop seamlessly. Generates duration+xfade
    seconds, then crossfades the extra tail over the head (equal-power)."""
    out = os.path.join(os.path.dirname(__file__), "..", "assets", "sounds", name)
    os.makedirs(os.path.dirname(out), exist_ok=True)
    n = int(RATE * duration)
    extra = int(RATE * xfade)
    buf = [sample_fn(i / RATE) for i in range(n + extra)]
    # Blend the natural continuation (buf[n+i]) into the head (buf[i]) so the
    # head begins exactly where the tail would have carried on.
    for i in range(extra):
        a = i / extra
        fade_in = math.sin(a * math.pi * 0.5)
        fade_out = math.cos(a * math.pi * 0.5)
        buf[i] = buf[i] * fade_in + buf[n + i] * fade_out
    frames = bytearray()
    for i in range(n):
        frames += struct.pack("<h", max(-32767, min(32767, int(32767 * buf[i]))))
    with wave.open(out, "wb") as w:
        w.setnchannels(1)
        w.setsampwidth(2)
        w.setframerate(RATE)
        w.writeframes(bytes(frames))
    print(f"wrote {os.path.normpath(out)} ({n} samples, {duration:g}s loop)")


def main() -> None:
    rng = random.Random(11)
    # One second of tileable noise (index wraps) for the "air" layer.
    noise = [rng.uniform(-1.0, 1.0) for _ in range(RATE)]

    # --- ambient: deep HVAC hum (detuned low sines -> slow beating) + a thin
    # filtered-noise hiss for room air. Subtle by design; it plays forever.
    def ambient(t):
        hum = (0.55 * math.sin(2.0 * math.pi * 50.0 * t)
               + 0.35 * math.sin(2.0 * math.pi * 50.7 * t)   # detune -> ~0.7 Hz beat
               + 0.22 * math.sin(2.0 * math.pi * 100.0 * t))
        i = int(t * RATE)
        air = (noise[i % RATE] - 0.7 * noise[(i - 1) % RATE]) * 0.06  # high-passed hiss
        swell = 0.85 + 0.15 * math.sin(2.0 * math.pi * t / 8.0)      # slow breathing
        return 0.5 * swell * (hum + air)
    render_loop("ambient.wav", 8.0, ambient, xfade=1.0)

    # --- music: A-minor drone pad (A2 root, E3 fifth, C4 minor third) with a
    # slow tremolo, plus a soft sine bell that steps A4 -> C5 -> E5 -> C5 once
    # per 4 s (16 s loop = 4 steps). Moody, sparse - a puzzle-game underscore.
    pad_freqs = [(110.00, 0.34), (164.81, 0.26), (130.81, 0.22),
                 (55.00, 0.30)]  # + sub-octave root for weight
    bell_seq = [440.00, 523.25, 659.25, 523.25]

    def music(t):
        pad = 0.0
        for f, amp in pad_freqs:
            pad += amp * math.sin(2.0 * math.pi * f * t)
            pad += 0.4 * amp * math.sin(2.0 * math.pi * f * 1.003 * t)  # chorus detune
        trem = 0.8 + 0.2 * math.sin(2.0 * math.pi * t / 6.0)
        # Bell: retrigger every 4 s; exponential pluck decay well within the step.
        step = int(t / 4.0) % len(bell_seq)
        local = t - math.floor(t / 4.0) * 4.0
        env = math.exp(-local * 2.2) * (1.0 - math.exp(-local * 60.0))
        bell = 0.30 * env * (math.sin(2.0 * math.pi * bell_seq[step] * t)
                             + 0.3 * math.sin(2.0 * math.pi * bell_seq[step] * 2.0 * t))
        return 0.42 * (trem * pad * 0.7 + bell)
    render_loop("music.wav", 16.0, music, xfade=2.0)


if __name__ == "__main__":
    main()
