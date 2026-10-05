"""Web front end for VideoToMP3.

Run locally:   python3 web/app.py
Production:    gunicorn -w 1 --threads 8 -b 127.0.0.1:8000 web.app:app
(Use one worker process: jobs are tracked in memory.)
"""

import hmac
import ipaddress
import os
import socket
import secrets
import shutil
import sys
import tempfile
import threading
import time
import uuid
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

app = Flask(__name__)
app.secret_key = os.environ.get("VTM_SECRET_KEY") or secrets.token_hex(32)
app.config.update(SESSION_COOKIE_HTTPONLY=True, SESSION_COOKIE_SAMESITE="Lax",
                  SESSION_COOKIE_SECURE=os.environ.get("VTM_SECURE_COOKIE", "1") == "1")

executor = ThreadPoolExecutor(max_workers=WORKERS)
jobs: dict[str, dict] = {}
jobs_lock = threading.Lock()
recent_by_ip: dict[str, deque] = defaultdict(deque)


def client_ip() -> str:
    # Cloudflare passes the visitor's real IP in this header.
    return request.headers.get("CF-Connecting-IP") or request.remote_addr or "?"


def authorized() -> bool:
    return not ACCESS_CODE or session.get("ok") is True


def rate_limited(ip: str) -> bool:
    now = time.time()
    q = recent_by_ip[ip]
    while q and now - q[0] > 3600:
        q.popleft()
    if len(q) >= JOBS_PER_HOUR:
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


def too_long(info, *, incomplete):
    duration = info.get("duration")
    if duration and duration > MAX_DURATION_MIN * 60:
        return f"Video is longer than {MAX_DURATION_MIN} minutes"
    return None


def run_job(job_id: str, url: str, quality: str) -> None:
    job_dir = WORK_DIR / job_id
    job_dir.mkdir(parents=True, exist_ok=True)

    def progress(d):
        if d.get("status") == "downloading":
            total = d.get("total_bytes") or d.get("total_bytes_estimate")
            if total:
                jobs[job_id]["progress"] = int(d.get("downloaded_bytes", 0) * 100 / total)
        elif d.get("status") == "finished":
            jobs[job_id]["status"] = "converting"

    opts = build_options(job_dir, quality, playlist=False, embed_art=True)
    opts.update(quiet=True, no_warnings=True, ignoreerrors=False, noprogress=True,
                progress_hooks=[progress], match_filter=too_long)
    jobs[job_id]["status"] = "downloading"
    try:
        with yt_dlp.YoutubeDL(opts) as ydl:
            ydl.download([url])
        mp3s = list(job_dir.glob("*.mp3"))
        if not mp3s:
            raise RuntimeError(f"Skipped: video may be longer than {MAX_DURATION_MIN} minutes")
        jobs[job_id].update(status="done", progress=100, file=str(mp3s[0]), name=mp3s[0].name)
    except Exception as exc:  # noqa: BLE001
        msg = str(exc).replace("ERROR: ", "").split("\n")[0]
        msg = msg.split("; please report this issue")[0].split(" (caused by")[0][:300]
        jobs[job_id].update(status="error", error=msg)
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
    return render_template("index.html", max_min=MAX_DURATION_MIN, ttl_min=FILE_TTL_SEC // 60)


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
    if not url.startswith(("http://", "https://")) or len(url) > 2000:
        return jsonify(error="Enter a valid http(s) link."), 400
    if not ALLOW_PRIVATE and points_to_private_network(url):
        return jsonify(error="That link is not allowed."), 400
    if quality not in ALLOWED_QUALITIES:
        quality = "192"
    if rate_limited(client_ip()):
        return jsonify(error="Too many requests. Try again later."), 429

    job_id = uuid.uuid4().hex
    with jobs_lock:
        jobs[job_id] = {"status": "queued", "progress": 0}
    executor.submit(run_job, job_id, url, quality)
    return jsonify(id=job_id)


@app.get("/api/status/<job_id>")
def status(job_id):
    if not authorized():
        abort(401)
    job = jobs.get(job_id)
    if not job:
        abort(404)
    return jsonify({k: job.get(k) for k in ("status", "progress", "error", "name")})


@app.get("/api/file/<job_id>")
def download(job_id):
    if not authorized():
        abort(401)
    job = jobs.get(job_id)
    if not job or job.get("status") != "done":
        abort(404)
    return send_file(job["file"], as_attachment=True, download_name=job["name"],
                     mimetype="audio/mpeg")


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
threading.Thread(target=cleanup_loop, daemon=True).start()

if __name__ == "__main__":
    app.config["SESSION_COOKIE_SECURE"] = False
    app.run(host="127.0.0.1", port=int(os.environ.get("PORT", "8000")))
