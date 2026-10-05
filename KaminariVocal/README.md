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

Presets:
- Each send panel has its own module preset menu, and the header has chain presets for the whole plug-in.
- `Presets/factory.json` holds 68 module presets for all nine modules and 11 chain presets.
- User presets save to `~/Library/Audio/Presets/Kaminari Audio/Kaminari Vocal/`.

Every return is 100 % wet and is added to the unchanged dry vocal. Each send has On, a send level (Off … +6 dB) and a Pre/Post-fader tap. Latency is 0 samples.

Channel modules (first versions, see DESIGN.md 2.12): Tune, 8-band EQ, Multiband (1-6 bands), Compression, De-ess and Resonance. Mono, mono-to-stereo and stereo tracks are supported. Reported latency: 96 samples at 48 kHz, plus any lookahead.

## Install on your Mac

```bash
# one-time: Xcode command line tools and CMake
xcode-select --install
brew install cmake

# from the repository folder:
./KaminariVocal/installer/make_installer.sh        # builds dist/KaminariVocal-<version>.pkg (VST3 + AU)
open KaminariVocal/dist/KaminariVocal-*.pkg        # run the installer

# or, without an installer, build and copy to ~/Library/Audio/Plug-Ins:
./KaminariVocal/build_mac.sh
```

Add `WITH_AAX=1` in front of either command to include AAX (Pro Tools Developer build only until PACE-signed).
To update later: pull the new code and run the installer again. It upgrades in place.
Uninstall: `./KaminariVocal/installer/uninstall_mac.sh`.

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
