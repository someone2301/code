"""Correction applied versus how fast the dry pitch moves (st/s), per render: python3 slope.py dry.wav render.wav ..."""
import sys
sys.argv = [sys.argv[0], "x"] + sys.argv[1:]
exec(open(__file__.replace("slope.py", "analyze.py")).read().split('if __name__ == "__main__"')[0])
dryp, refs = sys.argv[2], sys.argv[3:]
dry = load(dryp)
f0d, _, _ = yin_track(dry)
md = midi(np.where(f0d > 0, f0d, 1.0))
# slope in semitones per second from a 5-frame centred difference
sl = np.zeros_like(md)
sl[2:-2] = (md[4:] - md[:-4]) / (4 * HOP / SR)
vd = f0d > 0
ok = vd.copy(); ok[2:-2] &= vd[4:] & vd[:-4]
target = nearest_scale(md)
for p in refs:
    y = load(p); lag = latency(dry, y)
    y = y[lag:] if lag > 0 else np.concatenate([np.zeros(-lag), y])
    f0, _, _ = yin_track(y)
    m = midi(np.where(f0 > 0, f0, 1.0))
    n = min(len(m), len(md))
    both = ok[:n] & (f0[:n] > 0)
    applied = (m[:n] - md[:n]) * 100           # correction the tuner applied (cents)
    full = (target[:n] - md[:n]) * 100         # correction that snaps to the nearest scale note
    print(p.split("/")[-1])
    for lo, hi in [(0, 2), (2, 5), (5, 10), (10, 20), (20, 40), (40, 1000)]:
        sel = both & (np.abs(sl[:n]) >= lo) & (np.abs(sl[:n]) < hi) & (np.abs(full) > 15) & (np.abs(applied) < 300)
        if sel.sum() < 5: continue
        ratio = np.median(applied[sel] / full[sel])
        print(f"   |slope| {lo:3d}-{hi:4d} st/s: frames {sel.sum():5d}  applied/full correction median {ratio:5.2f}")
