# VideoToMP3

Command-line tool that downloads the audio from YouTube or other video links
and saves it as MP3. Built on [yt-dlp](https://github.com/yt-dlp/yt-dlp), so it
works with YouTube and the many other sites yt-dlp supports (Vimeo, SoundCloud,
Dailymotion, Bandcamp, Twitch clips, and pages with common embedded players).

## Requirements

- Python 3.9+
- `pip install -r requirements.txt`

ffmpeg is used from your PATH if installed. If not, the `static-ffmpeg`
package downloads a bundled copy on first run, so Homebrew is not needed
(useful on Intel Macs, which Homebrew no longer installs on).

### macOS quick setup

```sh
xcode-select --install          # provides python3
cd VideoToMP3
python3 -m venv .venv
source .venv/bin/activate
pip install -r requirements.txt
```

## Usage

```sh
# Single link
python3 video_to_mp3.py "https://www.youtube.com/watch?v=VIDEO_ID"

# Several links, 320 kbps, custom folder
python3 video_to_mp3.py URL1 URL2 -q 320 -o ~/Music/rips

# Links from a text file (one per line, # for comments)
python3 video_to_mp3.py -f links.txt

# Whole playlist
python3 video_to_mp3.py -p "https://www.youtube.com/playlist?list=LIST_ID"

# No arguments: prompts for a URL
python3 video_to_mp3.py
```

Options:

| Flag | Meaning |
|------|---------|
| `-o, --output DIR` | Output folder (default `./mp3_downloads`) |
| `-q, --quality N` | Bitrate: 96, 128, 160, 192, 256, 320 (default 192) |
| `-p, --playlist` | Download every item when the URL is a playlist |
| `-f, --file PATH` | Read URLs from a file |
| `--no-art` | Skip embedding the thumbnail as cover art |

Output files are named `Title [videoID].mp3` and include title/artist tags
and cover art.

## Troubleshooting

Sites change often. If downloads start failing, update yt-dlp first:
`pip install -U yt-dlp`.

## Legal

Only download content you own or have permission to download. YouTube's
Terms of Service prohibit downloading except where YouTube provides a
download option.

---

# Web version

A small website: paste a link, pick a quality, download the MP3.
Files live in `web/`; deployment files in `deploy/`.

Built-in protections (set in `/etc/videotomp3.env`):

| Setting | Default | Purpose |
|---------|---------|---------|
| `VTM_ACCESS_CODE` | none | Code visitors must enter. **Set this.** |
| `VTM_SECRET_KEY` | random | Signs login cookies. Set it so logins survive restarts. |
| `VTM_MAX_DURATION_MIN` | 20 | Rejects longer videos |
| `VTM_JOBS_PER_HOUR` | 20 | Per-visitor limit (uses Cloudflare's real visitor IP) |
| `VTM_FILE_TTL_MIN` | 30 | MP3s are deleted after this |
| `VTM_WORKERS` | 2 | Conversions run at the same time |

Links pointing to localhost or private networks are refused, so visitors
cannot use the site to reach machines inside your network.

## Try it locally

```sh
source .venv/bin/activate
pip install -r requirements.txt
VTM_ACCESS_CODE=test python3 web/app.py
# open http://127.0.0.1:8000
```

## Deploy on a Linux server (Debian/Ubuntu)

```sh
# 1. System packages and a service user
sudo apt install -y python3 python3-venv ffmpeg git
sudo useradd --system --home /opt/videotomp3 --shell /usr/sbin/nologin videotomp3

# 2. Code and Python packages
sudo git clone -b claude/zen-carson-02tyb3 https://github.com/someone2301/code.git /tmp/vtm
sudo cp -r /tmp/vtm/VideoToMP3 /opt/videotomp3
sudo python3 -m venv /opt/videotomp3/.venv
sudo /opt/videotomp3/.venv/bin/pip install -r /opt/videotomp3/requirements.txt
sudo chown -R videotomp3:videotomp3 /opt/videotomp3

# 3. Settings
sudo cp /opt/videotomp3/deploy/videotomp3.env.example /etc/videotomp3.env
sudo nano /etc/videotomp3.env          # set the access code and secret key
sudo chmod 600 /etc/videotomp3.env

# 4. Start the app (listens on 127.0.0.1:8000 only)
sudo cp /opt/videotomp3/deploy/videotomp3.service /etc/systemd/system/
sudo systemctl daemon-reload
sudo systemctl enable --now videotomp3
curl -I http://127.0.0.1:8000          # expect HTTP 200
```

## Put it behind Cloudflare (Cloudflare Tunnel)

A tunnel connects outward to Cloudflare, so you open no ports on your router
or firewall. Your domain must use Cloudflare DNS.

```sh
# Install cloudflared (see Cloudflare's docs for your distro), then:
cloudflared tunnel login
cloudflared tunnel create videotomp3          # prints the tunnel UUID
cloudflared tunnel route dns videotomp3 mp3.yourdomain.com

sudo mkdir -p /etc/cloudflared
sudo cp ~/.cloudflared/<UUID>.json /etc/cloudflared/
sudo cp /opt/videotomp3/deploy/cloudflared-config.yml.example /etc/cloudflared/config.yml
sudo nano /etc/cloudflared/config.yml         # fill in UUID and hostname

sudo cloudflared service install
sudo systemctl enable --now cloudflared
```

Then open `https://mp3.yourdomain.com`.

Recommended: add **Cloudflare Access** (Zero Trust > Access > Applications)
in front of the hostname so only emails you approve can reach the site.

## Stem splitting (optional)

Adds two options to the site: **vocals + instrumental** and **vocals, drums,
bass, other**. Uses [Demucs](https://github.com/adefossez/demucs) on your own
server. Visitors get a ZIP of MP3 320 kbps or WAV stems.

```sh
# 1. CPU-only PyTorch (about 1 GB; the default build adds ~4 GB of GPU libraries)
sudo /opt/videotomp3/.venv/bin/pip install torch torchaudio \
    --index-url https://download.pytorch.org/whl/cpu
# 2. Demucs
sudo /opt/videotomp3/.venv/bin/pip install -r /opt/videotomp3/requirements-stems.txt
sudo chown -R videotomp3:videotomp3 /opt/videotomp3

# 3. Download the model once (about 80 MB) into the service's model folder
sudo mkdir -p /var/lib/videotomp3/models && sudo chown -R videotomp3:videotomp3 /var/lib/videotomp3
sudo -u videotomp3 env TORCH_HOME=/var/lib/videotomp3/models/torch HF_HOME=/var/lib/videotomp3/models/hf \
    /opt/videotomp3/.venv/bin/python -c "from demucs.pretrained import get_model; get_model('htdemucs')"

# 4. Restart; the log should say "Stem splitting: on (htdemucs)"
sudo systemctl restart videotomp3
journalctl -u videotomp3 -n 20
```

How it behaves:

- One song is split at a time; others wait in a queue (max 5) and see their position.
- Expect several minutes per song on a 4-core CPU without a GPU.
- Songs longer than `VTM_STEM_MAX_DURATION_MIN` (default 10) are refused.
- Each visitor can split `VTM_STEM_JOBS_PER_HOUR` songs per hour (default 5).
- Demucs runs at low priority (`nice 10`) so the site stays responsive.
- `VTM_STEM_MODEL=htdemucs_ft` gives slightly better quality but is about 4x slower.

## Cleanup cron job (backup)

The app deletes files 30 minutes after a job finishes. This cron job also
removes anything left behind after a crash or restart:

```sh
sudo cp /opt/videotomp3/deploy/videotomp3-cleanup.cron /etc/cron.d/videotomp3-cleanup
```

Running stem jobs keep their folder's timestamp fresh, so the cron job never
deletes a job that is still being processed.

## Updating

```sh
sudo /opt/videotomp3/.venv/bin/pip install -U yt-dlp
sudo chown -R videotomp3:videotomp3 /opt/videotomp3
sudo systemctl restart videotomp3
```

## Known limits

- YouTube often blocks downloads from datacenter/VPS IP addresses
  ("Sign in to confirm you're not a bot"). A server on a home connection
  is less affected.
- Jobs are kept in memory; restarting the service cancels running jobs.
- Running a public stream-ripping site carries legal risk. Keep the access
  code set and share it only with people you trust.
