#!/usr/bin/env python3
"""make_sounds.py - synthesises every sound and the music loops into ./sounds (needs numpy).

    python3 make_sounds.py

All files are mono 16-bit WAVs at 22050 Hz, the format the client's mixer uses. The names match the
sndfile[] table in client/audio.c. Tweak the recipes and rerun to change the sound.
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

def laugh():
    """Obnoxious Ha-Ha-Ha cackle: pitched buzz bursts with a breathy noise edge."""
    d = 0.95
    n = int(RATE * d)
    t = tt(d)
    # syllable gate: ~7.5 Hz "ha ha ha"
    syl = (0.55 + 0.45 * np.sin(2 * np.pi * 7.5 * t)) ** 4
    # pitch wobbles per syllable (female-ish laugh)
    f0 = 420 + 90 * np.sin(2 * np.pi * 7.5 * t) + 35 * np.sin(2 * np.pi * 3.2 * t)
    ph = np.cumsum(f0) / RATE
    buzz = (
        np.sin(2 * np.pi * ph)
        + 0.55 * np.sin(4 * np.pi * ph)
        + 0.28 * np.sin(6 * np.pi * ph)
        + 0.18 * np.sign(np.sin(2 * np.pi * ph))
    )
    # open-mouth formant-ish brightness
    voice = lowpass(buzz, 0.35 + 0.25 * syl) * syl
    breath = lowpass(noise(d), 0.55) * syl * 0.35
    # slight rising excitement
    env = 0.65 + 0.35 * (t / d)
    y = (voice + breath) * env * np.sin(np.pi * t / d) ** 0.35
    return fade(norm(np.tanh(1.6 * y), 0.92), 0.01, 0.06)

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

def clap_snare():
    """Drier clap-like snare for industrial / protest feel."""
    d = 0.12
    return (lowpass(noise(d), 0.55) * 1.1 + 0.15 * sweep(400, 180, d)) * np.exp(-tt(d) * 40)

def sleigh_hat(d=0.05):
    """Bright metallic tick for holiday accents."""
    t = tt(d)
    y = (np.sin(2 * np.pi * 2400 * t) + 0.5 * np.sin(2 * np.pi * 3600 * t)) * np.exp(-t * 70)
    y += 0.35 * (noise(d) - lowpass(noise(d), 0.15)) * np.exp(-t * 50)
    return y * 0.55

def crack_snare():
    """Snappy crack for ballpark feel."""
    d = 0.11
    return (lowpass(noise(d), 0.6) * 0.85 + 0.45 * sweep(380, 120, d)) * np.exp(-tt(d) * 32)

# Fight themes: stadium + one home stage per roster fighter (order matches chars[]).
# Each recipe drives the shared sequencer with distinct BPM / harmony / waveform / drums.
FIGHT_RECIPES = {
    'stadium': dict(  # classic arena rock
        bpm=150, bars=8,
        roots=[45, 41, 48, 43, 45, 41, 43, 45],
        triads=[(69, 72, 76), (65, 69, 72), (72, 76, 79), (67, 71, 74),
                (69, 72, 76), (65, 69, 72), (67, 71, 74), (69, 72, 76)],
        bass='sq', lead='sq', bass_duty=0.25, lead_duty=0.125,
        arp_pat=[0, 1, 2, 1], bass_oct_on=3, lead_up_half=1,
        kick_beats=(0, 2), snare_beats=(1, 3), hat_every=0.5, hat_swing=0.0,
        kick_vol=0.55, snare_vol=0.55, hat_vol=0.35, bass_vol=0.20, lead_vol=0.10,
        snare_fn='snare', hat_fn='hat', accent=None,
    ),
    'slurpy': dict(  # bouncy playground cartoon
        bpm=132, bars=8,
        roots=[48, 53, 50, 55, 48, 53, 50, 48],  # C F D G
        triads=[(60, 64, 67), (65, 69, 72), (62, 65, 69), (67, 71, 74),
                (60, 64, 67), (65, 69, 72), (62, 65, 69), (60, 64, 67)],
        bass='tri', lead='tri', bass_duty=0.5, lead_duty=0.5,
        arp_pat=[0, 1, 2, 1, 0, 2, 1, 2], bass_oct_on=7, lead_up_half=0,
        kick_beats=(0, 2), snare_beats=(1, 3), hat_every=0.5, hat_swing=0.0,
        kick_vol=0.38, snare_vol=0.40, hat_vol=0.28, bass_vol=0.18, lead_vol=0.14,
        snare_fn='snare', hat_fn='hat', accent='toy',
    ),
    'alien': dict(  # quirky UFO sci-fi
        bpm=128, bars=8,
        roots=[45, 47, 49, 51, 45, 47, 52, 45],  # whole-tone-ish walk
        triads=[(69, 73, 76), (71, 75, 78), (73, 77, 80), (75, 79, 82),
                (69, 73, 76), (71, 75, 78), (76, 80, 83), (69, 72, 76)],
        bass='sin', lead='sin', bass_duty=0.5, lead_duty=0.5,
        arp_pat=[0, 2, 1, 2], bass_oct_on=-1, lead_up_half=0,
        kick_beats=(0,), snare_beats=(2,), hat_every=0.25, hat_swing=0.0,
        kick_vol=0.40, snare_vol=0.35, hat_vol=0.22, bass_vol=0.22, lead_vol=0.12,
        snare_fn='snare', hat_fn='hat', accent='ufo',
    ),
    'rump': dict(  # pompous march / fanfare
        bpm=140, bars=8,
        roots=[48, 48, 55, 53, 48, 48, 55, 48],  # C C G F
        triads=[(60, 64, 67), (60, 64, 67), (67, 71, 74), (65, 69, 72),
                (60, 64, 67), (60, 64, 67), (67, 71, 74), (60, 64, 67)],
        bass='sq', lead='sq', bass_duty=0.35, lead_duty=0.4,
        arp_pat=[0, 0, 1, 2], bass_oct_on=3, lead_up_half=1,
        kick_beats=(0, 2), snare_beats=(1, 3), hat_every=0.5, hat_swing=0.0,
        kick_vol=0.50, snare_vol=0.70, hat_vol=0.25, bass_vol=0.18, lead_vol=0.13,
        snare_fn='snare', hat_fn='hat', accent='fanfare',
    ),
    'jv': dict(  # twangy country-fair
        bpm=138, bars=8,
        roots=[45, 50, 45, 52, 45, 50, 43, 45],  # A D A E
        triads=[(69, 72, 76), (74, 78, 81), (69, 72, 76), (76, 79, 83),
                (69, 72, 76), (74, 78, 81), (67, 71, 74), (69, 72, 76)],
        bass='saw', lead='saw', bass_duty=0.5, lead_duty=0.5,
        arp_pat=[0, 1, 0, 2, 1, 0, 2, 1], bass_oct_on=3, lead_up_half=0,
        kick_beats=(0, 2), snare_beats=(1, 3), hat_every=0.5, hat_swing=0.08,
        kick_vol=0.58, snare_vol=0.48, hat_vol=0.30, bass_vol=0.17, lead_vol=0.11,
        snare_fn='snare', hat_fn='hat', accent=None,
    ),
    'kammy': dict(  # grim industrial protest
        bpm=120, bars=8,
        roots=[40, 40, 38, 40, 40, 40, 38, 36],  # E drone-ish minor
        triads=[(64, 67, 71), (64, 67, 71), (62, 65, 69), (64, 67, 71),
                (64, 67, 71), (64, 67, 71), (62, 65, 69), (60, 64, 67)],
        bass='saw', lead='sq', bass_duty=0.5, lead_duty=0.2,
        arp_pat=[0, 0, 1, 0], bass_oct_on=-1, lead_up_half=0,
        kick_beats=(0, 1, 2, 3), snare_beats=(1, 3), hat_every=1.0, hat_swing=0.0,
        kick_vol=0.65, snare_vol=0.50, hat_vol=0.18, bass_vol=0.24, lead_vol=0.07,
        snare_fn='clap', hat_fn='hat', accent=None,
    ),
    'flusk': dict(  # awkward synth-pop
        bpm=145, bars=8,
        roots=[48, 46, 51, 48, 50, 46, 53, 48],
        triads=[(60, 64, 67), (58, 62, 65), (63, 67, 70), (60, 64, 67),
                (62, 65, 69), (58, 62, 65), (65, 69, 72), (60, 64, 67)],
        bass='sq', lead='sq', bass_duty=0.2, lead_duty=0.15,
        arp_pat=[0, 2, 1, 0, 2, 1, 2, 0], bass_oct_on=5, lead_up_half=1,
        kick_beats=(0, 2), snare_beats=(1, 3), hat_every=0.25, hat_swing=0.06,
        kick_vol=0.48, snare_vol=0.45, hat_vol=0.32, bass_vol=0.19, lead_vol=0.12,
        snare_fn='snare', hat_fn='hat', accent=None, skip_bass=(1, 5),
    ),
    'stephen': dict(  # ballpark calliope
        bpm=136, bars=8,
        roots=[48, 52, 53, 55, 48, 52, 50, 48],  # walk-up C E F G
        triads=[(60, 64, 67), (64, 67, 71), (65, 69, 72), (67, 71, 74),
                (60, 64, 67), (64, 67, 71), (62, 65, 69), (60, 64, 67)],
        bass='tri', lead='tri', bass_duty=0.5, lead_duty=0.5,
        arp_pat=[0, 1, 2, 1], bass_oct_on=3, lead_up_half=1,
        kick_beats=(0, 2), snare_beats=(1, 3), hat_every=0.5, hat_swing=0.0,
        kick_vol=0.50, snare_vol=0.55, hat_vol=0.28, bass_vol=0.18, lead_vol=0.13,
        snare_fn='crack', hat_fn='hat', accent=None,
    ),
    'lopicardo': dict(  # cubist techno assimilation
        bpm=155, bars=8,
        roots=[45, 45, 48, 45, 43, 45, 48, 45],
        triads=[(69, 72, 76), (69, 72, 76), (72, 76, 79), (69, 72, 76),
                (67, 71, 74), (69, 72, 76), (72, 76, 79), (69, 72, 76)],
        bass='sq', lead='sq', bass_duty=0.18, lead_duty=0.1,
        arp_pat=[0, 1, 2, 0, 1, 2, 0, 1], bass_oct_on=-1, lead_up_half=0,
        kick_beats=(0, 2), snare_beats=(1, 3), hat_every=0.25, hat_swing=0.0,
        kick_vol=0.52, snare_vol=0.48, hat_vol=0.38, bass_vol=0.22, lead_vol=0.11,
        snare_fn='snare', hat_fn='hat', accent='metal',
    ),
    'mike': dict(  # busy workshop chiptune
        bpm=148, bars=8,
        roots=[47, 50, 52, 50, 47, 45, 43, 47],
        triads=[(71, 74, 78), (74, 78, 81), (76, 79, 83), (74, 78, 81),
                (71, 74, 78), (69, 72, 76), (67, 71, 74), (71, 74, 78)],
        bass='saw', lead='sq', bass_duty=0.5, lead_duty=0.2,
        arp_pat=[0, 1, 2, 1, 2, 0, 1, 2], bass_oct_on=3, lead_up_half=1,
        kick_beats=(0, 2), snare_beats=(1, 3), hat_every=0.25, hat_swing=0.0,
        kick_vol=0.50, snare_vol=0.48, hat_vol=0.40, bass_vol=0.18, lead_vol=0.12,
        snare_fn='snare', hat_fn='hat', accent='machine',
    ),
    'krampus': dict(  # dark holiday carol
        bpm=112, bars=8,
        roots=[40, 43, 38, 40, 40, 43, 36, 40],  # Em-ish / Phrygian colors
        triads=[(64, 67, 71), (67, 70, 74), (62, 65, 69), (64, 67, 71),
                (64, 67, 71), (67, 70, 74), (60, 64, 67), (64, 67, 71)],
        bass='saw', lead='sin', bass_duty=0.5, lead_duty=0.5,
        arp_pat=[0, 1, 0, 2], bass_oct_on=-1, lead_up_half=0,
        kick_beats=(0, 2), snare_beats=(2,), hat_every=1.0, hat_swing=0.0,
        kick_vol=0.62, snare_vol=0.40, hat_vol=0.30, bass_vol=0.26, lead_vol=0.10,
        snare_fn='snare', hat_fn='sleigh', accent=None,
    ),
}

def music_fight_from(recipe):
    """Render an 8-bar looping fight theme from a FIGHT_RECIPES entry."""
    bpm = recipe['bpm']
    bars = recipe['bars']
    beat = 60.0 / bpm
    total = bars * 4 * beat
    buf = np.zeros(int(RATE * total))
    roots = recipe['roots']
    triads = recipe['triads']
    arp_pat = recipe['arp_pat']
    bass_kind = recipe['bass']
    lead_kind = recipe['lead']
    bass_duty = recipe['bass_duty']
    lead_duty = recipe['lead_duty']
    snare_fn = {'snare': snare, 'clap': clap_snare, 'crack': crack_snare}[recipe['snare_fn']]
    hat_fn = {'hat': hat, 'sleigh': sleigh_hat}[recipe['hat_fn']]
    accent = recipe.get('accent')

    for bar in range(bars):
        b0 = bar * 4 * beat
        r = roots[bar % len(roots)]
        triad = triads[bar % len(triads)]
        # Bass: eighth notes (syncopation via bass_oct_on)
        for i in range(8):
            if recipe['bass_oct_on'] < 0:
                n = r
            else:
                n = r + (12 if i % 4 == recipe['bass_oct_on'] % 4 else 0)
            if i in recipe.get('skip_bass', ()):
                continue
            dur = beat / 2 * 0.9
            x = osc(bass_kind, hz(n), dur, bass_duty) * np.exp(-tt(dur) * 8)
            put(buf, recipe['bass_vol'] * x, b0 + i * beat / 2)
        # Lead arpeggio
        up = 12 if recipe['lead_up_half'] and bar >= bars // 2 else 0
        steps = 16 if len(arp_pat) <= 4 else 8
        step = beat / 4 if steps == 16 else beat / 2
        for i in range(steps):
            n = triad[arp_pat[i % len(arp_pat)]] + up
            dur = step * 0.95
            x = osc(lead_kind, hz(n), dur, lead_duty) * np.exp(-tt(dur) * 10)
            # Dual layer for Mike workshop
            if accent == 'machine':
                x = 0.65 * x + 0.35 * osc('saw', hz(n), dur) * np.exp(-tt(dur) * 10)
            put(buf, recipe['lead_vol'] * x, b0 + i * step)
        # Drums
        for beat_i in range(4):
            t0 = b0 + beat_i * beat
            if beat_i in recipe['kick_beats']:
                put(buf, recipe['kick_vol'] * kick(), t0)
            if beat_i in recipe['snare_beats']:
                put(buf, recipe['snare_vol'] * snare_fn(), t0)
        # Hats
        he = recipe['hat_every']
        n_hats = int(round(4 * beat / he))
        for i in range(n_hats):
            off = i * he + (recipe['hat_swing'] if i % 2 else 0.0)
            put(buf, recipe['hat_vol'] * hat_fn(), b0 + off)
        # Accents
        if accent == 'toy' and bar % 2 == 1:
            dur = beat / 4 * 0.8
            put(buf, 0.18 * osc('sq', hz(84), dur, 0.2) * np.exp(-tt(dur) * 12),
                b0 + 3.5 * beat)
        elif accent == 'ufo':
            d = 2 * beat
            t = tt(d)
            f = hz(triad[0] + 12) * (1 + 0.08 * np.sin(2 * np.pi * 3 * t))
            ph = np.cumsum(f) / RATE
            wob = np.sin(2 * np.pi * ph) * np.sin(np.pi * t / d)
            put(buf, 0.10 * wob, b0 + beat)
        elif accent == 'fanfare' and bar % 2 == 0:
            for j, m in enumerate((triad[0] + 12, triad[1] + 12, triad[2] + 12)):
                dur = beat * 0.45
                x = osc('sq', hz(m), dur, 0.4) * np.exp(-tt(dur) * 4)
                put(buf, 0.08 * x, b0 + j * beat * 0.15)
        elif accent == 'metal' and bar % 2 == 0:
            d = 0.08
            y = (noise(d) - lowpass(noise(d), 0.1)) * np.exp(-tt(d) * 60)
            put(buf, 0.20 * y, b0 + 1.5 * beat)
        elif accent == 'machine':
            for i in range(4):
                put(buf, 0.12 * hat(0.025), b0 + i * beat + beat * 0.75)
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

# Order matches chars[] in config.h (plus stadium first).
FIGHT_TRACKS = (
    'stadium', 'slurpy', 'alien', 'rump', 'jv', 'kammy',
    'flusk', 'stephen', 'lopicardo', 'mike', 'krampus',
)

def main():
    items = {
        'swing1': swing(0.12, 0.08, 0.35), 'swing2': swing(0.16, 0.06, 0.45), 'swing3': swing(0.23, 0.05, 0.60),
        'hit1': hit(0), 'hit2': hit(1), 'hit3': hit(2), 'block': block(), 'shot': shot(),
        'jump': jump(), 'land': land(), 'fly': fly(), 'boom': boom(), 'bomb': bomb(), 'ship': ship(), 'ko': ko(),
        'laugh': laugh(),
        'menu_move': blip([900], [0.05], 0.4), 'menu_select': blip([660, 880], [0.06, 0.10], 0.5),
        'menu_ready': chord([72, 76, 79], 0.32), 'bot_add': blip([440, 560, 700], [0.04, 0.04, 0.07], 0.45),
    }
    want = sys.argv[1:]
    for k, v in items.items():
        if not want or k in want:
            save(k, v)
    if not want or 'music' in want:
        save('music_menu', music_menu())
        for name in FIGHT_TRACKS:
            save(f'music_fight_{name}', music_fight_from(FIGHT_RECIPES[name]))
        # Alias of stadium for older Xcode/CMake resource globs that still list it.
        save('music_fight', music_fight_from(FIGHT_RECIPES['stadium']))
    print('wrote', sorted(os.listdir(OUT)))

if __name__ == '__main__':
    main()
