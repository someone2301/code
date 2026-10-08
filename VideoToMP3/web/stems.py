"""Stem-splitting pipeline shared by the web app and tools/stem_benchmark.py.

Basic:    Demucs (htdemucs) -> vocals, drums, bass, other.
Advanced: the same four stems, plus only the detailed splits that were selected:
  drums         LarsNet on the Demucs drum stem -> kick, snare, toms, hi-hat, cymbals
  vocals        karaoke model on the full mix -> lead; background = vocals - lead
  guitar_piano  a second Demucs pass (htdemucs_6s); only its guitar and piano are kept

Every model runs in its own low-priority child process, started only when its
step is needed, so its memory is freed as soon as the step ends. An optional
step that fails is reported and skipped; the four basic stems are still packaged.
"""

import os
import re
import shutil
import subprocess
import sys
import threading
import time
import zipfile
from collections import deque
from dataclasses import dataclass, field
from pathlib import Path
from typing import Callable

WEB_DIR = Path(__file__).resolve().parent
MODEL_SR = 44100  # every model here works at 44.1 kHz

BASIC_STEMS = ["vocals", "drums", "bass", "other"]
# option key -> (label, [(file produced by the tool, name in the ZIP)])
OPTIONS = {
    "drums": ("Drum kit parts", [
        ("kick", "drums - kick"), ("snare", "drums - snare"), ("toms", "drums - toms"),
        ("hihat", "drums - hi-hat"), ("cymbals", "drums - cymbals")]),
    "vocals": ("Lead / background vocals", [
        ("lead", "vocals - lead"), ("background", "vocals - background")]),
    "guitar_piano": ("Guitar and piano (experimental)", [
        ("guitar", "experimental - guitar"), ("piano", "experimental - piano")]),
}
STEP_LABELS = {
    "prepare": "Preparing audio", "stems": "Splitting 4 main stems", "drums": "Splitting drum kit",
    "vocals": "Splitting lead / background vocals", "guitar_piano": "Extracting guitar and piano",
    "package": "Converting and packaging",
}
RATES = {"44100", "48000", "88200", "96000"}
DEPTHS = {"16": "pcm_s16le", "24": "pcm_s24le", "32f": "pcm_f32le"}
SF_SUBTYPES = {"16": "PCM_16", "24": "PCM_24", "32f": "FLOAT"}

CREDITS = {
    "stems": "4 main stems: Demucs (htdemucs) by Alexandre Defossez / Meta AI, MIT license.",
    "drums": ("Drum kit parts: LarsNet by A. I. Mezza, R. Giampiccolo, A. Bernardini, A. Sarti "
              "(Politecnico di Milano). Pretrained weights licensed CC BY-NC 4.0 "
              "(https://creativecommons.org/licenses/by-nc/4.0/); non-commercial use only. "
              "Output was processed (format conversion)."),
    "vocals": ("Lead vocals: Mel-Band RoFormer karaoke model by aufr33 and viperx, run with "
               "python-audio-separator (MIT). The authors have not published a license for the "
               "weights. Background = Demucs vocals minus lead."),
    "guitar_piano": ("Guitar/piano: Demucs htdemucs_6s, which its authors describe as experimental; "
                     "piano has noticeable bleed and artifacts. Guitar/piano also remain in 'other'."),
}


def planned_files(options: set[str]) -> list[str]:
    """ZIP file names (without title prefix or .wav) for a selection, in ZIP order."""
    names = list(BASIC_STEMS)
    for key in OPTIONS:
        if key in options:
            names += [zip_name for _, zip_name in OPTIONS[key][1]]
    return names


@dataclass
class Config:
    demucs_model: str = "htdemucs"
    device: str = ""
    timeout_sec: int = 1800
    larsnet_dir: Path = Path("/opt/larsnet")
    drum_batch: str = "4"
    vocal_python: str = ""
    vocal_model_dir: str = "/var/lib/videotomp3/models/audio-separator"
    vocal_model: str = "mel_band_roformer_karaoke_aufr33_viperx_sdr_10.1956.ckpt"
    enabled: dict = field(default_factory=dict)

    @classmethod
    def from_env(cls) -> "Config":
        e = os.environ.get
        return cls(
            demucs_model=e("VTM_STEM_MODEL", "htdemucs"),
            device=e("VTM_STEM_DEVICE", ""),
            timeout_sec=int(e("VTM_STEM_TIMEOUT_MIN", "30")) * 60,
            larsnet_dir=Path(e("VTM_LARSNET_DIR", "/opt/larsnet")),
            drum_batch=e("VTM_DRUM_BATCH", "4"),
            vocal_python=e("VTM_VOCAL_PYTHON", ""),
            vocal_model_dir=e("VTM_VOCAL_MODEL_DIR", "/var/lib/videotomp3/models/audio-separator"),
            vocal_model=e("VTM_VOCAL_MODEL", "mel_band_roformer_karaoke_aufr33_viperx_sdr_10.1956.ckpt"),
            # Optional splits are off until the owner verifies them on their server.
            enabled={"drums": e("VTM_ENABLE_DRUM_SPLIT", "0") == "1",
                     "vocals": e("VTM_ENABLE_VOCAL_SPLIT", "0") == "1",
                     "guitar_piano": e("VTM_ENABLE_GUITAR_PIANO", "0") == "1"},
        )


def option_status(cfg: Config) -> dict[str, tuple[bool, str]]:
    """Which optional splits can be offered, with a reason for the server log."""
    status = {}
    for key, env in (("drums", "VTM_ENABLE_DRUM_SPLIT"), ("vocals", "VTM_ENABLE_VOCAL_SPLIT"),
                     ("guitar_piano", "VTM_ENABLE_GUITAR_PIANO")):
        if not cfg.enabled.get(key):
            status[key] = (False, f"off ({env} is not 1)")
    if "drums" not in status:
        config = cfg.larsnet_dir / "config.yaml"
        try:
            import yaml
            paths = yaml.safe_load(config.read_text())["inference_models"].values()
            missing = [p for p in paths if not (cfg.larsnet_dir / p).is_file()]
            status["drums"] = ((False, f"off (missing {cfg.larsnet_dir / missing[0]})") if missing
                               else (True, f"on (LarsNet in {cfg.larsnet_dir})"))
        except Exception as exc:  # noqa: BLE001
            status["drums"] = (False, f"off (cannot read {config}: {exc})")
    if "vocals" not in status:
        model = Path(cfg.vocal_model_dir) / cfg.vocal_model
        if not cfg.vocal_python or not Path(cfg.vocal_python).is_file():
            status["vocals"] = (False, "off (VTM_VOCAL_PYTHON does not point to a Python)")
        elif not model.is_file():
            status["vocals"] = (False, f"off (model not downloaded: {model})")
        else:
            status["vocals"] = (True, f"on ({cfg.vocal_model})")
    if "guitar_piano" not in status:
        status["guitar_piano"] = (True, "on (htdemucs_6s, experimental)")
    return status


class StepFailed(RuntimeError):
    pass


@dataclass
class StepStats:
    seconds: float = 0.0
    peak_mem_mb: float = 0.0


def run_tool(cmd: list[str], what: str, timeout_sec: int, on_progress: Callable[[int], None],
             touch: Path | None = None, passes_expected: int = 1, cwd: str | None = None) -> StepStats:
    """Run one model in a low-priority child process; parse its tqdm-style progress bar.

    Returns elapsed time and the child's own peak memory (from wait4).
    """
    passes, last_pct, tail = 0, 0, deque(maxlen=15)
    start = time.time()
    proc = subprocess.Popen(cmd, stdout=subprocess.DEVNULL, stderr=subprocess.PIPE, cwd=cwd,
                            preexec_fn=(lambda: os.nice(10)) if os.name == "posix" else None)
    timer = threading.Timer(timeout_sec, proc.kill)
    timer.start()
    try:
        buf = b""
        while chunk := os.read(proc.stderr.fileno(), 4096):
            buf += chunk
            *lines, buf = re.split(rb"[\r\n]", buf)
            for raw in lines:
                line = raw.decode("utf-8", "replace").strip()
                if not line:
                    continue
                tail.append(line)
                m = re.search(r"(\d{1,3})%\|", line)
                if m:
                    pct = int(m.group(1))
                    if pct + 50 < last_pct:
                        passes += 1
                    last_pct = pct
                    on_progress(min(99, int((min(passes, passes_expected - 1) * 100 + pct) / passes_expected)))
                    if touch:
                        os.utime(touch)  # keeps the cleanup cron away from running jobs
        _, status, usage = os.wait4(proc.pid, 0)
        proc.returncode = code = os.waitstatus_to_exitcode(status)
    finally:
        timer.cancel()
        proc.stderr.close()
    stats = StepStats(time.time() - start, usage.ru_maxrss / 1024)  # ru_maxrss is KiB on Linux
    if code != 0:
        if code < 0:
            raise StepFailed(f"{what} took too long and was stopped.")
        detail = next((l for l in reversed(tail) if "Error" in l), tail[-1] if tail else "")
        raise StepFailed(f"{what} failed. {detail}"[:300])
    return stats


def has_soxr() -> bool:
    try:
        return subprocess.run(
            ["ffmpeg", "-hide_banner", "-loglevel", "error", "-f", "lavfi", "-i",
             "anullsrc=r=44100", "-t", "0.05", "-af", "aresample=48000:resampler=soxr",
             "-f", "null", "-"], capture_output=True, timeout=30).returncode == 0
    except (OSError, subprocess.TimeoutExpired):
        return False


HAS_SOXR = has_soxr()


def _resample_filter(rate: str, depth: str | None = None) -> str:
    filt = f"aresample={rate}"
    if HAS_SOXR:
        filt += ":resampler=soxr:precision=28"
    if depth == "16":
        filt += ":osf=s16:dither_method=triangular"
    return filt


def prepare_source(src: Path, dst: Path) -> None:
    """Decode to 44.1 kHz stereo 32-bit float so every model sees identical input."""
    subprocess.run(["ffmpeg", "-hide_banner", "-loglevel", "error", "-y", "-i", str(src), "-vn",
                    "-af", _resample_filter(str(MODEL_SR)), "-ac", "2", "-c:a", "pcm_f32le",
                    str(dst)], check=True, capture_output=True, timeout=600)


def convert_stem(src: Path, dst: Path, rate: str, depth: str) -> None:
    subprocess.run(["ffmpeg", "-hide_banner", "-loglevel", "error", "-y", "-i", str(src),
                    "-af", _resample_filter(rate, depth), "-ac", "2", "-c:a", DEPTHS[depth],
                    "-ar", rate, str(dst)], check=True, capture_output=True, timeout=600)


def align_length(path: Path, frames: int) -> int:
    """Pad or trim a float WAV to `frames`; returns how many frames it was off by."""
    import numpy as np
    import soundfile as sf
    data, sr = sf.read(str(path), dtype="float32", always_2d=True)
    diff = len(data) - frames
    if diff:
        data = np.pad(data, ((0, max(0, -diff)), (0, 0)))[:frames]
        sf.write(str(path), data, sr, subtype="FLOAT")
    return diff


@dataclass
class Result:
    zip_path: Path
    files: list[str]
    failures: dict[str, str]
    stats: dict[str, StepStats]
    frames_adjusted: dict[str, int]


Report = Callable[[str, str, int, str], None]  # (step, state, progress, detail)


def run_pipeline(source: Path, work: Path, title: str, options: set[str], rate: str, depth: str,
                 cfg: Config, report: Report) -> Result:
    """Run the selected steps; raises only if the 4 basic stems cannot be produced."""
    import soundfile as sf
    stats: dict[str, StepStats] = {}
    failures: dict[str, str] = {}
    py = sys.executable

    def progress(step):
        return lambda pct: report(step, "running", pct, "")

    def timed(step, fn):
        report(step, "running", 0, "")
        t = time.time()
        result = fn()
        stats[step] = result if isinstance(result, StepStats) else StepStats(time.time() - t)
        report(step, "done", 100, "")

    src44 = work / "source44.wav"
    timed("prepare", lambda: prepare_source(source, src44))

    # 1. The four basic stems (required)
    out = work / "stems"
    cmd = [py, "-m", "demucs", "-n", cfg.demucs_model, "-o", str(out), "--filename", "{stem}.{ext}",
           "-j", "1", "--float32"] + (["-d", cfg.device] if cfg.device else []) + [str(src44)]
    try:
        timed("stems", lambda: run_tool(cmd, "Stem splitting", cfg.timeout_sec, progress("stems"), work,
                                        4 if cfg.demucs_model.endswith("_ft") else 1))
    except Exception as exc:
        report("stems", "failed", 0, str(exc))
        raise
    stem_dir = out / cfg.demucs_model
    produced = {name: stem_dir / f"{name}.wav" for name in BASIC_STEMS}
    missing = [n for n, p in produced.items() if not p.exists()]
    if missing:
        report("stems", "failed", 0, f"missing {missing}")
        raise StepFailed(f"Stem splitting did not produce: {', '.join(missing)}")

    # 2. Optional detailed splits, only the selected ones
    def optional(key, cmd, out_dir, cwd=None):
        try:
            timed(key, lambda: run_tool(cmd, STEP_LABELS[key], cfg.timeout_sec, progress(key), work, cwd=cwd))
            for tool_name, zip_name in OPTIONS[key][1]:
                p = out_dir / f"{tool_name}.wav"
                if not p.exists():
                    raise StepFailed(f"{STEP_LABELS[key]} did not produce {tool_name}.wav")
                produced[zip_name] = p
        except Exception as exc:  # noqa: BLE001
            failures[key] = str(exc)[:300]
            for _, zip_name in OPTIONS[key][1]:
                produced.pop(zip_name, None)
            report(key, "failed", 0, failures[key])

    if "drums" in options:
        d = work / "drumparts"
        optional("drums", [py, "-I", str(WEB_DIR / "drum_split.py"), "--larsnet-dir", str(cfg.larsnet_dir),
                           "--batch", cfg.drum_batch] + (["--device", cfg.device] if cfg.device else [])
                 + [str(produced["drums"]), str(d)], d)
    if "vocals" in options:
        d = work / "vocalparts"
        optional("vocals", [cfg.vocal_python, "-I", str(WEB_DIR / "vocal_split.py"),
                            "--model-dir", cfg.vocal_model_dir, "--model", cfg.vocal_model,
                            str(src44), str(produced["vocals"]), str(d)], d)
    if "guitar_piano" in options:
        o = work / "stems6"
        optional("guitar_piano", [py, "-m", "demucs", "-n", "htdemucs_6s", "-o", str(o), "--filename",
                                  "{stem}.{ext}", "-j", "1", "--float32"]
                 + (["-d", cfg.device] if cfg.device else []) + [str(src44)], o / "htdemucs_6s")

    # 3. Align, convert, verify, package
    report("package", "running", 0, "")
    t = time.time()
    frames = sf.info(str(produced["vocals"])).frames
    adjusted = {name: d for name, p in produced.items() if (d := align_length(p, frames))}
    names = [n for n in planned_files(options) if n in produced]
    final = work / "final"
    final.mkdir(exist_ok=True)
    zip_path = work / f"{title} - stems ({_spec(rate, depth)}).zip"
    with zipfile.ZipFile(zip_path, "w", zipfile.ZIP_STORED, allowZip64=True) as zf:
        for i, name in enumerate(names):
            dst = final / f"{title} - {name}.wav"
            convert_stem(produced[name], dst, rate, depth)
            check_wav(dst, rate, depth)
            zf.write(dst, arcname=dst.name)
            dst.unlink()
            os.utime(work)
            report("package", "running", int(100 * (i + 1) / len(names)), "")
        zf.writestr(f"{title} - README.txt", _readme(title, names, options, failures, rate, depth))
    verify_zip(zip_path, rate, depth)
    for d in ("stems", "stems6", "drumparts", "vocalparts", "final"):
        shutil.rmtree(work / d, ignore_errors=True)
    src44.unlink(missing_ok=True)
    stats["package"] = StepStats(time.time() - t)
    report("package", "done", 100, "")
    return Result(zip_path, names, failures, stats, adjusted)


def _spec(rate: str, depth: str) -> str:
    depth_label = "32-bit float" if depth == "32f" else f"{depth}-bit"
    return f"{depth_label} {int(rate) / 1000:g}kHz"


def check_wav(path: Path, rate: str, depth: str) -> None:
    import soundfile as sf
    info = sf.info(str(path))
    if info.samplerate != int(rate) or info.channels != 2 or info.subtype != SF_SUBTYPES[depth]:
        raise StepFailed(f"{path.name}: got {info.samplerate} Hz, {info.channels} ch, {info.subtype}")


def verify_zip(zip_path: Path, rate: str, depth: str) -> list[dict]:
    """Every WAV in the ZIP must share frame count, channels, rate and bit depth."""
    import soundfile as sf
    rows = []
    with zipfile.ZipFile(zip_path) as zf:
        for n in zf.namelist():
            if n.endswith(".wav"):
                with zf.open(n) as fh:  # seekable for stored entries; reads headers only
                    info = sf.info(fh)
                rows.append({"name": n, "frames": info.frames, "channels": info.channels,
                             "rate": info.samplerate, "subtype": info.subtype})
    if len({(r["frames"], r["channels"], r["rate"], r["subtype"]) for r in rows}) != 1:
        raise StepFailed(f"Stems in the ZIP do not match each other: {rows}")
    if rows[0]["rate"] != int(rate) or rows[0]["subtype"] != SF_SUBTYPES[depth]:
        raise StepFailed(f"ZIP format {rows[0]} does not match {rate} Hz / {depth}")
    return rows


def _readme(title, names, options, failures, rate, depth) -> str:
    lines = [f"{title}", f"Format: WAV, {_spec(rate, depth)}, stereo. All files are the same length",
             "and start at the same time, so they line up when dropped into a DAW at 0:00.", "",
             "Files:"] + [f"  {title} - {n}.wav" for n in names]
    if failures:
        lines += ["", "Optional splits that failed (the files above are still complete):"]
        lines += [f"  {OPTIONS[k][0]}: {msg}" for k, msg in failures.items()]
    lines += ["", "Notes:", "  Synths, guitars, keys and anything else not listed stay in 'other'."]
    if "guitar_piano" in options:
        lines.append("  Experimental guitar/piano files come from a separate model; they overlap with"
                     " 'other' and are not subtracted from it, so do not layer them on top of it.")
    if "vocals" in options:
        lines.append("  'vocals - lead' + 'vocals - background' add up to 'vocals'.")
    lines += ["", "Credits:", "  " + CREDITS["stems"]]
    lines += ["  " + CREDITS[k] for k in OPTIONS if k in options and k not in failures]
    return "\n".join(lines) + "\n"
