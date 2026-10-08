"""Web front end for VideoToMP3.

Run locally:   python3 web/app.py
Production:    gunicorn -w 1 --threads 8 -b 127.0.0.1:8000 web.app:app
(Use one worker process: jobs are tracked in memory.)
"""

import hmac
import importlib.util
import ipaddress
import os
import re
import secrets
import shutil
import socket
import subprocess
import sys
import tempfile
import threading
import time
import uuid
import zipfile
from collections import defaultdict, deque
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path
from urllib.parse import urlparse

from flask import (Flask, abort, jsonify, redirect, render_template, request,
                   send_file, session, url_for)

sys.path.insert(0, str(Path(__file__).resolve().parent.parent))
import yt_dlp  # noqa: E402
from video_to_mp3 import build_options, ensure_ffmpeg  # noqa: E402

ACCESS_CODE = os.environ.get("VTM_ACCESS_CODE", "")
MAX_DURATION_MIN = int(os.environ.get("VTM_MAX_DURATION_MIN", "20"))
JOBS_PER_HOUR = int(os.environ.get("VTM_JOBS_PER_HOUR", "20"))
FILE_TTL_SEC = int(os.environ.get("VTM_FILE_TTL_MIN", "30")) * 60
WORKERS = int(os.environ.get("VTM_WORKERS", "2"))
WORK_DIR = Path(os.environ.get("VTM_WORK_DIR", tempfile.gettempdir())) / "vtm_jobs"
ALLOWED_QUALITIES = {"128", "192", "256", "320"}
ALLOW_PRIVATE = os.environ.get("VTM_ALLOW_PRIVATE", "0") == "1"  # testing only

# Stem splitting (Demucs). Enabled automatically when the demucs package is installed.
STEMS_ENABLED = (os.environ.get("VTM_STEMS", "1") == "1"
                 and importlib.util.find_spec("demucs") is not None)
STEM_MODEL = os.environ.get("VTM_STEM_MODEL", "htdemucs")
STEM_DEVICE = os.environ.get("VTM_STEM_DEVICE", "")  # "", "cpu" or "cuda"
STEM_MAX_DURATION_MIN = int(os.environ.get("VTM_STEM_MAX_DURATION_MIN", "10"))
STEM_JOBS_PER_HOUR = int(os.environ.get("VTM_STEM_JOBS_PER_HOUR", "5"))
STEM_TIMEOUT_SEC = int(os.environ.get("VTM_STEM_TIMEOUT_MIN", "30")) * 60
STEM_QUEUE_MAX = int(os.environ.get("VTM_STEM_QUEUE_MAX", "5"))
STEM_MODES = {"stems2": "vocals", "stems4": None}  # value: --two-stems argument
STEM_FORMATS = {"mp3", "wav"}

app = Flask(__name__)
app.secret_key = os.environ.get("VTM_SECRET_KEY") or secrets.token_hex(32)
app.config.update(SESSION_COOKIE_HTTPONLY=True, SESSION_COOKIE_SAMESITE="Lax",
                  SESSION_COOKIE_SECURE=os.environ.get("VTM_SECURE_COOKIE", "1") == "1")

executor = ThreadPoolExecutor(max_workers=WORKERS)
# Stem splitting uses all CPU cores, so run one at a time and queue the rest.
stem_executor = ThreadPoolExecutor(max_workers=1)
stem_queue: list[str] = []
jobs: dict[str, dict] = {}
jobs_lock = threading.Lock()
recent_by_key: dict[tuple, deque] = defaultdict(deque)


def client_ip() -> str:
    # Cloudflare passes the visitor's real IP in this header.
    return request.headers.get("CF-Connecting-IP") or request.remote_addr or "?"


def authorized() -> bool:
    return not ACCESS_CODE or session.get("ok") is True


def rate_limited(key: tuple, limit: int) -> bool:
    now = time.time()
    q = recent_by_key[key]
    while q and now - q[0] > 3600:
        q.popleft()
    if len(q) >= limit:
        return True
    q.append(now)
    return False


def points_to_private_network(url: str) -> bool:
    """Block links to localhost or the LAN so visitors cannot probe your server."""
    host = urlparse(url).hostname
    if not host:
        return True
    try:
        addrs = {ai[4][0] for ai in socket.getaddrinfo(host, None)}
    except socket.gaierror:
        return False  # let yt-dlp report the bad link
    for a in addrs:
        ip = ipaddress.ip_address(a.split("%")[0])
        if not ip.is_global:
            return True
    return False


def duration_filter(max_min: int):
    def check(info, *, incomplete):
        duration = info.get("duration")
        if duration and duration > max_min * 60:
            return f"Video is longer than {max_min} minutes"
        return None
    return check


def clean_error(exc: Exception) -> str:
    msg = str(exc).replace("ERROR: ", "").split("\n")[0]
    return msg.split("; please report this issue")[0].split(" (caused by")[0][:300]


def download_progress_hook(job_id: str):
    def hook(d):
        if d.get("status") == "downloading":
            total = d.get("total_bytes") or d.get("total_bytes_estimate")
            if total:
                jobs[job_id]["progress"] = int(d.get("downloaded_bytes", 0) * 100 / total)
        elif d.get("status") == "finished":
            jobs[job_id]["status"] = "converting"
    return hook


def run_job(job_id: str, url: str, quality: str) -> None:
    job_dir = WORK_DIR / job_id
    job_dir.mkdir(parents=True, exist_ok=True)
    opts = build_options(job_dir, quality, playlist=False, embed_art=True)
    opts.update(quiet=True, no_warnings=True, ignoreerrors=False, noprogress=True,
                progress_hooks=[download_progress_hook(job_id)],
                match_filter=duration_filter(MAX_DURATION_MIN))
    jobs[job_id]["status"] = "downloading"
    try:
        with yt_dlp.YoutubeDL(opts) as ydl:
            ydl.download([url])
        mp3s = list(job_dir.glob("*.mp3"))
        if not mp3s:
            raise RuntimeError(f"Skipped: video may be longer than {MAX_DURATION_MIN} minutes")
        jobs[job_id].update(status="done", progress=100, file=str(mp3s[0]), name=mp3s[0].name)
    except Exception as exc:  # noqa: BLE001
        jobs[job_id].update(status="error", error=clean_error(exc))
    finally:
        jobs[job_id]["finished_at"] = time.time()


def run_demucs(job_id: str, job_dir: Path, source: Path, mode: str, fmt: str) -> Path:
    """Run Demucs in a child process so its memory is freed when it exits."""
    out_dir = job_dir / "stems"
    cmd = [sys.executable, "-m", "demucs", "-n", STEM_MODEL, "-o", str(out_dir),
           "--filename", "{stem}.{ext}", "-j", "1"]
    if STEM_DEVICE:
        cmd += ["-d", STEM_DEVICE]
    if STEM_MODES[mode]:
        cmd += ["--two-stems", STEM_MODES[mode]]
    if fmt == "mp3":
        cmd += ["--mp3", "--mp3-bitrate", "320"]
    cmd.append(str(source))

    # Fine-tuned models ("_ft") are a bag of 4 models, so the progress bar runs 4 times.
    passes_expected = 4 if STEM_MODEL.endswith("_ft") else 1
    passes, last_pct, tail = 0, 0, deque(maxlen=15)
    proc = subprocess.Popen(cmd, stdout=subprocess.DEVNULL, stderr=subprocess.PIPE,
                            preexec_fn=(lambda: os.nice(10)) if os.name == "posix" else None)
    timer = threading.Timer(STEM_TIMEOUT_SEC, proc.kill)
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
                    overall = (min(passes, passes_expected - 1) * 100 + pct) / passes_expected
                    jobs[job_id]["progress"] = min(99, int(overall))
                    os.utime(job_dir)  # keeps the cleanup cron from removing a running job
        code = proc.wait()
    finally:
        timer.cancel()
    if code != 0:
        if code < 0:
            raise RuntimeError("Stem splitting took too long and was stopped.")
        detail = next((l for l in reversed(tail) if "Error" in l), tail[-1] if tail else "")
        raise RuntimeError(f"Stem splitting failed. {detail}"[:300])
    model_dir = out_dir / STEM_MODEL
    return model_dir if model_dir.is_dir() else out_dir


def run_stem_job(job_id: str, url: str, mode: str, fmt: str) -> None:
    with jobs_lock:
        if job_id in stem_queue:
            stem_queue.remove(job_id)
    job_dir = WORK_DIR / job_id
    job_dir.mkdir(parents=True, exist_ok=True)
    opts = {
        "format": "bestaudio/best",
        "outtmpl": str(job_dir / "source.%(ext)s"),
        "noplaylist": True,
        "postprocessors": [{"key": "FFmpegExtractAudio", "preferredcodec": "wav"}],
        "quiet": True, "no_warnings": True, "noprogress": True, "retries": 5,
        "progress_hooks": [download_progress_hook(job_id)],
        "match_filter": duration_filter(STEM_MAX_DURATION_MIN),
    }
    jobs[job_id].update(status="downloading", progress=0)
    try:
        with yt_dlp.YoutubeDL(opts) as ydl:
            info = ydl.extract_info(url, download=True) or {}
        source = job_dir / "source.wav"
        if not source.exists():
            raise RuntimeError(f"Skipped: video may be longer than {STEM_MAX_DURATION_MIN} minutes")

        jobs[job_id].update(status="separating", progress=0)
        stem_dir = run_demucs(job_id, job_dir, source, mode, fmt)

        jobs[job_id].update(status="packaging", progress=99)
        title = yt_dlp.utils.sanitize_filename(info.get("title") or "track")[:120]
        zip_path = job_dir / f"{title} - stems.zip"
        stems = sorted(p for p in stem_dir.iterdir() if p.suffix in (".mp3", ".wav"))
        if not stems:
            raise RuntimeError("Stem splitting produced no files.")
        with zipfile.ZipFile(zip_path, "w", zipfile.ZIP_STORED) as zf:
            for p in stems:
                zf.write(p, arcname=f"{title} - {p.stem}{p.suffix}")
        source.unlink(missing_ok=True)
        shutil.rmtree(job_dir / "stems", ignore_errors=True)
        jobs[job_id].update(status="done", progress=100, file=str(zip_path), name=zip_path.name,
                            stems=[p.stem for p in stems])
    except Exception as exc:  # noqa: BLE001
        jobs[job_id].update(status="error", error=clean_error(exc))
    finally:
        jobs[job_id]["finished_at"] = time.time()


def cleanup_loop() -> None:
    while True:
        time.sleep(60)
        now = time.time()
        with jobs_lock:
            expired = [k for k, j in jobs.items()
                       if j.get("finished_at") and now - j["finished_at"] > FILE_TTL_SEC]
            for k in expired:
                jobs.pop(k, None)
                shutil.rmtree(WORK_DIR / k, ignore_errors=True)


@app.get("/")
def index():
    if not authorized():
        return render_template("login.html", error=None)
    return render_template("index.html", max_min=MAX_DURATION_MIN, ttl_min=FILE_TTL_SEC // 60,
                           stems_enabled=STEMS_ENABLED, stem_max_min=STEM_MAX_DURATION_MIN)


@app.post("/login")
def login():
    code = request.form.get("code", "")
    if ACCESS_CODE and hmac.compare_digest(code.encode(), ACCESS_CODE.encode()):
        session["ok"] = True
        return redirect(url_for("index"))
    time.sleep(1)
    return render_template("login.html", error="Wrong access code."), 401


@app.post("/logout")
def logout():
    session.clear()
    return redirect(url_for("index"))


@app.post("/api/convert")
def convert():
    if not authorized():
        abort(401)
    data = request.get_json(silent=True) or {}
    url = str(data.get("url", "")).strip()
    quality = str(data.get("quality", "192"))
    mode = str(data.get("mode", "mp3"))
    fmt = str(data.get("format", "mp3"))
    if mode != "mp3" and (mode not in STEM_MODES or not STEMS_ENABLED):
        return jsonify(error="Stem splitting is not available."), 400
    if not url.startswith(("http://", "https://")) or len(url) > 2000:
        return jsonify(error="Enter a valid http(s) link."), 400
    if not ALLOW_PRIVATE and points_to_private_network(url):
        return jsonify(error="That link is not allowed."), 400
    if quality not in ALLOWED_QUALITIES:
        quality = "192"
    if fmt not in STEM_FORMATS:
        fmt = "mp3"

    job_id = uuid.uuid4().hex
    if mode == "mp3":
        if rate_limited((client_ip(), "mp3"), JOBS_PER_HOUR):
            return jsonify(error="Too many requests. Try again later."), 429
        with jobs_lock:
            jobs[job_id] = {"status": "queued", "progress": 0}
        executor.submit(run_job, job_id, url, quality)
        return jsonify(id=job_id)

    with jobs_lock:
        if len(stem_queue) >= STEM_QUEUE_MAX:
            return jsonify(error="The stem splitter is busy. Try again in a few minutes."), 503
    if rate_limited((client_ip(), "stems"), STEM_JOBS_PER_HOUR):
        return jsonify(error=f"Stem splitting is limited to {STEM_JOBS_PER_HOUR} songs per hour."), 429
    with jobs_lock:
        jobs[job_id] = {"status": "queued", "progress": 0}
        stem_queue.append(job_id)
    stem_executor.submit(run_stem_job, job_id, url, mode, fmt)
    return jsonify(id=job_id)


@app.get("/api/status/<job_id>")
def status(job_id):
    if not authorized():
        abort(401)
    job = jobs.get(job_id)
    if not job:
        abort(404)
    result = {k: job.get(k) for k in ("status", "progress", "error", "name", "stems")}
    with jobs_lock:
        if job_id in stem_queue:
            result["queue_position"] = stem_queue.index(job_id) + 1
    return jsonify(result)


@app.get("/api/file/<job_id>")
def download(job_id):
    if not authorized():
        abort(401)
    job = jobs.get(job_id)
    if not job or job.get("status") != "done":
        abort(404)
    mimetype = "application/zip" if job["name"].endswith(".zip") else "audio/mpeg"
    return send_file(job["file"], as_attachment=True, download_name=job["name"],
                     mimetype=mimetype)


@app.after_request
def security_headers(resp):
    resp.headers["X-Content-Type-Options"] = "nosniff"
    resp.headers["X-Frame-Options"] = "DENY"
    resp.headers["Referrer-Policy"] = "no-referrer"
    return resp


WORK_DIR.mkdir(parents=True, exist_ok=True)
if not ensure_ffmpeg():
    sys.exit("ffmpeg not available. Run: pip install -r requirements.txt")
if not ACCESS_CODE:
    print("WARNING: VTM_ACCESS_CODE is not set. Anyone who finds the site can use it.")
print(f"Stem splitting: {'on (' + STEM_MODEL + ')' if STEMS_ENABLED else 'off (demucs not installed)'}")
threading.Thread(target=cleanup_loop, daemon=True).start()

if __name__ == "__main__":
    app.config["SESSION_COOKIE_SECURE"] = False
    app.run(host="127.0.0.1", port=int(os.environ.get("PORT", "8000")))
