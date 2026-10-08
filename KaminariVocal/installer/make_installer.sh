#!/usr/bin/env bash
# Builds the plug-in and packages it as dist/<Product>-<version>.pkg (macOS only). The product name and bundle ID
# come from CMakeLists.txt (KV_PRODUCT_NAME, KV_BUNDLE_ID), e.g. "Kaminari Vocal Alt".
#
# The installer offers VST3 and AU (and AAX when built with WITH_AAX=1) and installs them for all users:
#   /Library/Audio/Plug-Ins/VST3/<Product>.vst3
#   /Library/Audio/Plug-Ins/Components/<Product>.component
#   /Library/Application Support/Avid/Audio/Plug-Ins/<Product>.aaxplugin
# Running a newer installer over an older one upgrades in place; sessions and user presets are kept.
#
# Optional environment variables:
#   WITH_AAX=1               also build and package AAX (runs in Pro Tools Developer until PACE-signed)
#   CODESIGN_IDENTITY        "Developer ID Application: ..." to sign the plug-ins (default: ad-hoc)
#   INSTALLER_SIGN_IDENTITY  "Developer ID Installer: ..." to sign the .pkg
#   NOTARY_PROFILE           notarytool keychain profile; submits and staples the signed .pkg
#   ARCHS                    "x86_64", "arm64" or "x86_64;arm64" (universal); default: x86_64 (Intel; also runs on Apple Silicon under Rosetta)
set -euo pipefail

[ "$(uname)" = "Darwin" ] || { echo "This script must run on macOS."; exit 1; }
for tool in cmake pkgbuild productbuild codesign plutil; do
    command -v "$tool" >/dev/null || { echo "Missing tool: $tool"; exit 1; }
done

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
HERE="$ROOT/installer"
OUT="$ROOT/dist"
WORK="$ROOT/build-installer"
VERSION="$(sed -n 's/^project(KaminariVocal VERSION \([0-9.]*\).*/\1/p' "$ROOT/CMakeLists.txt")"
[ -n "$VERSION" ] || VERSION="0.1.0"
PRODUCT="$(sed -n 's/^set(KV_PRODUCT_NAME "\(.*\)")$/\1/p' "$ROOT/CMakeLists.txt")"
ID_BASE="$(sed -n 's/^set(KV_BUNDLE_ID *\([^ )]*\))$/\1/p' "$ROOT/CMakeLists.txt")"
[ -n "$PRODUCT" ] && [ -n "$ID_BASE" ] || { echo "KV_PRODUCT_NAME / KV_BUNDLE_ID not found in CMakeLists.txt"; exit 1; }
FILEBASE="$(echo "$PRODUCT" | tr -d ' ')"
AAX_FLAG=OFF
[ "${WITH_AAX:-0}" = "1" ] && AAX_FLAG=ON

rm -rf "$WORK" "$OUT"
mkdir -p "$WORK" "$OUT"

# ARCHS: CPU architectures to build, e.g. "x86_64;arm64" for a universal build (default: x86_64)
ARCHS="${ARCHS:-x86_64}"
echo "==> Building $PRODUCT $VERSION ($ARCHS, Release)"
cmake -S "$ROOT" -B "$WORK/build" -G "Unix Makefiles" -DCMAKE_BUILD_TYPE=Release -DKV_COPY_AFTER_BUILD=OFF -DKV_BUILD_AAX="$AAX_FLAG" \
      -DCMAKE_OSX_ARCHITECTURES="$ARCHS" -DCMAKE_OSX_DEPLOYMENT_TARGET=10.15
cmake --build "$WORK/build" --config Release -j "$(sysctl -n hw.ncpu)"
ART="$WORK/build/KaminariVocal_artefacts/Release"

# name | bundle | install location | package id
FORMATS=("VST3|$ART/VST3/$PRODUCT.vst3|/Library/Audio/Plug-Ins/VST3|$ID_BASE.vst3"
         "AU|$ART/AU/$PRODUCT.component|/Library/Audio/Plug-Ins/Components|$ID_BASE.au")
[ "$AAX_FLAG" = "ON" ] && FORMATS+=("AAX|$ART/AAX/$PRODUCT.aaxplugin|/Library/Application Support/Avid/Audio/Plug-Ins|$ID_BASE.aax")

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
    # Component list written directly: "pkgbuild --analyze" returns an empty list for .vst3 bundles.
    # Not relocatable, so the installer always writes to DEST (no "found elsewhere" redirection).
    cat > "$WORK/$NAME.plist" <<PLIST
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<array>
    <dict>
        <key>RootRelativeBundlePath</key><string>$(basename "$BUNDLE")</string>
        <key>BundleIsRelocatable</key><false/>
        <key>BundleIsVersionChecked</key><false/>
        <key>BundleHasStrictIdentifier</key><false/>
        <key>BundleOverwriteAction</key><string>upgrade</string>
    </dict>
</array>
</plist>
PLIST
    plutil -lint "$WORK/$NAME.plist" >/dev/null
    SCRIPTS=()
    [ "$NAME" = "AU" ] && SCRIPTS=(--scripts "$HERE/scripts-au")
    pkgbuild --root "$STAGE" --component-plist "$WORK/$NAME.plist" --identifier "$PKGID" --version "$VERSION" \
             --install-location "$DEST" ${SCRIPTS[@]+"${SCRIPTS[@]}"} "$WORK/$FILEBASE-$NAME.pkg"

    CHOICES+="    <choice id=\"$NAME\" title=\"$PRODUCT $NAME\" description=\"Installs $PRODUCT ($NAME) to $DEST\">\n        <pkg-ref id=\"$PKGID\"/>\n    </choice>\n    <pkg-ref id=\"$PKGID\" version=\"$VERSION\" onConclusion=\"none\">$FILEBASE-$NAME.pkg</pkg-ref>\n"
    OUTLINE+="        <line choice=\"$NAME\"/>\n"
done

echo "==> Building installer"
awk -v choices="$CHOICES" -v outline="$OUTLINE" '{ gsub(/@CHOICES@/, choices); gsub(/@OUTLINE@/, outline); print }' \
    "$HERE/distribution.xml.in" | sed -e "s/@VERSION@/$VERSION/g" -e "s/@PRODUCT@/$PRODUCT/g" > "$WORK/distribution.xml"
PKG="$OUT/$FILEBASE-$VERSION.pkg"
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
