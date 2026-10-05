#!/usr/bin/env bash
# Builds Kaminari Vocal (VST3 + AU, Intel x86_64) on a Mac and copies it to ~/Library/Audio/Plug-Ins.
# Usage: ./build_mac.sh            (add WITH_AAX=1 for an AAX build for the Pro Tools Developer build)
set -euo pipefail
cd "$(dirname "$0")"

command -v cmake >/dev/null || { echo "cmake is missing. Install it with: brew install cmake"; exit 1; }
xcode-select -p >/dev/null 2>&1 || { echo "Command line tools are missing. Install them with: xcode-select --install"; exit 1; }

AAX_FLAG=OFF
[ "${WITH_AAX:-0}" = "1" ] && AAX_FLAG=ON

cmake -S . -B build -G "Unix Makefiles" -DCMAKE_BUILD_TYPE=Release -DKV_BUILD_AAX="$AAX_FLAG"
cmake --build build --config Release -j "$(sysctl -n hw.ncpu)"

VST3="$HOME/Library/Audio/Plug-Ins/VST3/Kaminari Vocal.vst3"
AU="$HOME/Library/Audio/Plug-Ins/Components/Kaminari Vocal.component"
for p in "$VST3" "$AU"; do
    if [ -d "$p" ]; then
        codesign --force --deep --sign - "$p"
        echo "Installed: $p"
    else
        echo "Not found after build: $p"
    fi
done

# Refresh the Audio Unit cache and validate the AU.
killall -9 AudioComponentRegistrar 2>/dev/null || true
if command -v auval >/dev/null; then
    echo "Validating the Audio Unit (auval)..."
    auval -v aufx KmVc Kmni | tail -n 5 || echo "auval reported a problem; see the output above."
fi
