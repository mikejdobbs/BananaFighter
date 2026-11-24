#!/usr/bin/env python3
"""make_sounds.py - synthesises every sound and the music loops into ./sounds (needs numpy).

    python3 make_sounds.py
    python3 make_sounds.py music

SFX are mono 16-bit WAVs at 44100 Hz. Music is stereo 16-bit at 44100 Hz. Names match the
sndfile[] table in client/audio.c. Tweak the recipes and rerun to change the sound.
"""
import os, sys, wave
import numpy as np

RATE = 44100
OUT = os.path.join(os.path.dirname(os.path.abspath(__file__)), 'sounds')
rng = np.random.default_rng(7)

def tt(d):
    return np.arange(int(RATE * d)) / RATE

def lowpass(x, k):
    """One-pole low-pass; k may be a scalar or a per-sample array (0..1, higher = brighter)."""
    x = np.asarray(x, dtype=np.float64)
    y = np.empty_like(x)
    if np.isscalar(k):
        a = float(k)
        acc = 0.0
        for i in range(len(x)):
            acc += a * (x[i] - acc)
            y[i] = acc
        return y
    ks = np.asarray(k, dtype=np.float64)
    if ks.shape != x.shape:
        ks = np.broadcast_to(ks, x.shape)
    acc = 0.0
    for i in range(len(x)):
        acc += ks[i] * (x[i] - acc)
        y[i] = acc
    return y

def highpass(x, k=0.05):
    """Fast high-pass: x - moving-average lowpass (k ~ brightness of removed lows)."""
    x = np.asarray(x, dtype=np.float64)
    # Map old one-pole k to a rough MA window
    win = max(3, int(3 + (1.0 - float(k)) * 40))
    if win % 2 == 0:
        win += 1
    ker = np.ones(win) / win
    return x - np.convolve(x, ker, mode='same')

def soft_lp(x, cutoff_hz):
    """Fast anti-alias tame: moving-average blend (avoids slow one-pole Python loops)."""
    x = np.asarray(x, dtype=np.float64)
    a = float(np.clip(1.0 - np.exp(-2.0 * np.pi * cutoff_hz / RATE), 0.05, 0.95))
    win = max(3, int(RATE / max(cutoff_hz, 200.0)))
    if win % 2 == 0:
        win += 1
    ker = np.ones(win) / win
    y = np.convolve(x, ker, mode='same')
    return (1.0 - a) * x + a * y

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
    """Write a mono SFX WAV."""
    os.makedirs(OUT, exist_ok=True)
    pcm = (np.clip(x, -1, 1) * 32767).astype('<i2')
    with wave.open(os.path.join(OUT, name + '.wav'), 'wb') as w:
        w.setnchannels(1)
        w.setsampwidth(2)
        w.setframerate(RATE)
        w.writeframes(pcm.tobytes())

def save_music(name, L, R):
    """Write a stereo music WAV (interleaved L/R)."""
    os.makedirs(OUT, exist_ok=True)
    L = np.asarray(L, dtype=np.float64)
    R = np.asarray(R, dtype=np.float64)
    n = min(len(L), len(R))
    interleaved = np.empty(n * 2, dtype=np.float64)
    interleaved[0::2] = L[:n]
    interleaved[1::2] = R[:n]
    pcm = (np.clip(interleaved, -1, 1) * 32767).astype('<i2')
    with wave.open(os.path.join(OUT, name + '.wav'), 'wb') as w:
        w.setnchannels(2)
        w.setsampwidth(2)
        w.setframerate(RATE)
        w.writeframes(pcm.tobytes())

def hz(m):
    return 440.0 * 2 ** ((m - 69) / 12.0)

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
# Arcade / Switch-fighter stereo engine: drums, bass, pads, lead phrases,
# counters, stereo FX, soft limiter. 16-bar A/A/B/A' loops by default.

def adsr(n, a=0.01, d=0.08, s=0.65, r=0.12):
    """ADSR envelope over n samples. Times are seconds."""
    env = np.zeros(n)
    na, nd, nr = int(a * RATE), int(d * RATE), int(r * RATE)
    na, nd, nr = max(1, na), max(1, nd), max(1, nr)
    i = 0
    if i < n:
        end = min(n, na)
        env[i:end] = np.linspace(0, 1, end - i, endpoint=False)
        i = end
    if i < n:
        end = min(n, i + nd)
        env[i:end] = np.linspace(1, s, end - i, endpoint=False)
        i = end
    sustain_end = max(i, n - nr)
    if i < sustain_end:
        env[i:sustain_end] = s
        i = sustain_end
    if i < n:
        env[i:] = np.linspace(s if i > 0 else 0, 0, n - i)
    return env


def osc(kind, f, d, duty=0.5):
    """Soft anti-aliased oscillator for music voices."""
    n = max(1, int(RATE * d))
    t = np.arange(n) / RATE
    ph = (f * t) % 1.0
    if kind == 'sin':
        return np.sin(2 * np.pi * ph)
    if kind == 'tri':
        return 4 * np.abs(ph - 0.5) - 1
    if kind == 'saw':
        y = 2 * ph - 1
        return soft_lp(y, min(5500.0, max(1200.0, f * 6)))
    if kind == 'sq':
        y = np.where(ph < duty, 1.0, -1.0)
        return soft_lp(y, min(6000.0, max(1400.0, f * 7)))
    if kind == 'pulse':
        y = np.where(ph < duty, 1.0, -1.0)
        return soft_lp(y, min(5000.0, max(1000.0, f * 5)))
    return np.sin(2 * np.pi * ph)


def put(buf, x, start):
    start = int(start * RATE)
    n = min(len(x), len(buf) - start)
    if n > 0:
        buf[start:start + n] += x[:n]


def put_stereo(L, R, xL, xR, start):
    put(L, xL, start)
    put(R, xR, start)


def put_pan(L, R, x, start, pan=0.0):
    """pan -1..1 (L..R). Equal-power."""
    pan = float(np.clip(pan, -1, 1))
    ang = (pan + 1) * 0.25 * np.pi
    put_stereo(L, R, x * np.cos(ang), x * np.sin(ang), start)


# ---- drums ----------------------------------------------------------

def drum_kick():
    d = 0.22
    t = tt(d)
    body = sweep(150, 38, d) * np.exp(-t * 14)
    click = soft_lp(noise(0.02), 4000) * np.exp(-tt(0.02) * 180) * 0.55
    sub = np.sin(2 * np.pi * 48 * t) * np.exp(-t * 10) * 0.7
    y = np.tanh(2.4 * (body + sub))
    y[:len(click)] += click
    return fade(y, 0.001, 0.03)


def drum_snare():
    d = 0.16
    t = tt(d)
    body = sweep(240, 140, d) * np.exp(-t * 28) * 0.45
    crack = highpass(noise(d), 0.12) * np.exp(-t * 22)
    return fade(np.tanh(1.8 * (body + crack)), 0.001, 0.04)


def drum_clap():
    d = 0.14
    t = tt(d)
    bursts = np.zeros(int(RATE * d))
    for off in (0.0, 0.011, 0.019):
        o = int(off * RATE)
        n = noise(0.04) * np.exp(-tt(0.04) * 55)
        bursts[o:o + len(n)] += n
    return fade(highpass(bursts, 0.08) * 0.9, 0.001, 0.03)


def drum_crack():
    d = 0.12
    t = tt(d)
    return fade((highpass(noise(d), 0.15) * 0.85 + 0.4 * sweep(400, 120, d) * np.exp(-t * 35)),
                0.001, 0.03)


def drum_hat(open_=False):
    d = 0.18 if open_ else 0.045
    t = tt(d)
    n = highpass(noise(d), 0.18)
    if open_:
        y = n * np.exp(-t * 12)
    else:
        y = n * np.exp(-t * 90)
    metallic = (np.sin(2 * np.pi * 6200 * t) + 0.5 * np.sin(2 * np.pi * 9100 * t)) * np.exp(-t * (40 if open_ else 110))
    return fade(y * 0.7 + metallic * 0.25, 0.001, 0.01)


def drum_sleigh(d=0.05):
    t = tt(d)
    y = (np.sin(2 * np.pi * 2400 * t) + 0.5 * np.sin(2 * np.pi * 3600 * t)) * np.exp(-t * 70)
    y += 0.35 * highpass(noise(d), 0.2) * np.exp(-t * 50)
    return y * 0.55


def drum_crash():
    d = 0.9
    t = tt(d)
    y = highpass(noise(d), 0.1) * np.exp(-t * 3.2)
    y += 0.2 * np.sin(2 * np.pi * 480 * t) * np.exp(-t * 5)
    return fade(y * 0.7, 0.002, 0.08)


def drum_tom(f0=180):
    d = 0.28
    t = tt(d)
    return fade(sweep(f0, f0 * 0.55, d) * np.exp(-t * 10), 0.002, 0.05)


SN_FNS = {
    'snare': drum_snare,
    'clap': drum_clap,
    'crack': drum_crack,
}


# ---- instruments ----------------------------------------------------

def bass_voice(midi, d, style='saw'):
    f = hz(midi)
    t = tt(d)
    if style == 'sq':
        y = osc('sq', f, d, 0.28) * 0.55 + osc('sin', f * 0.5, d) * 0.45
    elif style == 'tri':
        y = osc('tri', f, d) * 0.7 + osc('sin', f, d) * 0.3
    elif style == 'sin':
        y = osc('sin', f, d) * 0.85 + osc('sin', f * 2, d) * 0.15
    else:  # saw
        y = osc('saw', f, d) * 0.65 + osc('sin', f * 0.5, d) * 0.4
    bright = soft_lp(y, 2400.0)
    return bright * adsr(len(y), 0.005, 0.06, 0.7, min(0.1, d * 0.35))


def pad_voice(midis, d, style='tri'):
    """Detuned stereo pad; returns (L, R)."""
    n = max(1, int(RATE * d))
    L = np.zeros(n)
    R = np.zeros(n)
    env = adsr(n, 0.08, 0.2, 0.85, 0.25)
    for i, m in enumerate(midis):
        f = hz(m)
        det = 1.0 + (0.003 if i % 2 == 0 else -0.003)
        if style == 'saw':
            a = osc('saw', f, d) * 0.35 + osc('saw', f * det, d) * 0.35
        elif style == 'sin':
            a = osc('sin', f, d) * 0.55 + osc('sin', f * det, d) * 0.35
        else:
            a = osc('tri', f, d) * 0.45 + osc('sin', f * det, d) * 0.35
        a = soft_lp(a, 2800) * env
        pan = -0.55 + 1.1 * (i / max(1, len(midis) - 1))
        ang = (pan + 1) * 0.25 * np.pi
        L += a * np.cos(ang)
        R += a * np.sin(ang)
    return L * 0.55, R * 0.55


def lead_voice(midi, d, style='sq', vibrato=5.5):
    f0 = hz(midi)
    n = max(1, int(RATE * d))
    t = np.arange(n) / RATE
    f = f0 * (1 + 0.008 * np.sin(2 * np.pi * vibrato * t))
    ph = np.cumsum(f) / RATE
    if style == 'saw':
        y = soft_lp(2 * (ph % 1) - 1, min(7000, f0 * 8))
    elif style == 'tri':
        y = 4 * np.abs((ph % 1) - 0.5) - 1
    elif style == 'sin':
        y = np.sin(2 * np.pi * ph)
    else:
        y = soft_lp(np.where((ph % 1) < 0.3, 1.0, -1.0), min(6500, f0 * 7))
        y = 0.7 * y + 0.3 * np.sin(2 * np.pi * ph)
    return y * adsr(n, 0.008, 0.05, 0.55, min(0.08, d * 0.3))


def pluck_voice(midi, d, style='tri'):
    y = osc(style if style in ('sin', 'tri', 'saw', 'sq') else 'tri', hz(midi), d)
    return y * adsr(len(y), 0.002, 0.04, 0.2, min(0.06, d * 0.4))


# ---- FX / master ----------------------------------------------------

def pingpong_delay(L, R, delay_s, mix=0.22, feedback=0.35):
    """Vectorized stereo ping-pong (2 taps)."""
    d = int(delay_s * RATE)
    if d < 1 or mix <= 0:
        return L, R
    outL, outR = L.copy(), R.copy()
    outR[d:] += mix * L[:-d]
    outL[d:] += mix * R[:-d]
    d2 = d * 2
    if feedback > 0 and d2 < len(L):
        outR[d2:] += mix * feedback * L[:-d2]
        outL[d2:] += mix * feedback * R[:-d2]
    return outL, outR


def comb_reverb(L, R, mix=0.18):
    """Short stereo reverb via multi-tap delays (vectorized)."""
    if mix <= 0:
        return L, R
    wetL = np.zeros_like(L)
    wetR = np.zeros_like(R)
    for i, dly_s in enumerate((0.0297, 0.0371, 0.0411, 0.0437)):
        d = int(dly_s * RATE)
        tapL = np.zeros_like(L)
        tapR = np.zeros_like(R)
        tapL[d:] = L[:-d]
        tapR[d:] = R[:-d]
        d2 = min(d * 2, len(L) - 1)
        if d2 > 0:
            tapL[d2:] += 0.45 * L[:-d2]
            tapR[d2:] += 0.45 * R[:-d2]
        if i % 2:
            wetL += tapR
            wetR += tapL
        else:
            wetL += tapL
            wetR += tapR
    wetL /= 4.0
    wetR /= 4.0
    return L * (1 - mix) + wetL * mix, R * (1 - mix) + wetR * mix


def soft_limit(L, R, drive=1.35, ceiling=0.92):
    L = np.tanh(L * drive)
    R = np.tanh(R * drive)
    peak = max(np.max(np.abs(L)), np.max(np.abs(R)), 1e-9)
    g = ceiling / peak if peak > ceiling else 1.0
    # seamless loop: short equal fade at both ends
    n = len(L)
    nf = min(n // 40, int(0.02 * RATE))
    if nf > 4:
        ramp = np.linspace(0, 1, nf)
        L[:nf] *= ramp
        R[:nf] *= ramp
        L[-nf:] *= ramp[::-1]
        R[-nf:] *= ramp[::-1]
    return L * g, R * g


def chord_notes(root, quality='maj'):
    if quality == 'min':
        return (root, root + 3, root + 7)
    if quality == 'dom7':
        return (root, root + 4, root + 7, root + 10)
    if quality == 'min7':
        return (root, root + 3, root + 7, root + 10)
    if quality == 'sus2':
        return (root, root + 2, root + 7)
    if quality == 'sus4':
        return (root, root + 5, root + 7)
    if quality == 'dim':
        return (root, root + 3, root + 6)
    if quality == 'add9':
        return (root, root + 4, root + 7, root + 14)
    return (root, root + 4, root + 7)


# ---- arrangement helpers --------------------------------------------

def parse_phrase(phrase, step):
    """phrase: list of (midi_or_None, steps). Returns list of (midi, start_beat, dur_beats)."""
    out = []
    t = 0.0
    for note, steps in phrase:
        dur = steps * step
        if note is not None:
            out.append((note, t, dur))
        t += dur
    return out


def render_theme(recipe, menu=False):
    """Render a stereo fight/menu theme from an expanded recipe. Returns (L, R)."""
    bpm = recipe['bpm']
    bars = recipe.get('bars', 16)
    beat = 60.0 / bpm
    total = bars * 4 * beat
    L = np.zeros(int(RATE * total))
    R = np.zeros(int(RATE * total))

    chords = recipe['chords']  # list of (root_midi, quality) length bars (or cycles)
    bass_roots = recipe['bass_roots']
    bass_pat = recipe.get('bass_pat', (1, 0, 1, 0, 1, 0, 1, 0))  # 8th notes
    kick_pat = recipe.get('kick_pat', (1, 0, 0, 0, 1, 0, 0, 0))  # 8th
    snare_pat = recipe.get('snare_pat', (0, 0, 0, 0, 1, 0, 0, 0))
    hat_pat = recipe.get('hat_pat', (1, 0, 1, 0, 1, 0, 1, 0))  # 8th; 2=open
    fill_bars = set(recipe.get('fill_bars', (7, 15)))

    bass_style = recipe.get('bass_style', 'saw')
    pad_style = recipe.get('pad_style', 'tri')
    lead_style = recipe.get('lead_style', 'sq')
    snare_fn = SN_FNS[recipe.get('snare_fn', 'snare')]
    hat_kind = recipe.get('hat_fn', 'hat')

    vols = recipe.get('vols', {})
    v_kick = vols.get('kick', 0.55)
    v_snare = vols.get('snare', 0.48)
    v_hat = vols.get('hat', 0.28)
    v_bass = vols.get('bass', 0.28)
    v_pad = vols.get('pad', 0.16)
    v_lead = vols.get('lead', 0.20)
    v_arp = vols.get('arp', 0.10)
    v_cnt = vols.get('counter', 0.12)

    lead_A = recipe.get('lead_A', [])
    lead_B = recipe.get('lead_B', lead_A)
    counter_B = recipe.get('counter_B', [])
    arp_pat = recipe.get('arp_pat', (0, 1, 2, 1))
    accent = recipe.get('accent')
    delay_beats = recipe.get('delay_beats', 0.75)
    delay_mix = recipe.get('delay_mix', 0.18)
    reverb_mix = recipe.get('reverb_mix', 0.16)
    width = recipe.get('width', 0.55)

    step16 = beat / 4

    for bar in range(bars):
        b0 = bar * 4 * beat
        section_B = (bars // 2 <= bar < 3 * bars // 4) if bars >= 8 else False
        # Arrangement intensity
        have_lead = bar >= bars // 4 or menu
        have_counter = section_B and not menu
        pad_boost = 1.15 if section_B else 1.0
        if menu:
            have_lead = bar >= 2
            have_counter = False

        cr, cq = chords[bar % len(chords)]
        notes = chord_notes(cr, cq)
        root = bass_roots[bar % len(bass_roots)]

        # Pad (whole bar)
        pL, pR = pad_voice(notes, 4 * beat * 0.98, pad_style)
        put_stereo(L, R, pL * v_pad * pad_boost, pR * v_pad * pad_boost, b0)

        # Bass — 8th grid
        for i, hit in enumerate(bass_pat):
            if not hit:
                continue
            # octave bump on some hits
            n = root + (12 if hit == 2 else 0)
            dur = beat / 2 * (0.92 if hit else 0)
            x = bass_voice(n, dur, bass_style) * v_bass
            put_pan(L, R, x, b0 + i * beat / 2, pan=-0.1)

        # Drums — 8th grid; fill on designated bars
        is_fill = bar in fill_bars
        for i in range(8):
            t0 = b0 + i * beat / 2
            k = kick_pat[i % len(kick_pat)]
            s = snare_pat[i % len(snare_pat)]
            h = hat_pat[i % len(hat_pat)]
            if is_fill and i >= 4:
                # tom / snare fill
                if i % 2 == 0:
                    put_pan(L, R, drum_tom(200 - 15 * (i - 4)) * v_snare * 0.7, t0, 0.2)
                else:
                    put_pan(L, R, snare_fn() * v_snare * 0.85, t0, 0.15)
                if i == 7:
                    put_pan(L, R, drum_crash() * 0.35, t0, 0.0)
                continue
            if k:
                put_pan(L, R, drum_kick() * v_kick, t0, 0.0)
            if s:
                put_pan(L, R, snare_fn() * v_snare, t0, 0.12)
            if h:
                if hat_kind == 'sleigh':
                    hh = drum_sleigh(0.06 if h == 2 else 0.04)
                else:
                    hh = drum_hat(open_=(h == 2))
                put_pan(L, R, hh * v_hat, t0, -0.25 if i % 2 == 0 else 0.25)

        # Crash on section boundaries
        if bar in (0, bars // 2, 3 * bars // 4) and not menu:
            put_pan(L, R, drum_crash() * 0.28, b0, 0.0)
        if menu and bar == 0:
            put_pan(L, R, drum_crash() * 0.18, b0, 0.0)

        # Arp under the pad
        if arp_pat and v_arp > 0:
            for i in range(16):
                n = notes[arp_pat[i % len(arp_pat)] % len(notes)] + (12 if recipe.get('arp_up') else 0)
                dur = step16 * 0.9
                x = pluck_voice(n, dur, recipe.get('arp_style', 'tri')) * v_arp
                pan = -width + 2 * width * (i % 4) / 3
                put_pan(L, R, x, b0 + i * step16, pan)

        # Lead phrases (A vs B). Long phrases span multiple bars — trigger on phrase boundary.
        if have_lead:
            phrase = lead_B if section_B else lead_A
            phrase_len_s = sum(st * step16 for _, st in phrase) if phrase else 0.0
            phrase_bars = max(1, int(round(phrase_len_s / (4 * beat)))) if phrase_len_s else 1
            if bar % phrase_bars == 0:
                for note, start_s, dur_s in parse_phrase(phrase, step16):
                    x = lead_voice(note, max(0.05, dur_s * 0.95), lead_style) * v_lead
                    put_pan(L, R, x, b0 + start_s, 0.38)
            if have_counter and counter_B:
                c_len = sum(st * step16 for _, st in counter_B)
                c_bars = max(1, int(round(c_len / (4 * beat)))) if c_len else 1
                if bar % c_bars == 0:
                    for note, start_s, dur_s in parse_phrase(counter_B, step16):
                        x = lead_voice(note, max(0.05, dur_s * 0.95),
                                       recipe.get('counter_style', 'sin')) * v_cnt
                        put_pan(L, R, x, b0 + start_s, -0.45)

        # Accents
        if accent == 'toy' and bar % 2 == 1:
            dur = beat / 4 * 0.8
            x = osc('sq', hz(84), dur, 0.2) * np.exp(-tt(dur) * 12) * 0.16
            put_pan(L, R, x, b0 + 3.5 * beat, 0.5)
        elif accent == 'ufo':
            d = 2 * beat
            t = tt(d)
            f = hz(chords[bar % len(chords)][0] + 24) * (1 + 0.08 * np.sin(2 * np.pi * 3 * t))
            ph = np.cumsum(f) / RATE
            wob = np.sin(2 * np.pi * ph) * np.sin(np.pi * t / d) * 0.12
            put_pan(L, R, wob, b0 + beat, -0.3)
        elif accent == 'fanfare' and bar % 4 == 0:
            for j, m in enumerate(notes[:3]):
                dur = beat * 0.4
                x = osc('sq', hz(m + 12), dur, 0.4) * adsr(int(RATE * dur), 0.01, 0.05, 0.5, 0.1) * 0.1
                put_pan(L, R, x, b0 + j * beat * 0.12, 0.2 * j - 0.2)
        elif accent == 'metal' and bar % 2 == 0:
            y = highpass(noise(0.07), 0.2) * np.exp(-tt(0.07) * 60) * 0.18
            put_pan(L, R, y, b0 + 1.5 * beat, 0.4)
        elif accent == 'machine':
            for i in range(4):
                put_pan(L, R, drum_hat(False) * 0.12, b0 + i * beat + beat * 0.75, 0.3)

    # Master bus
    L, R = pingpong_delay(L, R, delay_beats * beat, mix=delay_mix, feedback=0.32)
    L, R = comb_reverb(L, R, mix=reverb_mix)
    L, R = soft_limit(L, R, drive=1.25, ceiling=0.90)
    return L, R


# Phrase helpers: 16th-note steps. None = rest.
def ph(*pairs):
    return list(pairs)


# Order matches chars[] in config.h (plus stadium first).
FIGHT_TRACKS = (
    'stadium', 'slurpy', 'alien', 'rump', 'jv', 'kammy',
    'flusk', 'stephen', 'lopicardo', 'mike', 'krampus', 'kevkev', 'rose', 'toadie',
    'popearoni', 'pooh', 'santa',
)

# Expanded arcade recipes: 16-bar forms, real phrases, stereo FX.
FIGHT_RECIPES = {
    'stadium': dict(
        bpm=150, bars=16,
        chords=[(57, 'min'), (53, 'maj'), (55, 'maj'), (57, 'min'),
                (57, 'min'), (53, 'maj'), (50, 'maj'), (55, 'maj'),
                (57, 'min7'), (53, 'maj'), (55, 'dom7'), (57, 'min'),
                (57, 'min'), (53, 'maj'), (55, 'maj'), (57, 'min')],
        bass_roots=[45, 41, 43, 45, 45, 41, 38, 43, 45, 41, 43, 45, 45, 41, 43, 45],
        bass_pat=(1, 0, 1, 0, 1, 0, 2, 0),
        kick_pat=(1, 0, 0, 0, 1, 0, 0, 1),
        snare_pat=(0, 0, 1, 0, 0, 0, 1, 0),
        hat_pat=(1, 1, 1, 1, 1, 1, 1, 2),
        bass_style='sq', pad_style='saw', lead_style='sq', snare_fn='snare',
        arp_pat=(0, 1, 2, 1), arp_up=1, arp_style='sq',
        lead_A=ph((69, 2), (72, 2), (76, 4), (74, 2), (72, 2), (69, 4), (None, 4), (67, 4), (69, 8)),
        lead_B=ph((76, 2), (74, 2), (72, 2), (74, 2), (76, 4), (79, 4), (76, 4), (74, 2), (72, 2), (69, 8)),
        counter_B=ph((57, 4), (60, 4), (62, 4), (60, 4), (57, 8), (55, 8)),
        vols=dict(kick=0.58, snare=0.50, hat=0.26, bass=0.30, pad=0.14, lead=0.20, arp=0.08, counter=0.10),
        delay_beats=0.75, delay_mix=0.16, reverb_mix=0.14, accent=None,
    ),
    'slurpy': dict(
        bpm=132, bars=16,
        chords=[(60, 'maj'), (65, 'maj'), (62, 'min'), (67, 'maj'),
                (60, 'maj'), (65, 'maj'), (62, 'min'), (60, 'maj'),
                (60, 'add9'), (65, 'maj'), (67, 'maj'), (65, 'maj'),
                (60, 'maj'), (65, 'maj'), (62, 'min'), (60, 'maj')],
        bass_roots=[48, 53, 50, 55, 48, 53, 50, 48, 48, 53, 55, 53, 48, 53, 50, 48],
        bass_pat=(1, 0, 1, 1, 0, 1, 0, 1),
        kick_pat=(1, 0, 0, 1, 1, 0, 0, 0),
        snare_pat=(0, 0, 1, 0, 0, 0, 1, 0),
        hat_pat=(1, 0, 1, 0, 1, 0, 1, 0),
        bass_style='tri', pad_style='tri', lead_style='tri', snare_fn='snare',
        arp_pat=(0, 1, 2, 1, 0, 2, 1, 2), arp_up=1, arp_style='tri',
        lead_A=ph((72, 2), (74, 2), (76, 4), (74, 2), (72, 2), (69, 4), (None, 2), (67, 2), (69, 4), (72, 8)),
        lead_B=ph((76, 2), (79, 2), (81, 4), (79, 2), (76, 2), (74, 4), (72, 4), (74, 4), (76, 8)),
        counter_B=ph((64, 4), (65, 4), (67, 4), (65, 4), (64, 8), (60, 8)),
        vols=dict(kick=0.42, snare=0.40, hat=0.24, bass=0.24, pad=0.18, lead=0.22, arp=0.12, counter=0.10),
        delay_beats=0.5, delay_mix=0.20, reverb_mix=0.18, accent='toy',
    ),
    'alien': dict(
        bpm=128, bars=16,
        chords=[(57, 'sus2'), (59, 'sus2'), (61, 'sus2'), (63, 'sus2'),
                (57, 'sus2'), (59, 'sus2'), (64, 'maj'), (57, 'min'),
                (57, 'sus2'), (61, 'sus2'), (63, 'dim'), (59, 'sus2'),
                (57, 'sus2'), (59, 'sus2'), (64, 'maj'), (57, 'min')],
        bass_roots=[45, 47, 49, 51, 45, 47, 52, 45, 45, 49, 51, 47, 45, 47, 52, 45],
        bass_pat=(1, 0, 0, 1, 0, 0, 1, 0),
        kick_pat=(1, 0, 0, 0, 0, 0, 1, 0),
        snare_pat=(0, 0, 0, 0, 1, 0, 0, 0),
        hat_pat=(1, 1, 1, 1, 1, 1, 1, 1),
        bass_style='sin', pad_style='sin', lead_style='sin', snare_fn='snare',
        arp_pat=(0, 2, 1, 2), arp_up=1, arp_style='sin',
        lead_A=ph((69, 4), (73, 4), (76, 4), (73, 4), (71, 4), (69, 4), (None, 4), (76, 8)),
        lead_B=ph((81, 2), (80, 2), (78, 4), (76, 4), (78, 4), (81, 8), (78, 4), (76, 8)),
        counter_B=ph((57, 8), (61, 8), (59, 8), (57, 8)),
        vols=dict(kick=0.40, snare=0.32, hat=0.18, bass=0.28, pad=0.20, lead=0.18, arp=0.10, counter=0.12),
        delay_beats=1.0, delay_mix=0.28, reverb_mix=0.24, accent='ufo',
    ),
    'rump': dict(
        bpm=140, bars=16,
        chords=[(60, 'maj'), (60, 'maj'), (67, 'maj'), (65, 'maj'),
                (60, 'maj'), (60, 'maj'), (67, 'maj'), (60, 'maj'),
                (60, 'dom7'), (65, 'maj'), (67, 'maj'), (65, 'maj'),
                (60, 'maj'), (60, 'maj'), (67, 'maj'), (60, 'maj')],
        bass_roots=[48, 48, 55, 53, 48, 48, 55, 48, 48, 53, 55, 53, 48, 48, 55, 48],
        bass_pat=(1, 0, 0, 0, 1, 0, 1, 0),
        kick_pat=(1, 0, 1, 0, 1, 0, 1, 0),
        snare_pat=(0, 0, 1, 0, 0, 0, 1, 0),
        hat_pat=(0, 1, 0, 1, 0, 1, 0, 1),
        bass_style='sq', pad_style='tri', lead_style='sq', snare_fn='snare',
        arp_pat=(0, 0, 1, 2), arp_up=0, arp_style='sq',
        lead_A=ph((72, 4), (72, 2), (76, 2), (79, 8), (76, 4), (74, 4), (72, 8)),
        lead_B=ph((79, 4), (81, 4), (79, 2), (76, 2), (74, 4), (76, 4), (72, 4), (67, 4), (72, 8)),
        counter_B=ph((60, 4), (64, 4), (67, 4), (64, 4), (60, 8), (55, 8)),
        vols=dict(kick=0.52, snare=0.62, hat=0.22, bass=0.26, pad=0.15, lead=0.20, arp=0.07, counter=0.10),
        delay_beats=0.5, delay_mix=0.12, reverb_mix=0.12, accent='fanfare',
    ),
    'jv': dict(
        bpm=138, bars=16,
        chords=[(57, 'maj'), (62, 'maj'), (57, 'maj'), (64, 'maj'),
                (57, 'maj'), (62, 'maj'), (55, 'maj'), (57, 'maj'),
                (57, 'add9'), (62, 'maj'), (64, 'dom7'), (62, 'maj'),
                (57, 'maj'), (62, 'maj'), (55, 'maj'), (57, 'maj')],
        bass_roots=[45, 50, 45, 52, 45, 50, 43, 45, 45, 50, 52, 50, 45, 50, 43, 45],
        bass_pat=(1, 0, 1, 0, 1, 1, 0, 1),
        kick_pat=(1, 0, 0, 0, 1, 0, 0, 0),
        snare_pat=(0, 0, 1, 0, 0, 0, 1, 0),
        hat_pat=(1, 0, 1, 0, 1, 0, 1, 0),
        bass_style='saw', pad_style='tri', lead_style='saw', snare_fn='snare',
        arp_pat=(0, 1, 0, 2, 1, 0, 2, 1), arp_up=0, arp_style='saw',
        lead_A=ph((69, 2), (72, 2), (74, 4), (72, 2), (69, 2), (67, 4), (69, 4), (72, 4), (74, 8)),
        lead_B=ph((74, 2), (76, 2), (77, 4), (76, 2), (74, 2), (72, 4), (69, 4), (67, 4), (69, 8)),
        counter_B=ph((57, 4), (59, 4), (60, 4), (62, 4), (57, 8), (55, 8)),
        vols=dict(kick=0.56, snare=0.46, hat=0.26, bass=0.26, pad=0.14, lead=0.18, arp=0.09, counter=0.10),
        delay_beats=0.75, delay_mix=0.14, reverb_mix=0.12, accent=None,
    ),
    'kammy': dict(
        bpm=120, bars=16,
        chords=[(52, 'min'), (52, 'min'), (50, 'min'), (52, 'min'),
                (52, 'min'), (52, 'min'), (50, 'min'), (48, 'maj'),
                (52, 'min7'), (50, 'min'), (48, 'maj'), (50, 'min'),
                (52, 'min'), (52, 'min'), (50, 'min'), (52, 'min')],
        bass_roots=[40, 40, 38, 40, 40, 40, 38, 36, 40, 38, 36, 38, 40, 40, 38, 40],
        bass_pat=(1, 1, 1, 1, 1, 1, 1, 1),
        kick_pat=(1, 1, 1, 1, 1, 1, 1, 1),
        snare_pat=(0, 0, 1, 0, 0, 0, 1, 0),
        hat_pat=(1, 0, 0, 0, 1, 0, 0, 0),
        bass_style='saw', pad_style='saw', lead_style='sq', snare_fn='clap',
        arp_pat=(0, 0, 1, 0), arp_up=0, arp_style='sq',
        lead_A=ph((64, 4), (None, 2), (64, 2), (67, 4), (64, 4), (62, 4), (64, 8), (None, 4)),
        lead_B=ph((67, 2), (64, 2), (62, 4), (60, 4), (62, 4), (64, 8), (67, 4), (64, 8)),
        counter_B=ph((52, 8), (50, 8), (48, 8), (50, 8)),
        vols=dict(kick=0.68, snare=0.50, hat=0.16, bass=0.34, pad=0.12, lead=0.14, arp=0.05, counter=0.10),
        delay_beats=0.5, delay_mix=0.10, reverb_mix=0.10, accent=None,
    ),
    'flusk': dict(
        bpm=145, bars=16,
        chords=[(60, 'maj'), (58, 'maj'), (63, 'min'), (60, 'maj'),
                (62, 'min'), (58, 'maj'), (65, 'maj'), (60, 'maj'),
                (60, 'sus4'), (58, 'maj'), (63, 'min'), (62, 'min'),
                (60, 'maj'), (58, 'maj'), (65, 'maj'), (60, 'maj')],
        bass_roots=[48, 46, 51, 48, 50, 46, 53, 48, 48, 46, 51, 50, 48, 46, 53, 48],
        bass_pat=(1, 0, 1, 0, 0, 1, 0, 1),
        kick_pat=(1, 0, 0, 0, 1, 0, 0, 0),
        snare_pat=(0, 0, 1, 0, 0, 0, 1, 0),
        hat_pat=(1, 1, 1, 1, 1, 1, 1, 1),
        bass_style='sq', pad_style='tri', lead_style='sq', snare_fn='snare',
        arp_pat=(0, 2, 1, 0, 2, 1, 2, 0), arp_up=1, arp_style='sq',
        lead_A=ph((72, 2), (70, 2), (72, 2), (75, 2), (72, 4), (67, 4), (None, 2), (70, 2), (72, 8)),
        lead_B=ph((75, 2), (77, 2), (75, 2), (72, 2), (70, 4), (72, 4), (75, 4), (77, 4), (75, 8)),
        counter_B=ph((60, 4), (58, 4), (63, 4), (60, 4), (62, 8), (58, 8)),
        vols=dict(kick=0.48, snare=0.44, hat=0.30, bass=0.26, pad=0.15, lead=0.19, arp=0.11, counter=0.10),
        delay_beats=0.75, delay_mix=0.22, reverb_mix=0.16, accent=None,
    ),
    'stephen': dict(
        bpm=136, bars=16,
        chords=[(60, 'maj'), (64, 'min'), (65, 'maj'), (67, 'maj'),
                (60, 'maj'), (64, 'min'), (62, 'min'), (60, 'maj'),
                (60, 'add9'), (65, 'maj'), (67, 'maj'), (64, 'min'),
                (60, 'maj'), (64, 'min'), (62, 'min'), (60, 'maj')],
        bass_roots=[48, 52, 53, 55, 48, 52, 50, 48, 48, 53, 55, 52, 48, 52, 50, 48],
        bass_pat=(1, 0, 1, 0, 1, 0, 1, 0),
        kick_pat=(1, 0, 0, 0, 1, 0, 0, 0),
        snare_pat=(0, 0, 1, 0, 0, 0, 1, 0),
        hat_pat=(1, 0, 1, 0, 1, 0, 1, 0),
        bass_style='tri', pad_style='tri', lead_style='tri', snare_fn='crack',
        arp_pat=(0, 1, 2, 1), arp_up=1, arp_style='tri',
        lead_A=ph((72, 2), (76, 2), (77, 4), (76, 2), (72, 2), (74, 4), (72, 4), (67, 4), (72, 8)),
        lead_B=ph((79, 2), (77, 2), (76, 4), (74, 4), (72, 4), (74, 4), (76, 4), (79, 4), (76, 8)),
        counter_B=ph((60, 4), (64, 4), (65, 4), (67, 4), (60, 8), (55, 8)),
        vols=dict(kick=0.50, snare=0.52, hat=0.24, bass=0.24, pad=0.16, lead=0.20, arp=0.10, counter=0.10),
        delay_beats=0.5, delay_mix=0.14, reverb_mix=0.14, accent=None,
    ),
    'lopicardo': dict(
        bpm=155, bars=16,
        chords=[(57, 'min'), (57, 'min'), (60, 'maj'), (57, 'min'),
                (55, 'maj'), (57, 'min'), (60, 'maj'), (57, 'min'),
                (57, 'min7'), (60, 'maj'), (55, 'maj'), (57, 'min'),
                (57, 'min'), (60, 'maj'), (55, 'maj'), (57, 'min')],
        bass_roots=[45, 45, 48, 45, 43, 45, 48, 45, 45, 48, 43, 45, 45, 48, 43, 45],
        bass_pat=(1, 0, 1, 0, 1, 0, 1, 0),
        kick_pat=(1, 0, 0, 0, 1, 0, 0, 0),
        snare_pat=(0, 0, 1, 0, 0, 0, 1, 0),
        hat_pat=(1, 1, 1, 1, 1, 1, 1, 1),
        bass_style='sq', pad_style='saw', lead_style='sq', snare_fn='snare',
        arp_pat=(0, 1, 2, 0, 1, 2, 0, 1), arp_up=0, arp_style='sq',
        lead_A=ph((69, 2), (72, 2), (76, 2), (72, 2), (69, 4), (67, 4), (69, 4), (72, 4), (76, 8)),
        lead_B=ph((76, 2), (79, 2), (76, 2), (72, 2), (74, 4), (76, 4), (72, 4), (69, 4), (67, 8)),
        counter_B=ph((57, 4), (60, 4), (57, 4), (55, 4), (57, 8), (48, 8)),
        vols=dict(kick=0.52, snare=0.46, hat=0.34, bass=0.28, pad=0.13, lead=0.18, arp=0.10, counter=0.09),
        delay_beats=0.75, delay_mix=0.18, reverb_mix=0.14, accent='metal',
    ),
    'mike': dict(
        bpm=148, bars=16,
        chords=[(59, 'min'), (62, 'maj'), (64, 'min'), (62, 'maj'),
                (59, 'min'), (57, 'maj'), (55, 'maj'), (59, 'min'),
                (59, 'min7'), (62, 'maj'), (64, 'min'), (62, 'dom7'),
                (59, 'min'), (57, 'maj'), (55, 'maj'), (59, 'min')],
        bass_roots=[47, 50, 52, 50, 47, 45, 43, 47, 47, 50, 52, 50, 47, 45, 43, 47],
        bass_pat=(1, 0, 1, 0, 1, 0, 2, 0),
        kick_pat=(1, 0, 0, 0, 1, 0, 0, 0),
        snare_pat=(0, 0, 1, 0, 0, 0, 1, 0),
        hat_pat=(1, 1, 1, 1, 1, 1, 1, 1),
        bass_style='saw', pad_style='tri', lead_style='sq', snare_fn='snare',
        arp_pat=(0, 1, 2, 1, 2, 0, 1, 2), arp_up=1, arp_style='sq',
        lead_A=ph((71, 2), (74, 2), (78, 4), (76, 2), (74, 2), (71, 4), (69, 4), (71, 4), (74, 8)),
        lead_B=ph((78, 2), (81, 2), (78, 2), (74, 2), (76, 4), (78, 4), (74, 4), (71, 4), (69, 8)),
        counter_B=ph((59, 4), (62, 4), (64, 4), (62, 4), (59, 8), (55, 8)),
        vols=dict(kick=0.50, snare=0.46, hat=0.36, bass=0.26, pad=0.14, lead=0.19, arp=0.11, counter=0.10),
        delay_beats=0.75, delay_mix=0.16, reverb_mix=0.14, accent='machine',
    ),
    'krampus': dict(
        bpm=112, bars=16,
        chords=[(52, 'min'), (55, 'maj'), (50, 'maj'), (52, 'min'),
                (52, 'min'), (55, 'maj'), (48, 'maj'), (52, 'min'),
                (52, 'min7'), (55, 'maj'), (50, 'maj'), (48, 'maj'),
                (52, 'min'), (55, 'maj'), (48, 'maj'), (52, 'min')],
        bass_roots=[40, 43, 38, 40, 40, 43, 36, 40, 40, 43, 38, 36, 40, 43, 36, 40],
        bass_pat=(1, 0, 0, 1, 0, 0, 1, 0),
        kick_pat=(1, 0, 0, 0, 1, 0, 0, 0),
        snare_pat=(0, 0, 0, 0, 1, 0, 0, 0),
        hat_pat=(1, 0, 0, 0, 1, 0, 0, 0),
        bass_style='saw', pad_style='sin', lead_style='sin', snare_fn='snare', hat_fn='sleigh',
        arp_pat=(0, 1, 0, 2), arp_up=0, arp_style='sin',
        lead_A=ph((64, 4), (67, 4), (71, 4), (67, 4), (64, 8), (62, 4), (64, 8)),
        lead_B=ph((71, 4), (74, 4), (71, 4), (67, 4), (64, 4), (62, 4), (60, 4), (64, 8)),
        counter_B=ph((52, 8), (55, 8), (50, 8), (52, 8)),
        vols=dict(kick=0.60, snare=0.38, hat=0.28, bass=0.32, pad=0.18, lead=0.16, arp=0.08, counter=0.12),
        delay_beats=1.0, delay_mix=0.20, reverb_mix=0.22, accent=None,
    ),
    'kevkev': dict(
        bpm=128, bars=16,
        chords=[(57, 'maj'), (60, 'maj'), (55, 'maj'), (57, 'maj'),
                (53, 'maj'), (57, 'maj'), (60, 'maj'), (57, 'maj'),
                (57, 'add9'), (60, 'maj'), (55, 'maj'), (53, 'maj'),
                (57, 'maj'), (60, 'maj'), (55, 'maj'), (57, 'maj')],
        bass_roots=[45, 48, 43, 45, 41, 45, 48, 45, 45, 48, 43, 41, 45, 48, 43, 45],
        bass_pat=(1, 0, 1, 0, 1, 0, 1, 0),
        kick_pat=(1, 0, 0, 1, 1, 0, 0, 0),
        snare_pat=(0, 0, 1, 0, 0, 0, 1, 0),
        hat_pat=(1, 0, 1, 0, 1, 0, 1, 0),
        bass_style='tri', pad_style='tri', lead_style='sq', snare_fn='clap',
        arp_pat=(0, 2, 1, 2), arp_up=1, arp_style='tri',
        lead_A=ph((69, 2), (72, 2), (76, 4), (74, 2), (72, 2), (69, 4), (67, 4), (69, 4), (72, 8)),
        lead_B=ph((76, 2), (79, 2), (76, 2), (72, 2), (74, 4), (76, 4), (72, 4), (69, 4), (67, 8)),
        counter_B=ph((57, 4), (60, 4), (55, 4), (57, 4), (53, 8), (57, 8)),
        vols=dict(kick=0.56, snare=0.42, hat=0.22, bass=0.28, pad=0.16, lead=0.19, arp=0.09, counter=0.10),
        delay_beats=0.75, delay_mix=0.18, reverb_mix=0.16, accent=None,
    ),
    'rose': dict(  # crystalline ice waltz / sparkling arps
        bpm=134, bars=16,
        chords=[(61, 'maj'), (58, 'min'), (56, 'maj'), (63, 'maj'),
                (61, 'maj'), (58, 'min'), (56, 'maj'), (61, 'maj'),
                (61, 'add9'), (58, 'min7'), (56, 'maj'), (63, 'dom7'),
                (61, 'maj'), (58, 'min'), (56, 'maj'), (61, 'maj')],
        bass_roots=[49, 46, 44, 51, 49, 46, 44, 49, 49, 46, 44, 51, 49, 46, 44, 49],
        bass_pat=(1, 0, 0, 1, 0, 1, 0, 0),
        kick_pat=(1, 0, 0, 0, 1, 0, 0, 0),
        snare_pat=(0, 0, 1, 0, 0, 0, 1, 0),
        hat_pat=(1, 0, 1, 1, 1, 0, 1, 0),
        bass_style='sin', pad_style='sin', lead_style='tri', snare_fn='clap',
        arp_pat=(0, 1, 2, 1, 2, 0, 1, 2), arp_up=1, arp_style='sin',
        lead_A=ph((73, 2), (76, 2), (80, 4), (78, 2), (76, 2), (73, 4), (71, 4), (73, 4), (76, 8)),
        lead_B=ph((80, 2), (83, 2), (80, 2), (76, 2), (78, 4), (80, 4), (76, 4), (73, 4), (71, 8)),
        counter_B=ph((61, 4), (64, 4), (68, 4), (64, 4), (61, 8), (56, 8)),
        vols=dict(kick=0.44, snare=0.38, hat=0.26, bass=0.24, pad=0.20, lead=0.18, arp=0.14, counter=0.11),
        delay_beats=1.0, delay_mix=0.24, reverb_mix=0.26, accent='toy',
    ),
    'toadie': dict(  # swamp bounce: rubbery bass, croak-y toy accents
        bpm=126, bars=16,
        chords=[(55, 'maj'), (62, 'min'), (57, 'maj'), (60, 'maj'),
                (55, 'maj'), (62, 'min'), (57, 'maj'), (55, 'maj'),
                (55, 'add9'), (62, 'min7'), (57, 'maj'), (60, 'dom7'),
                (55, 'maj'), (62, 'min'), (57, 'maj'), (55, 'maj')],
        bass_roots=[43, 50, 45, 48, 43, 50, 45, 43, 43, 50, 45, 48, 43, 50, 45, 43],
        bass_pat=(1, 0, 1, 0, 1, 1, 0, 1),
        kick_pat=(1, 0, 0, 1, 1, 0, 0, 0),
        snare_pat=(0, 0, 1, 0, 0, 0, 1, 0),
        hat_pat=(1, 0, 1, 0, 1, 0, 1, 0),
        bass_style='tri', pad_style='tri', lead_style='sq', snare_fn='clap',
        arp_pat=(0, 2, 1, 0, 2, 1, 2, 0), arp_up=0, arp_style='tri',
        lead_A=ph((67, 2), (70, 2), (74, 4), (72, 2), (70, 2), (67, 4), (65, 4), (67, 4), (70, 8)),
        lead_B=ph((74, 2), (77, 2), (74, 2), (70, 2), (72, 4), (74, 4), (70, 4), (67, 4), (65, 8)),
        counter_B=ph((55, 4), (58, 4), (62, 4), (58, 4), (55, 8), (50, 8)),
        vols=dict(kick=0.52, snare=0.40, hat=0.24, bass=0.28, pad=0.16, lead=0.17, arp=0.11, counter=0.10),
        delay_beats=0.75, delay_mix=0.16, reverb_mix=0.18, accent='toy',
    ),
    'popearoni': dict(  # solemn arcade hymn: organ pad, processional bass
        bpm=118, bars=16,
        chords=[(60, 'maj'), (57, 'min'), (53, 'maj'), (55, 'maj'),
                (60, 'maj'), (57, 'min'), (62, 'min'), (55, 'maj'),
                (60, 'add9'), (57, 'min7'), (53, 'maj'), (55, 'dom7'),
                (60, 'maj'), (65, 'maj'), (55, 'maj'), (60, 'maj')],
        bass_roots=[48, 45, 41, 43, 48, 45, 50, 43, 48, 45, 41, 43, 48, 53, 43, 48],
        bass_pat=(1, 0, 0, 1, 1, 0, 0, 0),
        kick_pat=(1, 0, 0, 0, 1, 0, 0, 1),
        snare_pat=(0, 0, 1, 0, 0, 0, 1, 0),
        hat_pat=(1, 0, 1, 0, 1, 0, 1, 0),
        bass_style='sin', pad_style='saw', lead_style='sin', snare_fn='snare',
        arp_pat=(0, 1, 2, 4, 2, 1, 0, 2), arp_up=1, arp_style='sin',
        lead_A=ph((72, 4), (76, 4), (79, 4), (76, 4), (74, 4), (72, 4), (67, 4), (72, 8)),
        lead_B=ph((79, 4), (81, 4), (79, 4), (76, 4), (74, 4), (72, 4), (69, 4), (67, 8)),
        counter_B=ph((60, 8), (57, 8), (53, 8), (55, 8)),
        vols=dict(kick=0.48, snare=0.36, hat=0.18, bass=0.26, pad=0.22, lead=0.16, arp=0.10, counter=0.12),
        delay_beats=1.0, delay_mix=0.20, reverb_mix=0.28, accent=None,
    ),
    'pooh': dict(  # honey-sweet bounce: warm pads, buzzy toy accents
        bpm=124, bars=16,
        chords=[(60, 'maj'), (65, 'maj'), (62, 'min'), (67, 'maj'),
                (60, 'maj'), (65, 'maj'), (57, 'min'), (55, 'maj'),
                (60, 'add9'), (65, 'maj'), (62, 'min7'), (67, 'dom7'),
                (60, 'maj'), (65, 'maj'), (55, 'maj'), (60, 'maj')],
        bass_roots=[48, 53, 50, 55, 48, 53, 45, 43, 48, 53, 50, 55, 48, 53, 43, 48],
        bass_pat=(1, 0, 1, 0, 1, 0, 0, 1),
        kick_pat=(1, 0, 0, 0, 1, 0, 0, 1),
        snare_pat=(0, 0, 1, 0, 0, 0, 1, 0),
        hat_pat=(1, 1, 0, 1, 1, 0, 1, 0),
        bass_style='tri', pad_style='sin', lead_style='sq', snare_fn='clap',
        arp_pat=(0, 2, 4, 2, 0, 2, 4, 5), arp_up=1, arp_style='tri',
        lead_A=ph((72, 2), (76, 2), (79, 4), (77, 2), (76, 2), (72, 4), (69, 4), (72, 8)),
        lead_B=ph((79, 2), (84, 2), (79, 2), (76, 2), (77, 4), (79, 4), (72, 4), (67, 8)),
        counter_B=ph((60, 4), (64, 4), (67, 4), (64, 4), (60, 8), (55, 8)),
        vols=dict(kick=0.50, snare=0.38, hat=0.22, bass=0.26, pad=0.18, lead=0.17, arp=0.12, counter=0.10),
        delay_beats=0.75, delay_mix=0.18, reverb_mix=0.20, accent='toy',
    ),
    'santa': dict(  # merry sleigh bounce with sleigh-bell hats
        bpm=128, bars=16,
        chords=[(60, 'maj'), (67, 'maj'), (65, 'maj'), (60, 'maj'),
                (60, 'maj'), (62, 'min'), (67, 'maj'), (60, 'maj'),
                (60, 'add9'), (65, 'maj'), (67, 'maj'), (64, 'min'),
                (60, 'maj'), (67, 'maj'), (65, 'maj'), (60, 'maj')],
        bass_roots=[48, 55, 53, 48, 48, 50, 55, 48, 48, 53, 55, 52, 48, 55, 53, 48],
        bass_pat=(1, 0, 1, 0, 1, 0, 2, 0),
        kick_pat=(1, 0, 0, 0, 1, 0, 0, 1),
        snare_pat=(0, 0, 1, 0, 0, 0, 1, 0),
        hat_pat=(1, 0, 1, 0, 1, 0, 1, 0),
        bass_style='tri', pad_style='sin', lead_style='sq', snare_fn='snare', hat_fn='sleigh',
        arp_pat=(0, 2, 4, 2, 0, 1, 2, 4), arp_up=1, arp_style='sin',
        lead_A=ph((72, 2), (76, 2), (79, 4), (76, 2), (72, 2), (67, 4), (69, 4), (72, 8)),
        lead_B=ph((79, 2), (84, 2), (79, 2), (76, 2), (77, 4), (79, 4), (72, 4), (67, 8)),
        counter_B=ph((60, 4), (64, 4), (67, 4), (64, 4), (60, 8), (55, 8)),
        vols=dict(kick=0.52, snare=0.40, hat=0.30, bass=0.28, pad=0.16, lead=0.18, arp=0.10, counter=0.10),
        delay_beats=0.75, delay_mix=0.16, reverb_mix=0.18, accent=None,
    ),
}


def music_fight_from(recipe):
    return render_theme(recipe, menu=False)


def music_menu():
    recipe = dict(
        bpm=96, bars=16,
        chords=[(60, 'maj'), (57, 'min'), (53, 'maj'), (55, 'maj'),
                (60, 'maj'), (57, 'min'), (53, 'maj'), (55, 'maj'),
                (60, 'add9'), (57, 'min7'), (53, 'maj'), (55, 'dom7'),
                (60, 'maj'), (57, 'min'), (53, 'maj'), (55, 'maj')],
        bass_roots=[48, 45, 41, 43, 48, 45, 41, 43, 48, 45, 41, 43, 48, 45, 41, 43],
        bass_pat=(1, 0, 0, 0, 1, 0, 0, 0),
        kick_pat=(1, 0, 0, 0, 0, 0, 1, 0),
        snare_pat=(0, 0, 0, 0, 0, 0, 0, 0),
        hat_pat=(0, 0, 1, 0, 0, 0, 1, 0),
        fill_bars=(),
        bass_style='sin', pad_style='sin', lead_style='sin', snare_fn='snare',
        arp_pat=(0, 1, 2, 1), arp_up=1, arp_style='sin',
        lead_A=ph((72, 4), (76, 4), (79, 4), (76, 4), (74, 4), (72, 4), (None, 4), (67, 4)),
        lead_B=ph((79, 4), (81, 4), (79, 4), (76, 4), (74, 8), (72, 8)),
        counter_B=[],
        vols=dict(kick=0.22, snare=0.0, hat=0.14, bass=0.22, pad=0.24, lead=0.16, arp=0.12, counter=0.0),
        delay_beats=1.0, delay_mix=0.22, reverb_mix=0.26, accent=None, width=0.7,
    )
    return render_theme(recipe, menu=True)


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
        print('rendering music_menu…')
        L, R = music_menu()
        save_music('music_menu', L, R)
        for name in FIGHT_TRACKS:
            print(f'rendering music_fight_{name}…')
            L, R = music_fight_from(FIGHT_RECIPES[name])
            save_music(f'music_fight_{name}', L, R)
            if name == 'stadium':
                save_music('music_fight', L, R)
    print('wrote', sorted(os.listdir(OUT)))


if __name__ == '__main__':
    main()
