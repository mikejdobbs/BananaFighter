#!/usr/bin/env python3
"""make_sounds.py - synthesises every sound and the two music loops into ./sounds (needs numpy).

    python3 make_sounds.py

All files are mono 16-bit WAVs at 22050 Hz, the format the client's mixer uses. The names match the
sndfile[] table in client.c. Tweak the recipes and rerun to change the sound.
"""
import os, sys, wave
import numpy as np

RATE = 22050
OUT = os.path.join(os.path.dirname(os.path.abspath(__file__)), 'sounds')
rng = np.random.default_rng(7)

def tt(d):
    return np.arange(int(RATE * d)) / RATE

def lowpass(x, k):
    """One-pole low-pass; k may be a scalar or a per-sample array (0..1, higher = brighter)."""
    ks = np.broadcast_to(np.asarray(k, dtype=float), x.shape)
    y = np.empty_like(x)
    acc = 0.0
    for i in range(len(x)):
        acc += ks[i] * (x[i] - acc)
        y[i] = acc
    return y

def noise(d):
    return rng.uniform(-1, 1, int(RATE * d))

def sweep(f0, f1, d, kind='sin'):
    n = int(RATE * d)
    ph = np.cumsum(np.linspace(f0, f1, n)) / RATE
    if kind == 'sq':
        return np.sign(np.sin(2 * np.pi * ph))
    if kind == 'saw':
        return 2 * (ph % 1) - 1
    return np.sin(2 * np.pi * ph)

def fade(x, a=0.004, r=0.012):
    n = len(x)
    na, nr = min(n, int(RATE * a)), min(n, int(RATE * r))
    x = x.copy()
    x[:na] *= np.linspace(0, 1, na)
    x[n - nr:] *= np.linspace(1, 0, nr)
    return x

def norm(x, vol=0.9):
    return x / (np.max(np.abs(x)) + 1e-9) * vol

def save(name, x):
    os.makedirs(OUT, exist_ok=True)
    pcm = (np.clip(x, -1, 1) * 32767).astype('<i2')
    with wave.open(os.path.join(OUT, name + '.wav'), 'wb') as w:
        w.setnchannels(1)
        w.setsampwidth(2)
        w.setframerate(RATE)
        w.writeframes(pcm.tobytes())

# ------------------------------------------------------------ effects

def swing(d, k0, k1):                           # whoosh: noise whose brightness sweeps up, with a bell envelope
    n = int(RATE * d)
    y = lowpass(noise(d), np.linspace(k0, k1, n)) * np.sin(np.pi * np.linspace(0, 1, n)) ** 1.5
    return fade(norm(y, 0.8))

def hit(s):                                     # impact: pitch-dropping thump plus a noise crack
    d = 0.10 + 0.07 * s
    t = tt(d)
    thump = sweep(210 - 30 * s, 55, d) * np.exp(-t * (30 - 8 * s))
    crack = lowpass(noise(d), 0.5) * np.exp(-t * 90)
    return fade(norm(np.tanh(2.2 * (thump + 0.7 * crack)), 0.9))

def block():                                    # short metallic tick
    t = tt(0.1)
    y = (np.sin(2 * np.pi * 760 * t) + 0.6 * np.sin(2 * np.pi * 1140 * t)) * np.exp(-t * 48)
    y += 0.4 * noise(0.1) * np.exp(-t * 120)
    return fade(norm(y, 0.7))

def shot():                                     # laser/fireball launch: falling chirp
    t = tt(0.28)
    return fade(norm((0.6 * sweep(1500, 320, 0.28, 'sq') + 0.5 * sweep(1500, 320, 0.28)) * np.exp(-t * 9), 0.7))

def jump():
    t = tt(0.15)
    return fade(norm(sweep(260, 640, 0.15) * np.exp(-t * 8), 0.55))

def land():
    d = 0.10
    t = tt(d)
    return fade(norm(sweep(110, 50, d) * np.exp(-t * 35) + 0.5 * lowpass(noise(d), 0.15) * np.exp(-t * 45), 0.6))

def fly():                                      # tail whirr: buzzy noise chopped at the rotor rate
    d = 0.18
    t = tt(d)
    return fade(norm(lowpass(noise(d), 0.22) * (0.55 + 0.45 * np.sin(2 * np.pi * 40 * t)) * np.sin(np.pi * t / d), 0.5))

def boom():
    d = 0.95
    n = int(RATE * d)
    t = tt(d)
    body = lowpass(noise(d), np.linspace(0.55, 0.02, n)) * np.exp(-t * 4.2)
    rumble = sweep(70, 38, d) * np.exp(-t * 4.0)
    return fade(norm(np.tanh(2.5 * (body + 0.8 * rumble)), 0.95), r=0.05)

def bomb():                                     # falling whistle
    d = 0.5
    t = tt(d)
    return fade(norm(sweep(1800, 520, d) * np.sin(np.pi * t / d) ** 0.7, 0.5), 0.02, 0.05)

def ship():                                     # warbling hum
    d = 0.75
    t = tt(d)
    f = 170 + 14 * np.sin(2 * np.pi * 7 * t)
    ph = np.cumsum(f) / RATE
    y = np.sin(2 * np.pi * ph) + 0.5 * np.sin(4 * np.pi * ph) + 0.25 * np.sin(2 * np.pi * ph * 3)
    return fade(norm(y * np.sin(np.pi * t / d) ** 0.6, 0.55), 0.08, 0.12)

def ko():
    d = 0.9
    t = tt(d)
    y = sweep(560, 70, d, 'saw') * np.exp(-t * 3.2) + 0.6 * lowpass(noise(d), 0.3) * np.exp(-t * 14)
    return fade(norm(np.tanh(1.8 * y), 0.85), r=0.08)

def blip(freqs, d, vol=0.5, kind='sq'):
    out = np.zeros(int(RATE * sum(d)))
    pos = 0
    for f, dd in zip(freqs, d):
        t = tt(dd)
        w = np.sign(np.sin(2 * np.pi * f * t)) if kind == 'sq' else np.sin(2 * np.pi * f * t)
        out[pos:pos + len(t)] += fade(w * np.exp(-t * 6 / dd * 0.3), 0.002, 0.01)
        pos += len(t)
    return norm(out, vol)

def chord(midis, d, vol=0.6):
    t = tt(d)
    y = sum(np.sin(2 * np.pi * hz(m) * t) + 0.3 * np.sign(np.sin(2 * np.pi * hz(m) * t)) for m in midis)
    return fade(norm(y * np.exp(-t * 5), vol), 0.004, 0.05)

# ------------------------------------------------------------ music

def hz(m):
    return 440.0 * 2 ** ((m - 69) / 12.0)

def osc(kind, f, d, duty=0.5):
    t = tt(d)
    ph = (f * t) % 1
    if kind == 'sq':
        return np.where(ph < duty, 1.0, -1.0)
    if kind == 'tri':
        return 4 * np.abs(ph - 0.5) - 1
    if kind == 'saw':
        return 2 * ph - 1
    return np.sin(2 * np.pi * ph)

def put(buf, x, start):
    start = int(start * RATE)
    n = min(len(x), len(buf) - start)
    if n > 0:
        buf[start:start + n] += x[:n]

def kick():
    d = 0.18
    return sweep(160, 42, d) * np.exp(-tt(d) * 20)

def snare():
    d = 0.14
    return (lowpass(noise(d), 0.45) * 0.9 + 0.3 * sweep(220, 150, d)) * np.exp(-tt(d) * 24)

def hat(d=0.04):
    n = noise(d)
    return (n - lowpass(n, 0.2)) * np.exp(-tt(d) * 90) * 0.6

def music_fight():
    bpm, bars = 150, 8
    beat = 60.0 / bpm
    total = bars * 4 * beat
    buf = np.zeros(int(RATE * total))
    roots = [45, 41, 48, 43, 45, 41, 43, 45]                      # A F C G A F G A
    triads = [(69, 72, 76), (65, 69, 72), (72, 76, 79), (67, 71, 74),
              (69, 72, 76), (65, 69, 72), (67, 71, 74), (69, 72, 76)]
    for bar in range(bars):
        b0 = bar * 4 * beat
        r = roots[bar]
        for i in range(8):                                        # driving eighth-note bass
            n = r + (12 if i % 4 == 3 else 0)
            x = osc('sq', hz(n), beat / 2 * 0.9, 0.25) * np.exp(-tt(beat / 2 * 0.9) * 8)
            put(buf, 0.20 * x, b0 + i * beat / 2)
        up = 12 if bar >= 4 else 0                                # lead goes up an octave in the second half
        for i in range(16):                                       # sixteenth-note arpeggio
            n = triads[bar][[0, 1, 2, 1][i % 4]] + up
            x = osc('sq', hz(n), beat / 4 * 0.95, 0.125) * np.exp(-tt(beat / 4 * 0.95) * 10)
            put(buf, 0.10 * x, b0 + i * beat / 4)
        for beat_i in range(4):
            put(buf, 0.55 * (kick() if beat_i % 2 == 0 else snare()), b0 + beat_i * beat)
        for i in range(8):
            put(buf, 0.35 * hat(), b0 + i * beat / 2 + (beat / 4 if False else 0))
    return norm(buf, 0.8)

def music_menu():
    bpm, bars = 92, 8
    beat = 60.0 / bpm
    total = bars * 4 * beat
    buf = np.zeros(int(RATE * total))
    roots = [48, 45, 41, 43, 48, 45, 41, 43]                      # C Am F G
    triads = [(60, 64, 67), (57, 60, 64), (53, 57, 60), (55, 59, 62)] * 2
    for bar in range(bars):
        b0 = bar * 4 * beat
        d = 4 * beat
        for n in triads[bar]:                                     # soft sustained pad
            x = osc('tri', hz(n), d) * np.sin(np.pi * tt(d) / d) ** 0.5
            put(buf, 0.10 * x, b0)
        put(buf, 0.20 * osc('sin', hz(roots[bar] - 12), d) * np.exp(-tt(d) * 0.8), b0)
        for i in range(8):                                        # gentle arpeggio
            n = triads[bar][[0, 1, 2, 1, 2, 1, 0, 1][i]] + 12
            x = osc('sin', hz(n), beat / 2 * 0.95) * np.exp(-tt(beat / 2 * 0.95) * 5)
            put(buf, 0.13 * x, b0 + i * beat / 2)
        for beat_i in (0, 2):
            put(buf, 0.25 * kick(), b0 + beat_i * beat)
        for i in range(4):
            put(buf, 0.20 * hat(), b0 + i * beat + beat / 2)
    return norm(buf, 0.7)

def main():
    items = {
        'swing1': swing(0.12, 0.08, 0.35), 'swing2': swing(0.16, 0.06, 0.45), 'swing3': swing(0.23, 0.05, 0.60),
        'hit1': hit(0), 'hit2': hit(1), 'hit3': hit(2), 'block': block(), 'shot': shot(),
        'jump': jump(), 'land': land(), 'fly': fly(), 'boom': boom(), 'bomb': bomb(), 'ship': ship(), 'ko': ko(),
        'menu_move': blip([900], [0.05], 0.4), 'menu_select': blip([660, 880], [0.06, 0.10], 0.5),
        'menu_ready': chord([72, 76, 79], 0.32), 'bot_add': blip([440, 560, 700], [0.04, 0.04, 0.07], 0.45),
    }
    want = sys.argv[1:]
    for k, v in items.items():
        if not want or k in want:
            save(k, v)
    if not want or 'music' in want:
        save('music_menu', music_menu())
        save('music_fight', music_fight())
    print('wrote', sorted(os.listdir(OUT)))

if __name__ == '__main__':
    main()
