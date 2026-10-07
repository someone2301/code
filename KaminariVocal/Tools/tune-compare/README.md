# Tune comparison tools

Compare Kaminari Vocal's tuner with renders of the same dry vocal through another tuner.

1. Build the offline renderer: `g++ -O2 -std=c++17 -I../../Source render.cpp -o render`
2. Render the dry vocal at matching settings, e.g. hard tune in G minor:
   `./render dry.wav ours.wav 0 0 0 7 2 1` (Tracking, Retune Speed 0 ms, Humanize 0 %, key G, Natural Minor, Middle range)
3. Compare (needs numpy):
   - `python3 analyze.py dry.wav reference.wav ours.wav`: deviation from the scale, jitter
   - `python3 analyze.py compare dry.wav reference.wav ours.wav --frames`: roughness and note agreement
   - `python3 slope.py dry.wav reference.wav ours.wav`: how much correction is applied during glides

The first reference set (a dry G minor vocal and nine renders at different Retune and Humanize settings) is on the
branch `claude/gain-vst-mac-intel-fjdno4` under `KaminariVocal/`.
