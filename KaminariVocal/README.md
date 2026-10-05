# Kaminari Vocal

JUCE 8 / CMake plug-in by Kaminari Audio (VST3; AU on macOS; AAX optional). Design: `docs/KaminariVocal/DESIGN.md`.

## What this build contains

The three effect sends from DESIGN.md section 2.9, with In/Out gain and meters:

- **Reverb send**: 20 original algorithms (FDN, plate, nonlinear and ambience engines). Advanced controls are Decay, Size, Pre-delay, High/Low Cut, Mod Rate/Depth, Density, and Attack (Ambience and Nonlin only).
- **Delay send**:
  - Modes: Single, Dual, Ping-Pong.
  - Six styles, ms or tempo-synced note times.
  - Feedback, cuts, saturation, width, L/R offset, accent, groove, feel, prime numbers, wobble and diffusion.
- **Widener send**: MicroShift (Style I/II/III, Detune, Delay, Focus) or SideWidener (Width, Mode 1–3, Tone, Output). Only the selected one runs.

Every return is 100 % wet and is added to the unchanged dry vocal. Each send has On, a send level (Off … +6 dB) and a Pre/Post-fader tap. Latency is 0 samples.

The channel modules (Tune, EQ, Multiband, Compression, De-ess, Resonance) are specified in DESIGN.md but not built yet.

## Build

```bash
# plug-in (macOS: VST3 + AU, copied to ~/Library/Audio/Plug-Ins)
cmake -S KaminariVocal -B build-kv -DCMAKE_BUILD_TYPE=Release
cmake --build build-kv --config Release

# add AAX (Pro Tools Developer build only until the plug-in is PACE-signed)
cmake -S KaminariVocal -B build-kv -DKV_BUILD_AAX=ON

# tests (also writes editor screenshots when given a folder)
cmake -S KaminariVocal -B build-kv -DKV_BUILD_TESTS=ON
cmake --build build-kv --target KaminariVocalTests
./build-kv/KaminariVocalTests_artefacts/Release/KaminariVocalTests ./snapshots
```

On Linux, install the JUCE dependencies first, for example on Debian/Ubuntu:

```bash
sudo apt install libasound2-dev libx11-dev libxrandr-dev libxinerama-dev libxcursor-dev libxcomposite-dev libfreetype-dev libgl1-mesa-dev
```
