# Kaminari Vocal Alt

JUCE 8 / CMake plug-in by Kaminari Audio (VST3; AU on macOS; AAX optional). Design: `docs/KaminariVocal/DESIGN.md`.
This branch builds the **Alt** edition. It installs next to the full Kaminari Vocal (its own plug-in code, bundle ID and
preset folder). `docs/KaminariVocal/VERSIONS.md` lists both editions, where the full version's source and installer are
kept, and how they differ.

## What this build contains

Channel modules, in processing order: Tune, 8-band EQ, Multiband (1-6 bands), Compression, De-ess, Resonance.

- **Tune**: the original pitch-correction algorithm (fixed 96-sample delay at 48 kHz), Key/Scale/Range, Retune Speed,
  Humanize, Detune, Correct Pitch, vibrato and tremolo.
- **Compression**: one optical, LA-2A style mode. Compression (peak reduction) and Gain (makeup), plus the side-chain
  detection EQ. In the Basic view the hammer sets Compression and the slider beside it sets Gain.
- **De-ess**: Frequency (esses above it are detected and only that range is turned down) and Range.
- EQ, Multiband and Resonance as in the full edition; Multiband bands are drawn like EQ bands.

Presets:
- Each module page has its own module preset menu, and the header has chain presets for the whole plug-in.
- `Presets/factory.json` holds the module presets for all six modules and 12 chain presets.
- User presets save to `~/Library/Audio/Presets/Kaminari Audio/Kaminari Vocal Alt/`.

Mono, mono-to-stereo and stereo tracks are supported. Reported latency: 96 samples at 48 kHz (2 ms), plus Multiband
or Resonance oversampling while those modules are on.

## Install on your Mac

```bash
# one-time: Xcode command line tools and CMake
xcode-select --install
brew install cmake

# from the repository folder:
./KaminariVocal/installer/make_installer.sh        # builds dist/KaminariVocalAlt-<version>.pkg (VST3 + AU)
open KaminariVocal/dist/KaminariVocalAlt-*.pkg     # run the installer

# or, without an installer, build and copy to ~/Library/Audio/Plug-Ins:
./KaminariVocal/build_mac.sh
```

Add `WITH_AAX=1` in front of either command to include AAX (Pro Tools Developer build only until PACE-signed).
To update later: pull the new code and run the installer again. It upgrades in place.
Uninstall: `./KaminariVocal/installer/uninstall_mac.sh` (removes this edition only).

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
