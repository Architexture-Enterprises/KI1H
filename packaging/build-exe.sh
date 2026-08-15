#!/usr/bin/env bash
#
# build-exe.sh — build a Windows .exe installer for the Architexture Rack plugin.
#
# The Windows counterpart to build-pkg.sh. It drops the same two payloads into
# the current user's Rack 2 folders (no admin), via an NSIS installer:
#
#   plugin  -> %LOCALAPPDATA%\Rack2\plugins-win-x64\Architexture\
#   patches -> %LOCALAPPDATA%\Rack2\patches\
#
# The Windows plugin binary (plugin.dll) is NOT built by this repo's Makefile —
# it is cross-compiled by the VCV rack-plugin-toolchain Docker image, which
# emits a win-x64 .vcvplugin. This script takes that archive (or an already
# extracted Architexture/ folder) as the plugin payload.
#
# Usage:
#   packaging/build-exe.sh path/to/Architexture-<ver>-win-x64.vcvplugin
#   packaging/build-exe.sh path/to/Architexture/            # extracted folder
#   PLUGIN_SRC=... packaging/build-exe.sh                   # same, via env
#
# Requires: makensis (brew install makensis), and tar with zstd support for
# unpacking a .vcvplugin.
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$REPO_ROOT"

command -v makensis >/dev/null 2>&1 || {
  echo "error: makensis not found. Install it with 'brew install makensis'." >&2
  exit 1
}

plugin_field() {
  sed -n "s/.*\"$1\"[[:space:]]*:[[:space:]]*\"\([^\"]*\)\".*/\1/p" plugin.json | head -n1
}
SLUG="$(plugin_field slug)"
VERSION="$(plugin_field version)"
[ -n "$SLUG" ]    || { echo "error: could not read slug from plugin.json" >&2; exit 1; }
[ -n "$VERSION" ] || { echo "error: could not read version from plugin.json" >&2; exit 1; }

PLUGIN_SRC="${1:-${PLUGIN_SRC:-}}"
[ -n "$PLUGIN_SRC" ] || {
  echo "error: no plugin payload given." >&2
  echo "  pass the win-x64 .vcvplugin (or an extracted ${SLUG}/ folder) as arg 1," >&2
  echo "  e.g. packaging/build-exe.sh ../rack-plugin-toolchain/plugin-build/${SLUG}-${VERSION}-win-x64.vcvplugin" >&2
  exit 1
}
[ -e "$PLUGIN_SRC" ] || { echo "error: plugin payload '$PLUGIN_SRC' not found" >&2; exit 1; }

BUILD_DIR="dist/exe-build"
rm -rf "$BUILD_DIR"
mkdir -p "$BUILD_DIR"

# --- Stage the plugin payload into $BUILD_DIR/<slug>/ ------------------------
PLUGIN_PAYLOAD="${BUILD_DIR}/${SLUG}"
if [ -d "$PLUGIN_SRC" ]; then
  # Extracted folder: accept either .../Architexture or its parent.
  if [ -f "${PLUGIN_SRC}/plugin.json" ]; then
    cp -R "$PLUGIN_SRC" "$PLUGIN_PAYLOAD"
  elif [ -f "${PLUGIN_SRC}/${SLUG}/plugin.json" ]; then
    cp -R "${PLUGIN_SRC}/${SLUG}" "$PLUGIN_PAYLOAD"
  else
    echo "error: '$PLUGIN_SRC' has no plugin.json (not a staged plugin folder)" >&2
    exit 1
  fi
else
  # A .vcvplugin archive (zstd-compressed tar): extract, expect a <slug>/ root.
  echo ">> Extracting $PLUGIN_SRC ..."
  tar --zstd -xf "$PLUGIN_SRC" -C "$BUILD_DIR"
  [ -f "${PLUGIN_PAYLOAD}/plugin.json" ] || {
    echo "error: archive did not yield ${SLUG}/plugin.json" >&2; exit 1; }
fi

# Sanity: the payload must be a Windows build (plugin.dll present).
[ -f "${PLUGIN_PAYLOAD}/plugin.dll" ] || {
  echo "error: ${PLUGIN_PAYLOAD}/plugin.dll missing — payload is not a win-x64 build" >&2
  exit 1
}
# Drop Finder cruft that the .vcvplugin may carry (matches the pkg's filter).
find "$PLUGIN_PAYLOAD" -name '.DS_Store' -delete

# makensis resolves source File paths against its own cwd; pass absolutes so the
# glob works regardless of where it runs.
NSIS_DEFINES=( "-DSLUG=${SLUG}" "-DVERSION=${VERSION}"
               "-DPLUGIN_PAYLOAD=${REPO_ROOT}/${PLUGIN_PAYLOAD}" )

# --- Optional patches component: patches/*.vcv, mirroring build-pkg.sh -------
PATCH_PAYLOAD="${BUILD_DIR}/patches"
shopt -s nullglob
PATCH_FILES=( patches/*.vcv )
shopt -u nullglob
if [ ${#PATCH_FILES[@]} -gt 0 ]; then
  echo ">> Staging ${#PATCH_FILES[@]} patch file(s) from patches/ ..."
  mkdir -p "$PATCH_PAYLOAD"
  for f in "${PATCH_FILES[@]}"; do echo "     + $f"; cp "$f" "$PATCH_PAYLOAD/"; done
  find "$PATCH_PAYLOAD" -name '.DS_Store' -delete
  # Uninstall manifest: delete only the .vcv files we shipped (leave user patches).
  PATCH_MANIFEST="${BUILD_DIR}/uninstall-patches.nsh"
  : > "$PATCH_MANIFEST"
  for f in "${PATCH_FILES[@]}"; do
    printf 'Delete "$LOCALAPPDATA\\Rack2\\patches\\%s"\n' "$(basename "$f")" >> "$PATCH_MANIFEST"
  done
  NSIS_DEFINES+=( "-DPATCH_PAYLOAD=${REPO_ROOT}/${PATCH_PAYLOAD}" "-DPATCH_MANIFEST=${REPO_ROOT}/${PATCH_MANIFEST}" )
fi

OUTFILE="dist/${SLUG}-${VERSION}-win-x64-setup.exe"
# makensis resolves a relative OutFile against the script dir; pass an absolute.
NSIS_DEFINES+=( "-DOUTFILE=${REPO_ROOT}/${OUTFILE}" )

echo ">> Compiling installer with makensis ..."
makensis "${NSIS_DEFINES[@]}" packaging/installer.nsi

echo ""
echo "Built: $OUTFILE"
echo "Installs plugin to:  %LOCALAPPDATA%\\Rack2\\plugins-win-x64\\${SLUG}\\"
if [ ${#PATCH_FILES[@]} -gt 0 ]; then
  echo "Installs patches to: %LOCALAPPDATA%\\Rack2\\patches\\"
fi
