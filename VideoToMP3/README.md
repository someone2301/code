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

## Stem splitting

Two modes, both using [Demucs](https://github.com/adefossez/demucs) on your server:

| Mode | Files | Trade-off |
|------|-------|-----------|
| **Basic** | vocals, drums, bass, other | Faster, simpler, fewest artifacts. Guitar, keys, synths and anything else stay together in "other". |
| **Advanced** | the same 4 stems **plus** only the detailed splits picked | More parts to work with, but slower and more likely to have bleed or artifacts. |

Advanced options (each one must be installed **and** switched on, see below):

| Option | Extra files | Model | Status |
|--------|-------------|-------|--------|
| Drum kit parts | `drums - kick/snare/toms/hi-hat/cymbals` | LarsNet on the Demucs drum stem | Code path tested; real weights not yet verified on a song |
| Lead / background vocals (experimental) | `vocals - lead`, `vocals - background` | Mel-Band RoFormer karaoke (aufr33 & viperx) on the full mix; background = vocals - lead | Verified on test mixes (see below) |
| Guitar and piano (experimental) | `experimental - guitar`, `experimental - piano` | Second Demucs pass with `htdemucs_6s`; only guitar and piano kept | Not verified (model could not be downloaded in the build sandbox) |

- The 4 main stems are always in the ZIP, and the full `vocals` and `drums`
  stems stay next to their detailed parts.
- Guitar/piano files overlap with `other`; nothing is subtracted from `other`.
  Demucs' authors call the 6-source model experimental and say piano has a lot
  of bleeding and artifacts.
- **No synth stem.** No model tested here separates synths reliably, so synths stay in `other`.
- Every ZIP also has a `README.txt` listing its files, any optional split that
  failed, and the model credits (LarsNet's license requires attribution).
- All WAVs in a ZIP have the same length, channel count (stereo), sample rate
  and bit depth, and start at the same time. The app checks this before
  offering the download.
- If an optional split fails, the job still finishes with the other files and
  the page says which split failed.

Output format: WAV only. **Bit depth:** 16-bit (dithered), **24-bit (default)**,
or 32-bit float. **Sample rate:** **44.1 kHz (default)**, 48, 88.2 or 96 kHz.
Every model works at 44.1 kHz; other rates are resampled at the end, so rates
above 44.1 kHz add no detail.

### Basic install

```sh
# 1. CPU-only PyTorch (about 1 GB; the default build adds ~4 GB of GPU libraries)
sudo /opt/videotomp3/.venv/bin/pip install torch torchaudio \
    --index-url https://download.pytorch.org/whl/cpu
# 2. Demucs
sudo /opt/videotomp3/.venv/bin/pip install -r /opt/videotomp3/requirements-stems.txt
sudo chown -R videotomp3:videotomp3 /opt/videotomp3

# 3. Download the models once into the service's model folder
sudo mkdir -p /var/lib/videotomp3/models && sudo chown -R videotomp3:videotomp3 /var/lib/videotomp3
sudo -u videotomp3 env TORCH_HOME=/var/lib/videotomp3/models/torch HF_HOME=/var/lib/videotomp3/models/hf \
    /opt/videotomp3/.venv/bin/python -c "from demucs.pretrained import get_model; get_model('htdemucs'); get_model('htdemucs_6s')"

# 4. Restart; the log lists Basic and each Advanced option with on/off and why
sudo systemctl restart videotomp3
journalctl -u videotomp3 -n 20
```

General behaviour: one stem job runs at a time and others queue (max 5, with
their position shown); songs over `VTM_STEM_MAX_DURATION_MIN` (default 10) are
refused; each visitor gets `VTM_STEM_JOBS_PER_HOUR` jobs (default 5); every
model runs at low priority (`nice 10`) in its own process, started only if its
step was selected, so its memory is freed when the step ends.

### Advanced option: drum kit parts (LarsNet)

**License:** LarsNet's weights are CC BY-NC 4.0: non-commercial use, with
attribution (the ZIP's README.txt carries it). LarsNet's code repository has
no license file, so it is downloaded at install rather than copied here.

```sh
# 1. LarsNet code, pinned to a tested version
sudo apt install -y unzip
sudo git clone https://github.com/polimi-ispl/larsnet.git /opt/larsnet
sudo git -C /opt/larsnet checkout 17d631fd18e77ee2f1d23ee7b3fc0fb46ae629e2
sudo /opt/videotomp3/.venv/bin/pip install -r /opt/videotomp3/requirements-drums.txt

# 2. Pretrained weights (562 MB, hosted on Google Drive by the authors)
sudo /opt/videotomp3/.venv/bin/pip install gdown
sudo /opt/videotomp3/.venv/bin/gdown 1U8-5924B1ii1cjv9p0MTPzayb00P4qoL -O /tmp/larsnet_models.zip
#    (If gdown fails, download the zip in a browser from the link in LarsNet's
#     README and copy it to the server as /tmp/larsnet_models.zip.)
sudo unzip -q /tmp/larsnet_models.zip -d /tmp/larsnet_models
KICK=$(find /tmp/larsnet_models -name pretrained_kick_unet.pth)
sudo cp -r "$(dirname "$(dirname "$KICK")")"/. /opt/larsnet/pretrained_larsnet_models/
ls /opt/larsnet/pretrained_larsnet_models/*/    # expect 5 folders, one .pth each

# 3. One-time conversion so the site loads the weights in PyTorch's safe mode
sudo /opt/videotomp3/.venv/bin/python -I /opt/videotomp3/web/drum_split.py \
    --larsnet-dir /opt/larsnet --prepare-weights
```

Measured in the build sandbox with same-size stand-in weights: about 50 s and
3.1 GB peak memory for a 4-minute drum stem on 4 CPU cores. LarsNet was trained
on synthesized drum kits; expect some bleed between hi-hat and cymbals, and
between snare and toms. `VTM_DRUM_BATCH` (default 4) lowers memory if reduced.

### Advanced option: lead / background vocals

**How it works:** karaoke models are trained on full songs, so the model is
run on the **full mix** to get the lead vocal, and
`background = Demucs vocals - lead`. Running it on the vocal stem alone was
tested and rejected: it split a solo singer roughly in half.

**Verified** in the build sandbox on mixes built from real CC BY singing
recordings with a known lead and background, over an instrumental (higher is
better; "before" is the vocal stem as-is):

| Test | Lead SDR | Background SDR |
|------|----------|----------------|
| Same singer + 2 panned harmonies | 28.8 dB (before 11.0) | 17.7 dB |
| Male lead + different female backing | 22.0 dB (before 8.2) | 13.8 dB |
| Lead only, no backing | 29.7 dB | background came out 30 dB below the lead |
| Unison double (same notes) | not separated; stays in lead (expected limit) | |

**License:** the authors (aufr33 and viperx) have not published a license for
these weights, and no other karaoke model checked has one either. UVR's own
`UVR_MDXNET_KARA_2` has usage terms (credit UVR) but separated poorly in the
same tests, so it is not used. Enable this option only if you accept that.

It runs in its own virtualenv because audio-separator pins different library
versions than Demucs:

```sh
sudo python3 -m venv /opt/videotomp3/.venv-vocals
sudo /opt/videotomp3/.venv-vocals/bin/pip install torch \
    --index-url https://download.pytorch.org/whl/cpu
sudo /opt/videotomp3/.venv-vocals/bin/pip install -r /opt/videotomp3/requirements-vocals.txt
sudo mkdir -p /var/lib/videotomp3/models/audio-separator
sudo /opt/videotomp3/.venv-vocals/bin/audio-separator --download_model_only \
    --model_file_dir /var/lib/videotomp3/models/audio-separator \
    -m mel_band_roformer_karaoke_aufr33_viperx_sdr_10.1956.ckpt     # about 913 MB
sudo chown -R videotomp3:videotomp3 /opt/videotomp3 /var/lib/videotomp3
```

Measured in the build sandbox (4-core Xeon 2.8 GHz, no GPU), 60 s of audio:

| `VTM_VOCAL_OVERLAP` | Time | Lead SDR on test mix |
|---|---|---|
| 4 (model default) | 510 s (8.5x real time) | 29.0 dB |
| **2 (default here)** | 265 s (4.4x real time) | 29.0 dB |
| 1 | 158 s (2.6x real time), 2.6 GB peak memory | 28.4 dB |

So a 4-minute song needs roughly 18 minutes for this step alone at overlap 2
on that CPU, on top of Demucs. A full 4-minute run at overlap 4 was stopped
after 30 minutes without finishing. Its timeout is `VTM_VOCAL_TIMEOUT_MIN`
(default 60). Run `tools/stem_benchmark.py` for your server's real figures.

### Advanced option: guitar and piano (experimental)

Uses `htdemucs_6s` (downloaded in Basic install step 3). It is a full second
Demucs pass, so it roughly doubles the Basic time. Only its guitar and piano
outputs are kept, as extra files; the 4 main stems still come from `htdemucs`.

### Verify on your server, then switch options on

Every Advanced option is **off** until you switch it on in `/etc/videotomp3.env`.
First run the benchmark on several varied songs you own (about 4 minutes each).
It uses the same code as the site and records time, peak memory, failures,
and format/alignment checks for each step:

```sh
sudo -u videotomp3 bash -c 'set -a; . /etc/videotomp3.env; set +a;
  /opt/videotomp3/.venv/bin/python /opt/videotomp3/tools/stem_benchmark.py \
    --options drums,vocals,guitar_piano --out /var/lib/videotomp3/stem-report.md \
    /path/to/song1.flac /path/to/song2.mp3 /path/to/song3.wav'
```

Listen to the results, then enable only the options you are happy with and
restart:

```sh
VTM_ENABLE_DRUM_SPLIT=1
VTM_ENABLE_VOCAL_SPLIT=1
VTM_ENABLE_GUITAR_PIANO=1
```

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
