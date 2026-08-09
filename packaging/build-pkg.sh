#!/usr/bin/env bash
#
# build-pkg.sh — build a macOS .pkg installer for the Architexture Rack plugin.
#
# The installer drops the plugin into the *current user's* Rack 2 plugins
# folder for the matching CPU architecture, e.g.:
#
#   ~/Library/Application Support/Rack2/plugins-mac-arm64/Architexture/
#
# Rack loads the extracted folder directly (no .vcvplugin auto-extract step
# needed). Requires `make dist` to have produced dist/<slug>/ beforehand; this
# script will run it for you if that folder is missing.
#
# Usage:
#   packaging/build-pkg.sh                     # arch inferred from the built dylib
#   ARCH=x64 packaging/build-pkg.sh            # override arch (arm64 | x64)
#   RACK_DIR=/path/to/Rack-SDK packaging/build-pkg.sh
#
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$REPO_ROOT"

# --- Read slug + version straight from plugin.json (no jq dependency) --------
plugin_field() {
  # $1 = field name; grabs the first "field": "value" pair.
  sed -n "s/.*\"$1\"[[:space:]]*:[[:space:]]*\"\([^\"]*\)\".*/\1/p" plugin.json | head -n1
}

SLUG="$(plugin_field slug)"
VERSION="$(plugin_field version)"
[ -n "$SLUG" ]    || { echo "error: could not read slug from plugin.json" >&2; exit 1; }
[ -n "$VERSION" ] || { echo "error: could not read version from plugin.json" >&2; exit 1; }

# --- Determine architecture --------------------------------------------------
# Prefer an explicit override, else read it from the compiled dylib, else uname.
if [ -z "${ARCH:-}" ]; then
  DYLIB_ARCH="$(lipo -archs plugin.dylib 2>/dev/null | awk '{print $1}')"
  case "${DYLIB_ARCH:-$(uname -m)}" in
    arm64)          ARCH=arm64 ;;
    x86_64|x64)     ARCH=x64 ;;
    *) echo "error: unrecognised arch '${DYLIB_ARCH:-$(uname -m)}'" >&2; exit 1 ;;
  esac
fi
RACK_PLUGINS_SUBDIR="plugins-mac-${ARCH}"

# --- Ensure the staged plugin folder exists (make dist output) ---------------
PAYLOAD_ROOT="dist/${SLUG}"
if [ ! -d "$PAYLOAD_ROOT" ]; then
  echo ">> dist/${SLUG} missing; running 'make dist'..."
  make dist
fi
[ -d "$PAYLOAD_ROOT" ] || { echo "error: $PAYLOAD_ROOT still missing after make dist" >&2; exit 1; }

# Note: pkgbuild encodes each file's com.apple.provenance xattr (auto-attached
# by macOS on APFS, and un-removable) as a ._AppleDouble entry in the archive.
# That's cosmetic to the cpio listing only — the Installer reconstitutes it as
# an xattr on the real file, so no ._ files appear in the installed folders.
# Genuine Finder cruft (.DS_Store) is excluded via pkgbuild --filter below.
PKG_FILTER='(^|/)\.DS_Store$'

IDENTIFIER="com.architexture.${SLUG}"
INSTALL_LOCATION="Library/Application Support/Rack2/${RACK_PLUGINS_SUBDIR}/${SLUG}"

PATCH_IDENTIFIER="com.architexture.${SLUG}.patches"
PATCH_INSTALL_LOCATION="Library/Application Support/Rack2/patches"

BUILD_DIR="dist/pkg-build"
rm -rf "$BUILD_DIR"
mkdir -p "$BUILD_DIR"

COMPONENT_PKG="${BUILD_DIR}/${SLUG}-component.pkg"
PATCH_PKG="${BUILD_DIR}/${SLUG}-patches.pkg"
DIST_XML="${BUILD_DIR}/distribution.xml"
FINAL_PKG="dist/${SLUG}-${VERSION}-mac-${ARCH}.pkg"

echo ">> Building plugin component package (${ARCH})..."
pkgbuild \
  --root "$PAYLOAD_ROOT" \
  --filter "$PKG_FILTER" \
  --identifier "$IDENTIFIER" \
  --version "$VERSION" \
  --install-location "$INSTALL_LOCATION" \
  "$COMPONENT_PKG"

# --- Optional patches component: *.vcv files in the patches/ directory -------
# The whole patches/ dir is the payload root, so it maps directly onto
# Rack2/patches/ where Rack's file browser looks.
PATCHES_SRC_DIR="patches"
PATCH_XML_OUTLINE=""
PATCH_XML_CHOICE=""
PATCH_XML_PKGREF=""
shopt -s nullglob
PATCH_FILES=( "$PATCHES_SRC_DIR"/*.vcv )
shopt -u nullglob
if [ ${#PATCH_FILES[@]} -gt 0 ]; then
  echo ">> Building patches component (${#PATCH_FILES[@]} file(s) from ${PATCHES_SRC_DIR}/)..."
  for f in "${PATCH_FILES[@]}"; do echo "     + $f"; done
  # patches/ maps directly onto Rack2/patches/; --filter drops any .DS_Store.
  pkgbuild \
    --root "$PATCHES_SRC_DIR" \
    --filter "$PKG_FILTER" \
    --identifier "$PATCH_IDENTIFIER" \
    --version "$VERSION" \
    --install-location "$PATCH_INSTALL_LOCATION" \
    "$PATCH_PKG"
  PATCH_XML_OUTLINE="            <line choice=\"${PATCH_IDENTIFIER}\"/>"
  PATCH_XML_CHOICE="    <choice id=\"${PATCH_IDENTIFIER}\" visible=\"false\"><pkg-ref id=\"${PATCH_IDENTIFIER}\"/></choice>"
  PATCH_XML_PKGREF="    <pkg-ref id=\"${PATCH_IDENTIFIER}\" version=\"${VERSION}\" onConclusion=\"none\">${SLUG}-patches.pkg</pkg-ref>"
fi

# Distribution XML: restrict install to the current user's home domain so the
# files land in ~/Library/... (no admin password, no root-owned files).
cat > "$DIST_XML" <<XML
<?xml version="1.0" encoding="utf-8"?>
<installer-gui-script minSpecVersion="2">
    <title>${SLUG} ${VERSION}</title>
    <domains enable_anywhere="false" enable_currentUserHome="true" enable_localSystem="false"/>
    <options customize="never" require-scripts="false" hostArchitectures="${ARCH/x64/x86_64}"/>
    <choices-outline>
        <line choice="default">
            <line choice="${IDENTIFIER}"/>
${PATCH_XML_OUTLINE}
        </line>
    </choices-outline>
    <choice id="default"/>
    <choice id="${IDENTIFIER}" visible="false">
        <pkg-ref id="${IDENTIFIER}"/>
    </choice>
${PATCH_XML_CHOICE}
    <pkg-ref id="${IDENTIFIER}" version="${VERSION}" onConclusion="none">${SLUG}-component.pkg</pkg-ref>
${PATCH_XML_PKGREF}
</installer-gui-script>
XML

echo ">> Building product archive..."
productbuild \
  --distribution "$DIST_XML" \
  --package-path "$BUILD_DIR" \
  "$FINAL_PKG"

echo ""
echo "Built: $FINAL_PKG"
echo "Installs plugin to: ~/${INSTALL_LOCATION}/"
if [ ${#PATCH_FILES[@]} -gt 0 ]; then
  echo "Installs patches to: ~/${PATCH_INSTALL_LOCATION}/"
fi
echo ""
echo "To distribute publicly, sign + notarize:"
echo "  productsign --sign \"Developer ID Installer: NAME (TEAMID)\" \"$FINAL_PKG\" \"${FINAL_PKG%.pkg}-signed.pkg\""
echo "  xcrun notarytool submit ... && xcrun stapler staple \"$FINAL_PKG\""
