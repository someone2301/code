#!/usr/bin/env bash
# Builds Kaminari Vocal and packages it as dist/KaminariVocal-<version>.pkg (macOS only).
#
# The installer offers VST3 and AU (and AAX when built with WITH_AAX=1) and installs them for all users:
#   /Library/Audio/Plug-Ins/VST3/Kaminari Vocal.vst3
#   /Library/Audio/Plug-Ins/Components/Kaminari Vocal.component
#   /Library/Application Support/Avid/Audio/Plug-Ins/Kaminari Vocal.aaxplugin
# Running a newer installer over an older one upgrades in place; sessions and user presets are kept.
#
# Optional environment variables:
#   WITH_AAX=1               also build and package AAX (runs in Pro Tools Developer until PACE-signed)
#   CODESIGN_IDENTITY        "Developer ID Application: ..." to sign the plug-ins (default: ad-hoc)
#   INSTALLER_SIGN_IDENTITY  "Developer ID Installer: ..." to sign the .pkg
#   NOTARY_PROFILE           notarytool keychain profile; submits and staples the signed .pkg
set -euo pipefail

[ "$(uname)" = "Darwin" ] || { echo "This script must run on macOS."; exit 1; }
for tool in cmake pkgbuild productbuild codesign /usr/libexec/PlistBuddy; do
    command -v "$tool" >/dev/null || { echo "Missing tool: $tool"; exit 1; }
done

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
HERE="$ROOT/installer"
OUT="$ROOT/dist"
WORK="$ROOT/build-installer"
VERSION="$(sed -n 's/^project(KaminariVocal VERSION \([0-9.]*\).*/\1/p' "$ROOT/CMakeLists.txt")"
[ -n "$VERSION" ] || VERSION="0.1.0"
ID_BASE="com.kaminariaudio.kaminarivocal"
AAX_FLAG=OFF
[ "${WITH_AAX:-0}" = "1" ] && AAX_FLAG=ON

rm -rf "$WORK" "$OUT"
mkdir -p "$WORK" "$OUT"

echo "==> Building Kaminari Vocal $VERSION (x86_64, Release)"
cmake -S "$ROOT" -B "$WORK/build" -G "Unix Makefiles" -DCMAKE_BUILD_TYPE=Release -DKV_COPY_AFTER_BUILD=OFF -DKV_BUILD_AAX="$AAX_FLAG"
cmake --build "$WORK/build" --config Release -j "$(sysctl -n hw.ncpu)"
ART="$WORK/build/KaminariVocal_artefacts/Release"

# name | bundle | install location | package id
FORMATS=("VST3|$ART/VST3/Kaminari Vocal.vst3|/Library/Audio/Plug-Ins/VST3|$ID_BASE.vst3"
         "AU|$ART/AU/Kaminari Vocal.component|/Library/Audio/Plug-Ins/Components|$ID_BASE.au")
[ "$AAX_FLAG" = "ON" ] && FORMATS+=("AAX|$ART/AAX/Kaminari Vocal.aaxplugin|/Library/Application Support/Avid/Audio/Plug-Ins|$ID_BASE.aax")

CHOICES=""
OUTLINE=""
for entry in "${FORMATS[@]}"; do
    IFS='|' read -r NAME BUNDLE DEST PKGID <<< "$entry"
    [ -d "$BUNDLE" ] || { echo "Built bundle not found: $BUNDLE"; exit 1; }
    STAGE="$WORK/stage-$NAME"
    mkdir -p "$STAGE"
    cp -R "$BUNDLE" "$STAGE/"
    B="$STAGE/$(basename "$BUNDLE")"

    echo "==> Signing $NAME"
    codesign --force --deep --timestamp --options runtime --sign "${CODESIGN_IDENTITY:--}" "$B" 2>/dev/null \
        || codesign --force --deep --sign "${CODESIGN_IDENTITY:--}" "$B"
    codesign --verify --deep --strict "$B"

    echo "==> Packaging $NAME"
    pkgbuild --analyze --root "$STAGE" "$WORK/$NAME.plist" >/dev/null
    /usr/libexec/PlistBuddy -c "Set :0:BundleIsRelocatable false" "$WORK/$NAME.plist"
    SCRIPTS=()
    [ "$NAME" = "AU" ] && SCRIPTS=(--scripts "$HERE/scripts-au")
    pkgbuild --root "$STAGE" --component-plist "$WORK/$NAME.plist" --identifier "$PKGID" --version "$VERSION" \
             --install-location "$DEST" ${SCRIPTS[@]+"${SCRIPTS[@]}"} "$WORK/KaminariVocal-$NAME.pkg"

    CHOICES+="    <choice id=\"$NAME\" title=\"Kaminari Vocal $NAME\" description=\"Installs Kaminari Vocal ($NAME) to $DEST\">\n        <pkg-ref id=\"$PKGID\"/>\n    </choice>\n    <pkg-ref id=\"$PKGID\" version=\"$VERSION\" onConclusion=\"none\">KaminariVocal-$NAME.pkg</pkg-ref>\n"
    OUTLINE+="        <line choice=\"$NAME\"/>\n"
done

echo "==> Building installer"
awk -v choices="$CHOICES" -v outline="$OUTLINE" '{ gsub(/@CHOICES@/, choices); gsub(/@OUTLINE@/, outline); print }' \
    "$HERE/distribution.xml.in" | sed "s/@VERSION@/$VERSION/g" > "$WORK/distribution.xml"
PKG="$OUT/KaminariVocal-$VERSION.pkg"
SIGN_ARGS=()
[ -n "${INSTALLER_SIGN_IDENTITY:-}" ] && SIGN_ARGS=(--sign "$INSTALLER_SIGN_IDENTITY")
productbuild --distribution "$WORK/distribution.xml" --package-path "$WORK" --resources "$HERE/resources" \
             ${SIGN_ARGS[@]+"${SIGN_ARGS[@]}"} "$PKG"

if [ -n "${NOTARY_PROFILE:-}" ]; then
    [ -n "${INSTALLER_SIGN_IDENTITY:-}" ] || { echo "Notarization needs INSTALLER_SIGN_IDENTITY"; exit 1; }
    echo "==> Notarizing"
    xcrun notarytool submit "$PKG" --keychain-profile "$NOTARY_PROFILE" --wait
    xcrun stapler staple "$PKG"
fi

echo
echo "Installer: $PKG"
pkgutil --check-signature "$PKG" || true
