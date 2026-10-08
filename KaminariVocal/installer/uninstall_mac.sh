#!/usr/bin/env bash
# Removes the plug-in named in CMakeLists.txt (KV_PRODUCT_NAME: "Kaminari Vocal Alt" in this edition), system-wide and
# per-user copies. The other edition is left installed. User presets are kept unless --presets is given.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
PRODUCT="$(sed -n 's/^set(KV_PRODUCT_NAME "\(.*\)")$/\1/p' "$ROOT/CMakeLists.txt")"
ID_BASE="$(sed -n 's/^set(KV_BUNDLE_ID *\([^ )]*\))$/\1/p' "$ROOT/CMakeLists.txt")"
[ -n "$PRODUCT" ] && [ -n "$ID_BASE" ] || { echo "KV_PRODUCT_NAME / KV_BUNDLE_ID not found in CMakeLists.txt"; exit 1; }
TARGETS=(
  "/Library/Audio/Plug-Ins/VST3/$PRODUCT.vst3"
  "/Library/Audio/Plug-Ins/Components/$PRODUCT.component"
  "/Library/Application Support/Avid/Audio/Plug-Ins/$PRODUCT.aaxplugin"
  "$HOME/Library/Audio/Plug-Ins/VST3/$PRODUCT.vst3"
  "$HOME/Library/Audio/Plug-Ins/Components/$PRODUCT.component"
)
for t in "${TARGETS[@]}"; do
    if [ -e "$t" ]; then
        echo "Removing $t"
        if [ -w "$(dirname "$t")" ]; then rm -rf "$t"; else sudo rm -rf "$t"; fi
    fi
done
if [ "${1:-}" = "--presets" ]; then
    rm -rf "$HOME/Library/Audio/Presets/Kaminari Audio/$PRODUCT"
    echo "Removed user presets."
fi
for id in vst3 au aax; do sudo pkgutil --forget "$ID_BASE.$id" 2>/dev/null || true; done
killall -9 AudioComponentRegistrar 2>/dev/null || true
echo "Done."
