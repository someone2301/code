#!/usr/bin/env bash
# Builds the plug-in (VST3 + AU, Intel x86_64) on a Mac and copies it to ~/Library/Audio/Plug-Ins. The product name
# and plug-in code come from CMakeLists.txt (KV_PRODUCT_NAME, KV_PLUGIN_CODE), so this builds whichever edition is checked out.
# Usage: ./build_mac.sh            (add WITH_AAX=1 for an AAX build for the Pro Tools Developer build)
set -euo pipefail
cd "$(dirname "$0")"

command -v cmake >/dev/null || { echo "cmake is missing. Install it with: brew install cmake"; exit 1; }
xcode-select -p >/dev/null 2>&1 || { echo "Command line tools are missing. Install them with: xcode-select --install"; exit 1; }

PRODUCT="$(sed -n 's/^set(KV_PRODUCT_NAME "\(.*\)")$/\1/p' CMakeLists.txt)"
CODE="$(sed -n 's/^set(KV_PLUGIN_CODE *\([^ )]*\))$/\1/p' CMakeLists.txt)"
[ -n "$PRODUCT" ] && [ -n "$CODE" ] || { echo "KV_PRODUCT_NAME / KV_PLUGIN_CODE not found in CMakeLists.txt"; exit 1; }

AAX_FLAG=OFF
[ "${WITH_AAX:-0}" = "1" ] && AAX_FLAG=ON

cmake -S . -B build -G "Unix Makefiles" -DCMAKE_BUILD_TYPE=Release -DKV_BUILD_AAX="$AAX_FLAG"
cmake --build build --config Release -j "$(sysctl -n hw.ncpu)"

VST3="$HOME/Library/Audio/Plug-Ins/VST3/$PRODUCT.vst3"
AU="$HOME/Library/Audio/Plug-Ins/Components/$PRODUCT.component"
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
    auval -v aufx "$CODE" Kmni | tail -n 5 || echo "auval reported a problem; see the output above."
fi
