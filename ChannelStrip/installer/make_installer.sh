#!/usr/bin/env bash
# Builds Channel Strip and packages it as ChannelStrip-<version>.pkg (macOS only).
#
# Optional environment variables:
#   VERSION                  package version (default 0.1.0)
#   CODESIGN_IDENTITY        "Developer ID Application: ..." to sign the plug-in (default: ad-hoc)
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
VERSION="${VERSION:-0.1.0}"
IDENTIFIER="com.local.channelstrip.vst3"
PLUGIN_NAME="Channel Strip.vst3"

rm -rf "$WORK" "$OUT"
mkdir -p "$WORK/stage" "$OUT"

echo "==> Building (x86_64, Release)"
cmake -S "$ROOT" -B "$WORK/build" -G "Unix Makefiles" -DCMAKE_BUILD_TYPE=Release
cmake --build "$WORK/build" --config Release -j "$(sysctl -n hw.ncpu)"

BUILT="$(find "$WORK/build" -type d -name "$PLUGIN_NAME" | head -n 1)"
[ -n "$BUILT" ] || { echo "Built plug-in not found"; exit 1; }
cp -R "$BUILT" "$WORK/stage/"

echo "==> Signing plug-in"
codesign --force --deep --timestamp --sign "${CODESIGN_IDENTITY:--}" "$WORK/stage/$PLUGIN_NAME"
codesign --verify --deep --strict "$WORK/stage/$PLUGIN_NAME"

echo "==> Building component package"
# Make the bundle non-relocatable so it always lands in /Library/Audio/Plug-Ins/VST3.
pkgbuild --analyze --root "$WORK/stage" "$WORK/components.plist" >/dev/null
/usr/libexec/PlistBuddy -c "Set :0:BundleIsRelocatable false" "$WORK/components.plist"
pkgbuild --root "$WORK/stage" \
         --component-plist "$WORK/components.plist" \
         --identifier "$IDENTIFIER" \
         --version "$VERSION" \
         --install-location "/Library/Audio/Plug-Ins/VST3" \
         "$WORK/ChannelStrip-component.pkg"

echo "==> Building installer"
sed -e "s/@IDENTIFIER@/$IDENTIFIER/g" -e "s/@VERSION@/$VERSION/g" "$HERE/distribution.xml.in" > "$WORK/distribution.xml"
PKG="$OUT/ChannelStrip-$VERSION.pkg"
SIGN_ARGS=()
[ -n "${INSTALLER_SIGN_IDENTITY:-}" ] && SIGN_ARGS=(--sign "$INSTALLER_SIGN_IDENTITY")
productbuild --distribution "$WORK/distribution.xml" \
             --package-path "$WORK" \
             --resources "$HERE/resources" \
             ${SIGN_ARGS[@]+"${SIGN_ARGS[@]}"} \
             "$PKG"

if [ -n "${NOTARY_PROFILE:-}" ]; then
    [ -n "${INSTALLER_SIGN_IDENTITY:-}" ] || { echo "Notarization needs INSTALLER_SIGN_IDENTITY"; exit 1; }
    echo "==> Notarizing"
    xcrun notarytool submit "$PKG" --keychain-profile "$NOTARY_PROFILE" --wait
    xcrun stapler staple "$PKG"
fi

echo
echo "Installer: $PKG"
pkgutil --check-signature "$PKG" || true
