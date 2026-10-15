"""Estimate tempo (BPM) and musical key of a song, for file names.

Tempo: autocorrelation of librosa's onset envelope (ISC license), searched at 0.05 BPM steps.
Key: average chroma over the song, correlated with the Krumhansl-Kessler
major/minor key profiles; the best of the 24 keys wins.

Both are estimates. Common errors: half/double tempo (e.g. 70 vs 140 BPM) and
confusing a key with its relative major/minor (C major vs A minor).
"""

from pathlib import Path

NOTES = ["C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"]
MAJOR = [6.35, 2.23, 3.48, 2.33, 4.38, 4.09, 2.52, 5.19, 2.39, 3.66, 2.29, 2.88]
MINOR = [6.33, 2.68, 3.52, 5.38, 2.60, 3.53, 2.54, 4.75, 3.98, 2.69, 3.34, 3.17]


def _tempo(y, sr) -> float | None:
    """Fine-resolution tempo: comb-filter the onset autocorrelation over 70-180 BPM."""
    import librosa
    import numpy as np
    hop = 128
    env = librosa.onset.onset_strength(y=y, sr=sr, hop_length=hop)
    env = env - env.mean()
    ac = np.correlate(env, env, mode="full")[len(env) - 1:]
    ac /= ac[0] + 1e-9
    fps = sr / hop
    lags = np.arange(len(ac))

    def score(bpm):
        period = 60 * fps / bpm
        # beats and their multiples, plus half-beats weighted lower
        return sum(np.interp(k * period, lags, ac) for k in (1, 2, 3, 4)) + \
            0.5 * np.interp(0.5 * period, lags, ac)

    grid = np.arange(70.0, 180.0, 0.05)
    scores = np.array([score(b) for b in grid])
    if scores.max() <= 0:
        return None
    return float(grid[int(np.argmax(scores))])


def _key(y, sr) -> str | None:
    import librosa
    import numpy as np
    harmonic = librosa.effects.harmonic(y)
    chroma = librosa.feature.chroma_cqt(y=harmonic, sr=sr).mean(axis=1)
    ranked = []
    for shift in range(12):
        for name, profile in (("major", MAJOR), ("minor", MINOR)):
            ranked.append((float(np.corrcoef(chroma, np.roll(profile, shift))[0, 1]), shift, name))
    ranked.sort(reverse=True)
    r, shift, mode = ranked[0]
    if r < 0.3:
        return None
    # Relative major/minor share the same notes; the bass usually sits on the tonic.
    rel_shift, rel_mode = ((shift + 9) % 12, "minor") if mode == "major" else ((shift + 3) % 12, "major")
    rel_r = next(x[0] for x in ranked if x[1] == rel_shift and x[2] == rel_mode)
    if r - rel_r < 0.15:
        bass = librosa.feature.chroma_cqt(y=harmonic, sr=sr, fmin=librosa.note_to_hz("C1"),
                                          n_octaves=3).mean(axis=1)
        if bass[rel_shift] > bass[shift] * 1.1:
            shift, mode = rel_shift, rel_mode
    return f"{NOTES[shift]} {mode}"


def detect(path: Path, max_seconds: float = 240) -> dict:
    """Return {"bpm": float, "key": "A minor"} (values None if not detectable)."""
    import librosa
    import numpy as np
    y, sr = librosa.load(str(path), sr=22050, mono=True, duration=max_seconds)
    if len(y) < sr * 5 or float(np.abs(y).max()) < 1e-4:
        return {"bpm": None, "key": None}
    return {"bpm": _tempo(y, sr), "key": _key(y, sr)}


def tag(info: dict) -> str:
    """'128 BPM, A minor' style label; empty if nothing was detected."""
    bpm = info.get("bpm")
    parts = [f"{bpm:.0f} BPM" if abs(bpm - round(bpm)) < 0.25 else f"{bpm:.1f} BPM"] if bpm else []
    parts += [info["key"]] if info.get("key") else []
    return ", ".join(parts)


if __name__ == "__main__":
    import sys
    for p in sys.argv[1:]:
        print(p, detect(Path(p)))
