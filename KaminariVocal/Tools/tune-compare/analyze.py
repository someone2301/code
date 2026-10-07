"""Pitch-curve comparison: dry vocal vs reference tuner renders vs Kaminari renders (needs numpy).

usage:
  python3 analyze.py dry.wav file1.wav [file2.wav ...]
      per file: voiced share, deviation from the nearest scale note, frame-to-frame jitter, offset to the dry file
  python3 analyze.py compare dry.wav reference.wav ours1.wav [ours2.wav ...] [--frames]
      roughness (YIN aperiodicity, voiced share of loud frames) and note agreement with the reference render;
      --frames lists the segments where ours lands on another note
The scale is G natural minor (set G_MINOR for other material).
"""
import sys, wave
import numpy as np

SR = 44100
HOP = 256
G_MINOR = {7, 9, 10, 0, 2, 3, 5}   # G A Bb C D Eb F


def load(path):
    w = wave.open(path)
    n, bw, ch = w.getnframes(), w.getsampwidth(), w.getnchannels()
    raw = w.readframes(n)
    if bw == 3:
        b = np.frombuffer(raw, dtype=np.uint8).reshape(-1, 3)
        v = (b[:, 0].astype(np.int32) | (b[:, 1].astype(np.int32) << 8) | (b[:, 2].astype(np.int32) << 16))
        v = np.where(v >= 1 << 23, v - (1 << 24), v).astype(np.float64) / 8388608.0
    else:
        v = np.frombuffer(raw, dtype=np.int16).astype(np.float64) / 32768.0
    if ch > 1:
        v = v.reshape(-1, ch).mean(axis=1)
    return v


def yin_track(x, fmin=70.0, fmax=1000.0, win=1536, thresh=0.12):
    """Frame-wise YIN f0 (Hz, 0 = unvoiced) every HOP samples; frame centred at k*HOP."""
    maxlag = int(SR / fmin) + 2
    minlag = int(SR / fmax)
    nfr = len(x) // HOP
    f0 = np.zeros(nfr)
    conf = np.ones(nfr)
    rms = np.zeros(nfr)
    pad = np.concatenate([np.zeros(win), x, np.zeros(win + maxlag)])
    for k in range(nfr):
        c = k * HOP + win
        seg = pad[c - win // 2: c - win // 2 + win + maxlag]
        a = seg[:win]
        rms[k] = np.sqrt(np.mean(a * a))
        if rms[k] < 1e-3:
            continue
        # difference function via FFT
        n = 1
        while n < 2 * (win + maxlag):
            n *= 2
        A = np.fft.rfft(seg, n)
        B = np.fft.rfft(a, n)
        corr = np.fft.irfft(np.conj(B) * A, n)[:maxlag + 1]
        e0 = np.sum(a * a)
        cs = np.concatenate([[0.0], np.cumsum(seg * seg)])
        et = cs[np.arange(maxlag + 1) + win] - cs[np.arange(maxlag + 1)]
        d = e0 + et - 2 * corr
        d[0] = 0
        cm = np.ones_like(d)
        run = np.cumsum(d[1:])
        cm[1:] = d[1:] * np.arange(1, len(d)) / np.maximum(run, 1e-12)
        tau = -1
        for t in range(minlag, maxlag):
            if cm[t] < thresh:
                while t + 1 < maxlag and cm[t + 1] < cm[t]:
                    t += 1
                tau = t
                break
        if tau < 0:
            continue
        if 1 <= tau < maxlag:
            a0, b0, c0 = d[tau - 1], d[tau], d[tau + 1]
            den = a0 - 2 * b0 + c0
            frac = 0.5 * (a0 - c0) / den if abs(den) > 1e-12 else 0.0
        else:
            frac = 0.0
        f0[k] = SR / (tau + np.clip(frac, -0.5, 0.5))
        conf[k] = cm[tau]
    return f0, conf, rms


def midi(f):
    return 69 + 12 * np.log2(f / 440.0)


def nearest_scale(m):
    base = np.round(m)
    best = np.full_like(m, np.nan)
    bestd = np.full_like(m, 99.0)
    for off in range(-2, 3):
        cand = base + off
        ok = np.isin(np.mod(cand, 12).astype(int), list(G_MINOR))
        d = np.abs(m - cand)
        upd = ok & (d < bestd)
        best[upd] = cand[upd]
        bestd[upd] = d[upd]
    return best


def latency(dry, y, maxlag=4096):
    """Offset of y against dry from their loudness envelopes (1 ms resolution, refined on the waveform)."""
    def env(z):
        h = 44
        n = len(z) // h
        return np.sqrt(np.mean(z[:n * h].reshape(n, h) ** 2, axis=1))
    a, b = env(dry), env(y)
    n = min(len(a), len(b))
    a, b = a[:n] - a[:n].mean(), b[:n] - b[:n].mean()
    best, bl = -1e9, 0
    for l in range(-maxlag // 44, maxlag // 44 + 1):
        if l >= 0:
            c = np.dot(a[:n - l], b[l:])
        else:
            c = np.dot(a[-l:], b[:n + l])
        if c > best:
            best, bl = c, l
    return bl * 44


def stats(name, f0, dryf0=None):
    v = f0 > 0
    m = midi(np.where(v, f0, 1.0))
    tgt = nearest_scale(m)
    dev = (m - tgt) * 100
    dv = dev[v]
    # steadiness: frame-to-frame jitter in cents (voiced runs)
    dm = np.diff(m) * 100
    vv = v[1:] & v[:-1]
    jit = np.median(np.abs(dm[vv & (np.abs(dm) < 50)]))
    out = f"{name:34s} voiced {100 * v.mean():4.1f}%  |dev from G-minor note| median {np.median(np.abs(dv)):5.1f} ct  p90 {np.percentile(np.abs(dv), 90):5.1f} ct  frame jitter {jit:4.2f} ct"
    return out, m, v




def compare(dryp, refp, others):
    """Note agreement with a reference render and roughness (YIN aperiodicity) per file."""
    dry = load(dryp)
    f0d, cd, rd = yin_track(dry)
    def prep(p):
        y = load(p)
        lag = latency(dry, y)
        y = y[lag:] if lag > 0 else np.concatenate([np.zeros(-lag), y])
        return yin_track(y)
    fr, cr, rr = prep(refp)
    mr = midi(np.where(fr > 0, fr, 1.0))
    def rough(f0, c, r):
        v = (f0 > 0)
        loud = r > np.percentile(r[r > 0], 30)
        return np.mean(c[v & loud]), 100 * np.mean(v[loud])
    a, vp = rough(fr, cr, rr)
    ad, vpd = rough(f0d, cd, rd)
    print(f"{'dry':40s} aperiodicity {ad:.3f}  voiced(loud) {vpd:4.1f}%")
    print(f"{'REF ' + refp.split('/')[-1]:40s} aperiodicity {a:.3f}  voiced(loud) {vp:4.1f}%")
    for p in others:
        f0, c, r = prep(p)
        m = midi(np.where(f0 > 0, f0, 1.0))
        n = min(len(m), len(mr))
        both = (f0[:n] > 0) & (fr[:n] > 0)
        d = (m[:n] - mr[:n]) * 100
        d = np.abs(np.remainder(d + 600, 1200) - 600)   # fold octave errors of the meter
        disagree = 100 * np.mean(d[both] > 50)
        close = np.median(d[both])
        a, vp = rough(f0, c, r)
        print(f"{p.split('/')[-1]:40s} aperiodicity {a:.3f}  voiced(loud) {vp:4.1f}%  | vs REF: other note {disagree:4.1f}% of frames, median diff {close:4.1f} ct")
        if "--frames" in sys.argv:
            t = np.arange(n) * HOP / SR
            bad = both & (d > 50)
            # group runs
            runs = []
            i = 0
            idx = np.where(bad)[0]
            if len(idx):
                start = prev = idx[0]
                for k in idx[1:]:
                    if k - prev > 3:
                        runs.append((start, prev)); start = k
                    prev = k
                runs.append((start, prev))
            for s0, s1 in runs[:40]:
                print(f"    {t[s0]:6.2f}-{t[s1]:6.2f}s  dry {mr[s0]*0 + midi(f0d[s0]) if f0d[s0] > 0 else 0:6.2f}  ref {mr[s0]:6.2f}  ours {m[s0]:6.2f}")


if __name__ == "__main__" and sys.argv[1] == "compare":
    args = [a for a in sys.argv[2:] if not a.startswith("--")]
    compare(args[0], args[1], args[2:])
elif __name__ == "__main__":
    dry = load(sys.argv[1])
    f0d, _, _ = yin_track(dry)
    s, md, vd = stats("dry", f0d)
    print(s)
    vals = f0d[f0d > 0]
    print(f"  dry range: {vals.min():.0f}-{vals.max():.0f} Hz (median {np.median(vals):.0f} Hz)")
    for p in sys.argv[2:]:
        y = load(p)
        lag = latency(dry, y)
        if lag > 0:
            y = y[lag:]
        elif lag < 0:
            y = np.concatenate([np.zeros(-lag), y])
        f0, _, _ = yin_track(y)
        s, m, v = stats(p.split("/")[-1][:34], f0)
        # correction applied relative to dry, in voiced frames of both
        n = min(len(m), len(md))
        both = v[:n] & vd[:n]
        corr = (m[:n] - md[:n])[both] * 100
        corr = corr[np.abs(corr) < 300]
        print(s + f"  | latency {lag:5d} smp  mean |shift| {np.mean(np.abs(corr)):5.1f} ct")
