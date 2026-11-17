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

    # shatter: bright noisy crash for breaking glass.
    def shatter(t):
        i = int(t * RATE)
        hp = noise[i % RATE] - noise[(i - 1) % RATE]
        ring = math.sin(2.0 * math.pi * 2900.0 * t) * 0.3
        return 0.7 * (hp + ring) * math.exp(-t * 11.0)
    render("shatter.wav", 0.3, shatter)

    # ---- week 9 audio pass ----
    # chime: two-note bell (checkpoints, the win) - reward, not alarm.
    def chime(t):
        n1 = math.sin(2.0 * math.pi * 880.0 * t) * math.exp(-t * 6.0)
        t2 = max(0.0, t - 0.16)
        n2 = math.sin(2.0 * math.pi * 1318.5 * t2) * math.exp(-t2 * 6.0) if t > 0.16 else 0.0
        return 0.30 * (n1 + n2)
    render("chime.wav", 0.6, chime)

    # beep_ok: the drone's happy double-beep (SPARK personality, beeps only).
    def beep_ok(t):
        if t < 0.09:
            f, tt = 1175.0, t
        elif 0.12 <= t < 0.22:
            f, tt = 1568.0, t - 0.12
        else:
            return 0.0
        env = min(tt / 0.008, 1.0) * math.exp(-tt * 22.0)
        sq = 1.0 if math.sin(2.0 * math.pi * f * t) >= 0.0 else -1.0  # squarish = robotic
        return 0.22 * env * (0.6 * sq + 0.4 * math.sin(2.0 * math.pi * f * t))
    render("beep_ok.wav", 0.26, beep_ok)

    # beep_no: low descending buzz - the drone says "can't do that here".
    def beep_no(t):
        f = 340.0 - 160.0 * t / 0.25
        sq = 1.0 if math.sin(2.0 * math.pi * f * t) >= 0.0 else -1.0
        return 0.20 * sq * math.exp(-t * 7.0)
    render("beep_no.wav", 0.25, beep_no)

    # ---- week 12 (PITCH & CATCH) ----
    # Charge whine: THREE seamless loops the script crossfades by charge01 —
    # the pitch rises then audibly PLATEAUS (three pre-baked loops beat an
    # audio.set_pitch engine seam, per the design cut list). Loopable = every
    # partial completes integer cycles in 0.5 s (multiples of 2 Hz), constant
    # amplitude, no envelope.
    def whine(f1, f2, wob):
        def fn(t):
            am = 1.0 + 0.12 * math.sin(2.0 * math.pi * wob * t)  # wob: integer*2 Hz
            return 0.16 * am * (math.sin(2.0 * math.pi * f1 * t)
                                + 0.5 * math.sin(2.0 * math.pi * f2 * t))
        return fn
    render("whine_low.wav", 0.5, whine(160.0, 322.0, 6.0))
    render("whine_mid.wav", 0.5, whine(280.0, 562.0, 8.0))
    render("whine_high.wav", 0.5, whine(440.0, 884.0, 12.0))

    # charge_click: the full-power tell - a short metallic latch.
    def charge_click(t):
        i = int(t * RATE)
        tick = (noise[i % RATE] - noise[(i - 1) % RATE]) * math.exp(-t * 140.0)
        ping = math.sin(2.0 * math.pi * 2400.0 * t) * math.exp(-t * 60.0)
        return 0.5 * (0.6 * tick + 0.5 * ping)
    render("charge_click.wav", 0.09, charge_click)

    # gong: the resonance bell RINGS - big, warm, long; unmistakably a reward
    # (the chime's larger sibling; inharmonic partials read as struck metal).
    def gong(t):
        env = min(t / 0.004, 1.0)
        s = (math.sin(2.0 * math.pi * 220.0 * t) * math.exp(-t * 2.2)
             + 0.65 * math.sin(2.0 * math.pi * 331.0 * t) * math.exp(-t * 3.0)
             + 0.4 * math.sin(2.0 * math.pi * 524.0 * t) * math.exp(-t * 4.5)
             + 0.25 * math.sin(2.0 * math.pi * 742.0 * t) * math.exp(-t * 7.0))
        return 0.5 * env * s
    render("gong.wav", 1.2, gong)

    # thunk: the under-speed bell answer - dull, muted, "harder".
    def thunk(t):
        f = 130.0 * math.exp(-t * 10.0) + 60.0
        return 0.42 * math.exp(-t * 30.0) * math.sin(2.0 * math.pi * f * t)
    render("thunk.wav", 0.16, thunk)

    # clunk: a crate seating into a snap-socket - thud + latch tick.
    def clunk(t):
        thud = math.sin(2.0 * math.pi * 95.0 * t) * math.exp(-t * 35.0)
        t2 = max(0.0, t - 0.06)
        tick = math.sin(2.0 * math.pi * 1500.0 * t2) * math.exp(-t2 * 70.0) if t > 0.06 else 0.0
        return 0.45 * thud + 0.2 * tick
    render("clunk.wav", 0.18, clunk)

    # snap: SPARK's magnet catch - descending zip into a soft grip thump.
    def snap(t):
        f = 1900.0 * math.exp(-t * 26.0) + 220.0
        zip_ = math.sin(2.0 * math.pi * f * t) * math.exp(-t * 22.0)
        t2 = max(0.0, t - 0.05)
        thump = math.sin(2.0 * math.pi * 130.0 * t2) * math.exp(-t2 * 40.0) if t > 0.05 else 0.0
        return 0.34 * zip_ + 0.3 * thump
    render("snap.wav", 0.2, snap)

    # ---- Assembly/Reactor zone ----
    # Appended after everything above, with its OWN rng + noise table, so no
    # earlier file's bytes can change. Two helpers keep the new set honest:
    # - render_peak bakes loudness like the rest of the file, but by
    #   normalising to a stated peak (noise-heavy recipes can't be eyeballed
    #   to a safe gain); it still writes through render(), so the WAV format
    #   is identical by construction.
    # - shaped() forces a one-shot to start and end at zero (no clicks).
    # The two hum loops are NOT shaped: every partial, modulator and tick
    # rate is an integer number of cycles in 2.0 s (multiples of 0.5 Hz), so
    # sample N would equal sample 0 - seamless by construction, same rule as
    # the whine loops.
    TAU = 2.0 * math.pi
    zrng = random.Random(4242)
    znoise = [zrng.uniform(-1.0, 1.0) for _ in range(RATE)]
    zgrain = [zrng.random() for _ in range(512)]

    def render_peak(name, duration, sample_fn, peak):
        n = int(RATE * duration)
        buf = [sample_fn(i / RATE) for i in range(n)]
        gain = peak / max(abs(v) for v in buf)
        render(name, duration, lambda t: gain * buf[min(n - 1, int(round(t * RATE)))])

    def shaped(fn, duration, fade_in=0.003, fade_out=0.02):
        def out(t):
            return fn(t) * min(1.0, t / fade_in, max(0.0, duration - t) / fade_out)
        return out

    def white(i, lag=0):
        return znoise[(i + lag) % RATE]

    # zap: tesla arc discharge - a hard crack, then grainy crackle riding a
    # sagging buzz-saw body. Harsh on purpose: this is the hazard's bite.
    zap_ph = [0.0]
    def zap(t):
        i = int(t * RATE)
        g = zgrain[int(t * 420.0) % 512] ** 3            # ~2.4 ms crackle grains
        hp = white(i) - white(i - 1)
        crack = hp * (0.22 + 1.5 * g) + 1.2 * hp * math.exp(-t * 90.0)
        zap_ph[0] += (112.0 - 34.0 * t) / RATE
        saw = 2.0 * (zap_ph[0] % 1.0) - 1.0
        body = math.tanh(2.2 * saw * (0.55 + 0.9 * g))
        sizzle = math.sin(TAU * 3100.0 * t) * g
        # tanh glues crack + body into one dense, saturated discharge.
        return math.exp(-t * 6.0) * math.tanh(1.5 * (0.5 * crack + 0.55 * body + 0.16 * sizzle))
    render_peak("zap.wav", 0.45, shaped(zap, 0.45, 0.001, 0.05), 0.72)

    # tesla_charge: the warning. A whine climbing ~4 octaves with a tremolo
    # that speeds up, crackle grains that get denser, and a swelling level -
    # the zap lands right where this ends, so it cuts off high (30 ms fade).
    chg_ph = [0.0]
    def tesla_charge(t):
        x = t / 0.7
        chg_ph[0] += TAU * 170.0 * (2500.0 / 170.0) ** x / RATE
        whine = math.sin(chg_ph[0]) + 0.4 * math.sin(2.0 * chg_ph[0])
        trem = 0.72 + 0.28 * math.sin(TAU * (14.0 + 40.0 * x) * t)
        i = int(t * RATE)
        g = zgrain[(int(t * 300.0) + 97) % 512]
        crack = (white(i, 7000) - white(i, 6999)) if g > 1.0 - 0.7 * x else 0.0
        buzz = 1.0 if math.sin(TAU * 100.0 * t) >= 0.0 else -1.0
        level = 0.22 + 0.78 * x ** 1.4
        return level * (0.6 * whine * trem + 0.42 * crack + 0.10 * buzz)
    render_peak("tesla_charge.wav", 0.7, shaped(tesla_charge, 0.7, 0.012, 0.03), 0.50)

    # hum_belt: seamless 2.0 s loop - low motor (55 Hz stack with a 6 Hz
    # throb) + a faint roller tick 4x/s, alternating strong/weak.
    def hum_belt(t):
        motor = (math.sin(TAU * 55.0 * t) + 0.55 * math.sin(TAU * 110.0 * t + 0.4)
                 + 0.30 * math.sin(TAU * 165.0 * t + 1.1) + 0.10 * math.sin(TAU * 330.0 * t + 2.0)
                 + 0.25 * math.sin(TAU * 27.5 * t + 0.6))
        throb = 1.0 + 0.16 * math.sin(TAU * 6.0 * t)
        tt = t % 0.25
        strong = 1.0 if int(t / 0.25 + 1e-9) % 2 == 0 else 0.55
        tick = (strong * min(tt / 0.002, 1.0) * math.exp(-tt * 110.0)
                * (math.sin(TAU * 1840.0 * tt) + 0.6 * math.sin(TAU * 2760.0 * tt)))
        return 0.6 * motor * throb + 0.26 * tick
    render_peak("hum_belt.wav", 2.0, hum_belt, 0.20)

    # hum_platform: seamless 2.0 s loop - a smoother servo drone. Higher and
    # purer than the belt (96 Hz + fifth + octave), one slow 0.5 Hz swell per
    # loop from a 96/96.5 Hz beat, a soft FM servo whine on top; no ticks.
    def hum_platform(t):
        drone = (math.sin(TAU * 96.0 * t + 0.5) + 0.45 * math.sin(TAU * 96.5 * t + 1.3)
                 + 0.42 * math.sin(TAU * 144.0 * t + 0.9) + 0.26 * math.sin(TAU * 192.0 * t + 0.3))
        servo = (0.15 * (1.0 + 0.3 * math.sin(TAU * 1.5 * t))
                 * math.sin(TAU * 624.0 * t + 1.8 * math.sin(TAU * 3.0 * t)))
        return drone + servo
    render_peak("hum_platform.wav", 2.0, hum_platform, 0.17)

    # pickup: data-core collect - three bright ascending bell notes (E6 G#6
    # B6) with a tremolo sparkle two octaves up on the last one.
    def pickup(t):
        s = 0.0
        for k, f in enumerate((1318.51, 1661.22, 1975.53)):
            tt = t - 0.075 * k
            if tt >= 0.0:
                a = min(tt / 0.004, 1.0) * math.exp(-tt * (8.0 if k == 2 else 15.0))
                s += a * (math.sin(TAU * f * tt) + 0.35 * math.sin(TAU * 2.0 * f * tt)
                          + 0.15 * math.sin(TAU * 3.0 * f * tt) * math.exp(-tt * 20.0))
        tt = t - 0.15
        if tt >= 0.0:
            s += (0.22 * math.sin(TAU * 3951.07 * tt) * math.exp(-tt * 9.0)
                  * (0.5 + 0.5 * math.sin(TAU * 26.0 * tt)) * min(tt / 0.01, 1.0))
        return s
    render_peak("pickup.wav", 0.5, shaped(pickup, 0.5, 0.002, 0.04), 0.36)

    # power_up: battery seated / circuit live. Stage 1 = contact thunk,
    # stage 2 = a tone that winds up an octave (G3 -> G4) and settles into a
    # fifth: "it is running now".
    pu_ph = [0.0]
    def power_up(t):
        i = int(t * RATE)
        thunk = (math.sin(TAU * 84.0 * t) * math.exp(-t * 26.0)
                 + 0.25 * (white(i, 300) - white(i, 299)) * math.exp(-t * 220.0))
        t2 = t - 0.11
        tone = 0.0
        if t2 >= 0.0:
            f = 196.0 + 196.0 * min(t2 / 0.17, 1.0) ** 0.7
            pu_ph[0] += TAU * f / RATE
            hold = math.exp(-max(0.0, t2 - 0.17) * 5.0)
            tone = min(t2 / 0.02, 1.0) * hold * (math.sin(pu_ph[0]) + 0.4 * math.sin(2.0 * pu_ph[0])
                                                 + 0.18 * math.sin(3.0 * pu_ph[0]))
            t3 = t2 - 0.17
            if t3 >= 0.0:
                tone += 0.45 * min(t3 / 0.02, 1.0) * math.exp(-t3 * 5.0) * math.sin(TAU * 587.33 * t3)
        return 0.85 * thunk + 0.6 * tone
    render_peak("power_up.wav", 0.6, shaped(power_up, 0.6, 0.002, 0.05), 0.50)

    # power_down: the reverse feel - the tone sags from G4 toward A2 while
    # its harmonics die first, then a dull relay drop.
    pd_ph = [0.0]
    def power_down(t):
        pd_ph[0] += TAU * (110.0 + 282.0 * math.exp(-t * 9.0)) / RATE
        a = math.exp(-t * 6.0)
        tone = a * (math.sin(pd_ph[0]) + 0.4 * a * math.sin(2.0 * pd_ph[0])
                    + 0.18 * a * a * math.sin(3.0 * pd_ph[0]))
        t2 = t - 0.27
        drop = math.sin(TAU * 62.0 * t2) * math.exp(-t2 * 24.0) if t2 >= 0.0 else 0.0
        return 0.6 * tone + 0.75 * drop
    render_peak("power_down.wav", 0.5, shaped(power_down, 0.5, 0.004, 0.05), 0.45)

    # impact_soft / impact_hard: a crate meeting concrete. Unlike thunk (one
    # swept sine) and clunk (sine + latch tick) these are a low-passed noise
    # slap over a cluster of short INHARMONIC modes - boxy wood, a little
    # sheet-metal on top. Hard = lower, longer, grittier, with a rattle.
    def impact(modes, noise_amt, noise_decay, lp, lag, rattle=0.0):
        state = [0.0]
        def fn(t):
            i = int(t * RATE)
            state[0] += lp * (white(i, lag) - state[0])
            body = sum(a * math.sin(TAU * f * t) * math.exp(-t * d) for f, a, d in modes)
            t2 = t - 0.062
            if rattle and t2 >= 0.0:
                body += rattle * math.exp(-t2 * 55.0) * (math.sin(TAU * 347.0 * t2)
                                                         + 0.5 * math.sin(TAU * 1410.0 * t2))
            return body + noise_amt * state[0] * math.exp(-t * noise_decay)
        return fn
    render_peak("impact_soft.wav", 0.15, shaped(impact(
        ((172.0, 1.0, 38.0), (291.0, 0.5, 55.0), (463.0, 0.3, 80.0), (1240.0, 0.06, 90.0)),
        1.6, 70.0, 0.25, 1500), 0.15, 0.001, 0.03), 0.36)
    render_peak("impact_hard.wav", 0.25, shaped(impact(
        ((118.0, 1.0, 22.0), (203.0, 0.7, 30.0), (347.0, 0.45, 42.0), (588.0, 0.25, 60.0),
         (1410.0, 0.12, 55.0)),
        2.6, 45.0, 0.4, 9000, rattle=0.3), 0.25, 0.001, 0.04), 0.78)

    # chirp_hint / chirp_sad: SPARK's voice (same squarish timbre as
    # beep_ok/beep_no), but GLIDING notes so they read as inflection:
    # hint = short note then a rising "?" ; sad = a note that droops, twice.
    def spark_voice(notes, decay):
        ph = [0.0]
        def fn(t):
            for t0, t1, f0, f1 in notes:
                if t0 <= t < t1:
                    x = (t - t0) / (t1 - t0)
                    ph[0] += TAU * f0 * (f1 / f0) ** x / RATE
                    env = min((t - t0) / 0.008, 1.0, (t1 - t) / 0.014) * math.exp(-(t - t0) * decay)
                    sq = 1.0 if math.sin(ph[0]) >= 0.0 else -1.0
                    return env * (0.6 * sq + 0.4 * math.sin(ph[0]))
            return 0.0
        return fn
    render_peak("chirp_hint.wav", 0.35, spark_voice(
        ((0.0, 0.10, 988.0, 1046.5), (0.13, 0.345, 1174.7, 1864.7)), 5.0), 0.22)
    render_peak("chirp_sad.wav", 0.35, spark_voice(
        ((0.0, 0.11, 880.0, 830.6), (0.14, 0.345, 740.0, 466.2)), 5.0), 0.20)

    # prism: the laser finds a prism - a glassy ping. Inharmonic partials
    # (x2.76, x4.07) make it glass rather than bell; a detuned twin of the
    # fundamental beats at ~8 Hz and the 2nd partial flutters = the shimmer.
    def prism(t):
        f = 2093.0
        i = int(t * RATE)
        s = (math.sin(TAU * f * t) * math.exp(-t * 7.0)
             + 0.6 * math.sin(TAU * f * 1.004 * t) * math.exp(-t * 6.0)
             + 0.45 * math.sin(TAU * f * 2.76 * t) * math.exp(-t * 11.0)
             * (0.6 + 0.4 * math.sin(TAU * 23.0 * t))
             + 0.25 * math.sin(TAU * f * 4.07 * t) * math.exp(-t * 16.0)
             + 0.3 * (white(i, 12000) - white(i, 11999)) * math.exp(-t * 300.0))
        return s
    render_peak("prism.wav", 0.6, shaped(prism, 0.6, 0.001, 0.06), 0.34)

    # door_hiss: pneumatic release - a soft low thump, then air: high-passed
    # noise through a low-pass that closes as the pressure bleeds off.
    hiss_lp = [0.0]
    def door_hiss(t):
        i = int(t * RATE)
        hp = white(i, 15000) - white(i, 14999)
        hiss_lp[0] += (0.78 - 0.5 * min(t / 0.4, 1.0)) * (hp - hiss_lp[0])
        air = min(t / 0.02, 1.0) * math.exp(-t * 6.5) * hiss_lp[0]
        thump = math.sin(TAU * 66.0 * t) * math.exp(-t * 30.0)
        return 0.6 * air + 0.8 * thump
    render_peak("door_hiss.wav", 0.5, shaped(door_hiss, 0.5, 0.002, 0.06), 0.50)

    # Shared bell voice for the two jingles: the chime's pure sine + a soft
    # octave that dies early - same timbre family, a little warmer.
    def bell_note(f, tt, decay):
        if tt < 0.0:
            return 0.0
        return (min(tt / 0.004, 1.0) * math.exp(-tt * decay)
                * (math.sin(TAU * f * tt) + 0.22 * math.sin(TAU * 2.0 * f * tt) * math.exp(-tt * 4.0)))

    # fanfare: the win stinger - C major arpeggio (C5 E5 G5 C6) in chime
    # timbre, landing on a full, low-rooted C chord that rings out. Resolved
    # and warm; no brass.
    def fanfare(t):
        s = 0.0
        for k, f in enumerate((523.25, 659.26, 783.99, 1046.5)):
            s += 0.8 * bell_note(f, t - 0.13 * k, 5.0)
        for f, a, d in ((130.81, 0.55, 1.5), (261.63, 0.8, 1.5), (392.0, 0.6, 1.6),
                        (659.26, 0.55, 1.7), (1046.5, 0.45, 1.9), (1318.51, 0.25, 2.3)):
            s += a * bell_note(f, t - 0.56, d)
        return s
    render_peak("fanfare.wav", 2.5, shaped(fanfare, 2.5, 0.002, 0.3), 0.62)

    # secret: "all cores found" - a quick A-major run up two octaves, then a
    # high C#7 answered by a shimmering A chord. Brighter and quirkier than
    # the fanfare so the two never get confused.
    def secret(t):
        s = 0.0
        for k, f in enumerate((880.0, 1108.73, 1318.51, 1661.22, 1760.0)):
            s += 0.7 * bell_note(f, t - 0.08 * k, 9.0)
        t2 = t - 0.46
        if t2 >= 0.0:
            shimmer = 0.8 + 0.2 * math.sin(TAU * 11.0 * t2)
            s += shimmer * (0.8 * bell_note(2217.46, t2, 3.2) + 0.5 * bell_note(1318.51, t2, 3.0)
                            + 0.5 * bell_note(880.0, t2, 3.0) + 0.3 * bell_note(440.0, t2, 3.0))
        return s
    render_peak("secret.wav", 1.2, shaped(secret, 1.2, 0.002, 0.15), 0.42)


if __name__ == "__main__":
    main()
