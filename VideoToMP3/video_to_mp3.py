#!/usr/bin/env python3
"""Download audio from YouTube or other video pages and convert it to MP3.

Uses yt-dlp, which supports YouTube plus over a thousand other sites
(Vimeo, SoundCloud, Dailymotion, Twitch clips, many embedded players, etc.).
Uses ffmpeg from PATH, or the static-ffmpeg pip package if it is missing.
"""

import argparse
import shutil
import sys
from pathlib import Path

try:
    import yt_dlp
except ImportError:
    sys.exit("yt-dlp is not installed. Run: pip install -r requirements.txt")


def build_options(out_dir: Path, bitrate: str, playlist: bool, embed_art: bool) -> dict:
    postprocessors = [
        {"key": "FFmpegExtractAudio", "preferredcodec": "mp3", "preferredquality": bitrate},
        {"key": "FFmpegMetadata", "add_metadata": True},
    ]
    if embed_art:
        postprocessors.append({"key": "EmbedThumbnail"})

    return {
        "format": "bestaudio/best",
        "outtmpl": str(out_dir / "%(title)s [%(id)s].%(ext)s"),
        "restrictfilenames": False,
        "windowsfilenames": True,
        "noplaylist": not playlist,
        "writethumbnail": embed_art,
        "postprocessors": postprocessors,
        "ignoreerrors": True,
        "retries": 5,
        "quiet": False,
        "no_warnings": False,
    }


def ensure_ffmpeg() -> bool:
    """Use ffmpeg from PATH, or fall back to the static-ffmpeg pip package."""
    if shutil.which("ffmpeg") and shutil.which("ffprobe"):
        return True
    try:
        import static_ffmpeg
    except ImportError:
        return False
    print("Using bundled ffmpeg (downloads once on first run)...")
    try:
        static_ffmpeg.add_paths(weak=True)
    except Exception as exc:
        print(f"Could not download bundled ffmpeg: {exc}")
        return False
    return bool(shutil.which("ffmpeg") and shutil.which("ffprobe"))


def read_url_file(path: Path) -> list[str]:
    lines = path.read_text(encoding="utf-8").splitlines()
    return [ln.strip() for ln in lines if ln.strip() and not ln.strip().startswith("#")]


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Download YouTube or other embedded video links and convert them to MP3."
    )
    parser.add_argument("urls", nargs="*", help="One or more video page URLs")
    parser.add_argument("-f", "--file", type=Path, help="Text file with one URL per line")
    parser.add_argument("-o", "--output", type=Path, default=Path("mp3_downloads"),
                        help="Output directory (default: ./mp3_downloads)")
    parser.add_argument("-q", "--quality", default="192",
                        choices=["96", "128", "160", "192", "256", "320"],
                        help="MP3 bitrate in kbps (default: 192)")
    parser.add_argument("-p", "--playlist", action="store_true",
                        help="Download the whole playlist when a URL points to one")
    parser.add_argument("--no-art", action="store_true",
                        help="Do not embed the video thumbnail as cover art")
    args = parser.parse_args()

    urls = list(args.urls)
    if args.file:
        urls += read_url_file(args.file)
    if not urls:
        try:
            entered = input("Paste a video URL: ").strip()
        except EOFError:
            entered = ""
        if entered:
            urls.append(entered)
    if not urls:
        parser.error("no URLs given")

    if not ensure_ffmpeg():
        sys.exit("ffmpeg was not found. Run: pip install -r requirements.txt")

    args.output.mkdir(parents=True, exist_ok=True)
    opts = build_options(args.output, args.quality, args.playlist, not args.no_art)

    with yt_dlp.YoutubeDL(opts) as ydl:
        code = ydl.download(urls)

    print(f"\nDone. Files saved to: {args.output.resolve()}")
    return 1 if code else 0


if __name__ == "__main__":
    sys.exit(main())
