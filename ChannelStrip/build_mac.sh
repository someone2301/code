#!/usr/bin/env bash
# Builds Channel Strip (VST3, x86_64) on an Intel Mac and installs it to ~/Library/Audio/Plug-Ins/VST3.
set -euo pipefail
cd "$(dirname "$0")"

command -v cmake >/dev/null || { echo "cmake missing. Install with: brew install cmake"; exit 1; }
xcode-select -p >/dev/null 2>&1 || { echo "Command line tools missing. Install with: xcode-select --install"; exit 1; }

cmake -S . -B build -G "Unix Makefiles" -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release -j "$(sysctl -n hw.ncpu)"

PLUGIN="$HOME/Library/Audio/Plug-Ins/VST3/Channel Strip.vst3"
if [ -d "$PLUGIN" ]; then
    codesign --force --deep --sign - "$PLUGIN"
    echo "Installed: $PLUGIN"
    lipo -archs "$PLUGIN/Contents/MacOS/Channel Strip"
else
    echo "Build finished but the plugin was not found at $PLUGIN"
    echo "Look in build/ChannelStrip_artefacts/Release/VST3/"
    exit 1
fi
