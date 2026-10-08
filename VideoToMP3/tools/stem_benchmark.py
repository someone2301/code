"""Benchmark and verify the stem pipeline on your own server with your own songs.

Runs Basic and Advanced on each audio file you give it, using exactly the same
code as the website, and writes a Markdown report with, per song and step:
processing time, peak memory, failures, and checks that every WAV in the ZIP
has the same length, start alignment, channel count, sample rate and bit depth.

Example (on the server, as the service user, with the service settings):
    set -a; . /etc/videotomp3.env; set +a
    /opt/videotomp3/.venv/bin/python /opt/videotomp3/tools/stem_benchmark.py \
        --options drums,vocals,guitar_piano --out ~/stem-report.md song1.flac song2.mp3 ...

--options forces those Advanced splits on for the test even if their
VTM_ENABLE_* switch is still 0, so you can verify them before enabling.
"""

from __future__ import annotations

import argparse
import io
import shutil
import sys
import tempfile
import time
import zipfile
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent / "web"))
import numpy as np  # noqa: E402
import soundfile as sf  # noqa: E402
import stems  # noqa: E402


def lag_samples(a: np.ndarray, b: np.ndarray, max_lag: int = 22050) -> int:
    """Offset of b relative to a (0 = aligned), from FFT cross-correlation of mono mixes."""
    a = a.mean(axis=1)[: 30 * stems.MODEL_SR]
    b = b.mean(axis=1)[: 30 * stems.MODEL_SR]
    n = len(a) + len(b)
    corr = np.fft.irfft(np.fft.rfft(a, n) * np.conj(np.fft.rfft(b, n)), n)
    corr = np.concatenate([corr[-max_lag:], corr[: max_lag + 1]])
    return int(np.argmax(np.abs(corr)) - max_lag)


def read_zip(zip_path: Path) -> dict[str, tuple[np.ndarray, sf._SoundFileInfo]]:
    out = {}
    with zipfile.ZipFile(zip_path) as zf:
        for n in zf.namelist():
            if n.endswith(".wav"):
                raw = zf.read(n)
                out[n.split(" - ", 1)[1][:-4]] = (sf.read(io.BytesIO(raw), dtype="float32", always_2d=True)[0],
                                                  sf.info(io.BytesIO(raw)))
    return out


def checks(files: dict, source: np.ndarray | None, rate: str) -> list[str]:
    notes = []
    fmt = {(i.frames, i.channels, i.samplerate, i.subtype) for _, i in files.values()}
    notes.append(f"format identical across {len(files)} files: {'yes' if len(fmt) == 1 else 'NO ' + str(fmt)}")
    if int(rate) == stems.MODEL_SR and source is not None:
        mix = sum(files[k][0] for k in stems.BASIC_STEMS)
        n = min(len(mix), len(source))
        notes.append(f"4 stems vs original start offset: {lag_samples(source[:n], mix[:n])} samples")
        resid = ((source[:n] - mix[:n]) ** 2).sum() / ((source[:n] ** 2).sum() + 1e-12)
        notes.append(f"4 stems summed vs original: residual {10 * np.log10(resid + 1e-12):.1f} dB")
    pairs = [("vocals", ["vocals - lead", "vocals - background"]),
             ("drums", ["drums - kick", "drums - snare", "drums - toms", "drums - hi-hat", "drums - cymbals"]),
             ("other", ["experimental - guitar", "experimental - piano"])]
    for parent, parts in pairs:
        if all(p in files for p in parts):
            summed = sum(files[p][0] for p in parts)
            notes.append(f"{' + '.join(p.split(' - ')[1] for p in parts)} vs {parent} start offset: "
                         f"{lag_samples(files[parent][0], summed)} samples")
    return notes


def main() -> None:
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("songs", nargs="+", type=Path)
    ap.add_argument("--options", default="", help="comma list: drums,vocals,guitar_piano")
    ap.add_argument("--rate", default="44100", choices=sorted(stems.RATES))
    ap.add_argument("--depth", default="24", choices=sorted(stems.DEPTHS))
    ap.add_argument("--out", type=Path, default=Path("stem-report.md"))
    a = ap.parse_args()

    cfg = stems.Config.from_env()
    wanted = {o for o in a.options.split(",") if o}
    unknown = wanted - set(stems.OPTIONS)
    if unknown:
        sys.exit(f"Unknown options: {unknown}")
    for k in wanted:
        cfg.enabled[k] = True
    status = stems.option_status(cfg)
    for k in wanted:
        if not status[k][0]:
            sys.exit(f"{k} cannot run: {status[k][1]}")

    lines = ["# Stem pipeline benchmark", "",
             f"Settings: {a.rate} Hz, {a.depth}, Demucs model {cfg.demucs_model}, device "
             f"'{cfg.device or 'auto'}'. Options tested: {', '.join(sorted(wanted)) or 'none'}.", ""]
    runs = [("Basic", set())] + ([("Advanced", wanted)] if wanted else [])
    for song in a.songs:
        source44 = None
        with tempfile.TemporaryDirectory() as tmp:
            ref = Path(tmp) / "ref.wav"
            stems.prepare_source(song, ref)
            source44 = sf.read(str(ref), dtype="float32", always_2d=True)[0]
        lines += [f"## {song.name} ({len(source44) / stems.MODEL_SR / 60:.1f} min)", ""]
        for label, opts in runs:
            work = Path(tempfile.mkdtemp(prefix="stembench-"))
            print(f"\n== {song.name}: {label} {sorted(opts)}", flush=True)

            def report(step, state, pct, detail):
                if state != "running" or pct in (0, 50):
                    print(f"   {step:<13} {state:<8} {pct:3d}% {detail}", flush=True)
            t = time.time()
            try:
                result = stems.run_pipeline(song, work, "bench", opts, a.rate, a.depth, cfg, report)
            except Exception as exc:  # noqa: BLE001
                lines += [f"### {label}: FAILED", "", f"`{exc}`", ""]
                shutil.rmtree(work, ignore_errors=True)
                continue
            files = read_zip(result.zip_path)
            expected = stems.planned_files(opts - set(result.failures))
            lines += [f"### {label}: {time.time() - t:.0f} s total", "",
                      "| Step | Time (s) | Peak memory (MB) | Result |", "|---|---|---|---|"]
            for step in ["prepare", "stems"] + [k for k in stems.OPTIONS if k in opts] + ["package"]:
                s = result.stats.get(step)
                res = f"FAILED: {result.failures[step]}" if step in result.failures else "ok"
                lines.append(f"| {stems.STEP_LABELS[step]} | {s.seconds:.0f} | "
                             f"{s.peak_mem_mb:.0f} | {res} |" if s else f"| {stems.STEP_LABELS[step]} | - | - | {res} |")
            lines += ["", f"- ZIP contents match selection: {'yes' if sorted(files) == sorted(expected) else 'NO'} "
                          f"({len(files)} WAV files: {', '.join(files)})"]
            lines += [f"- {n}" for n in checks(files, source44, a.rate)]
            if result.frames_adjusted:
                lines.append(f"- length corrections before packaging (frames): {result.frames_adjusted}")
            lines.append("")
            shutil.rmtree(work, ignore_errors=True)
        a.out.write_text("\n".join(lines) + "\n")
    print(f"\nReport written to {a.out}")


if __name__ == "__main__":
    main()
