#!/usr/bin/env bash
# Removes Channel Strip (system-wide and per-user copies).
set -euo pipefail
for dir in "/Library/Audio/Plug-Ins/VST3" "$HOME/Library/Audio/Plug-Ins/VST3"; do
    if [ -d "$dir/Channel Strip.vst3" ]; then
        echo "Removing $dir/Channel Strip.vst3"
        if [ -w "$dir" ]; then rm -rf "$dir/Channel Strip.vst3"; else sudo rm -rf "$dir/Channel Strip.vst3"; fi
    fi
done
sudo pkgutil --forget com.local.channelstrip.vst3 2>/dev/null || true
echo "Done."
