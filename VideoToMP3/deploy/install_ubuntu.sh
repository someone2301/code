#!/usr/bin/env bash
# Install or update VideoToMP3 (MP3 + Basic 4-stem splitting) on Ubuntu.
#
# Usage, from a checkout of this repository on the server:
#     sudo bash VideoToMP3/deploy/install_ubuntu.sh
#
# Safe to run again: it updates the code and packages and keeps your settings.
# Advanced stem options are not installed; see README for those.
set -euo pipefail

APP_DIR=/opt/videotomp3
DATA_DIR=/var/lib/videotomp3
ENV_FILE=/etc/videotomp3.env
SVC_USER=videotomp3
SRC_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

step() { printf '\n==> %s\n' "$*"; }
die() { printf 'ERROR: %s\n' "$*" >&2; exit 1; }

[ "$(id -u)" -eq 0 ] || die "run with sudo"
[ -f "$SRC_DIR/web/app.py" ] || die "run this script from inside the repository checkout"
grep -qi ubuntu /etc/os-release || echo "WARNING: not Ubuntu; continuing anyway"
[ "$(uname -m)" = "x86_64" ] || die "this script supports x86_64 only"

step "Installing system packages"
export DEBIAN_FRONTEND=noninteractive
apt-get update -q
apt-get install -y -q python3 python3-venv python3-pip ffmpeg git unzip curl ca-certificates rsync

step "Installing Deno (JavaScript runtime yt-dlp needs for YouTube)"
if ! command -v deno >/dev/null; then
    tmp=$(mktemp -d)
    curl -fsSL -o "$tmp/deno.zip" \
        https://github.com/denoland/deno/releases/latest/download/deno-x86_64-unknown-linux-gnu.zip
    unzip -q -o "$tmp/deno.zip" -d /usr/local/bin
    chmod 755 /usr/local/bin/deno
    rm -rf "$tmp"
fi
deno --version | head -1

step "Creating service user and folders"
id "$SVC_USER" >/dev/null 2>&1 || useradd --system --home "$APP_DIR" --shell /usr/sbin/nologin "$SVC_USER"
mkdir -p "$APP_DIR" "$DATA_DIR/models"

step "Copying application code to $APP_DIR"
rsync -a --delete --exclude .venv --exclude mp3_downloads --exclude __pycache__ "$SRC_DIR/" "$APP_DIR/"

step "Installing Python packages (this downloads about 1 GB the first time)"
[ -x "$APP_DIR/.venv/bin/python" ] || python3 -m venv "$APP_DIR/.venv"
PIP="$APP_DIR/.venv/bin/pip"
"$PIP" install -q --upgrade pip
# CPU-only PyTorch: the default build adds ~4 GB of GPU libraries this server cannot use.
"$PIP" install -q torch torchaudio --index-url https://download.pytorch.org/whl/cpu
"$PIP" install -q -r "$APP_DIR/requirements.txt" -r "$APP_DIR/requirements-stems.txt"
"$PIP" install -q --upgrade "yt-dlp[default]"

step "Settings file $ENV_FILE"
if [ ! -f "$ENV_FILE" ]; then
    code=$(python3 -c "import secrets; print(secrets.token_urlsafe(12))")
    key=$(python3 -c "import secrets; print(secrets.token_hex(32))")
    sed -e "s|^VTM_ACCESS_CODE=.*|VTM_ACCESS_CODE=$code|" \
        -e "s|^VTM_SECRET_KEY=.*|VTM_SECRET_KEY=$key|" \
        "$APP_DIR/deploy/videotomp3.env.example" > "$ENV_FILE"
    chmod 600 "$ENV_FILE"
    NEW_CODE=$code
else
    echo "Keeping existing settings."
fi

chown -R "$SVC_USER:$SVC_USER" "$APP_DIR" "$DATA_DIR"

step "Downloading the Demucs model (about 80 MB)"
sudo -u "$SVC_USER" env TORCH_HOME="$DATA_DIR/models/torch" HF_HOME="$DATA_DIR/models/hf" \
    "$APP_DIR/.venv/bin/python" -c "from demucs.pretrained import get_model; get_model('htdemucs')"

step "Installing the service and cleanup job"
cp "$APP_DIR/deploy/videotomp3.service" /etc/systemd/system/videotomp3.service
cp "$APP_DIR/deploy/videotomp3-cleanup.cron" /etc/cron.d/videotomp3-cleanup
chmod 644 /etc/cron.d/videotomp3-cleanup
systemctl daemon-reload
systemctl enable videotomp3 >/dev/null
systemctl restart videotomp3

step "Checking that the site responds"
for _ in $(seq 1 30); do
    if curl -fsS -o /dev/null http://127.0.0.1:8000/; then ok=1; break; fi
    sleep 2
done
[ "${ok:-}" = 1 ] || { journalctl -u videotomp3 -n 30 --no-pager; die "site did not start"; }
journalctl -u videotomp3 -n 20 --no-pager | grep -E "Stem splitting|Advanced option|WARNING" || true

echo
echo "Done. The site is running on http://127.0.0.1:8000 (this server only)."
if [ -n "${NEW_CODE:-}" ]; then
    echo "Access code: $NEW_CODE   (stored in $ENV_FILE)"
fi
echo "Next: put it behind Cloudflare Tunnel (README, 'Put it behind Cloudflare')."
