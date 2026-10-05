#!/usr/bin/env bash
# Removes Kaminari Vocal (system-wide and per-user copies). User presets are kept unless --presets is given.
set -euo pipefail
TARGETS=(
  "/Library/Audio/Plug-Ins/VST3/Kaminari Vocal.vst3"
  "/Library/Audio/Plug-Ins/Components/Kaminari Vocal.component"
  "/Library/Application Support/Avid/Audio/Plug-Ins/Kaminari Vocal.aaxplugin"
  "$HOME/Library/Audio/Plug-Ins/VST3/Kaminari Vocal.vst3"
  "$HOME/Library/Audio/Plug-Ins/Components/Kaminari Vocal.component"
)
for t in "${TARGETS[@]}"; do
    if [ -e "$t" ]; then
        echo "Removing $t"
        if [ -w "$(dirname "$t")" ]; then rm -rf "$t"; else sudo rm -rf "$t"; fi
    fi
done
if [ "${1:-}" = "--presets" ]; then
    rm -rf "$HOME/Library/Audio/Presets/Kaminari Audio/Kaminari Vocal"
    echo "Removed user presets."
fi
for id in vst3 au aax; do sudo pkgutil --forget "com.kaminariaudio.kaminarivocal.$id" 2>/dev/null || true; done
killall -9 AudioComponentRegistrar 2>/dev/null || true
echo "Done."
