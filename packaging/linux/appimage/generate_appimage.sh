#!/bin/bash
set -euo pipefail

# linuxdeploy and its Qt plugin run with the release build's AppDir in hand, so
# they are fetched at a fixed release tag and checked against a known SHA-256
# before they execute. The `continuous` tag is rewritten on every upstream
# push, which made the bundled tooling (and so the AppImage) change underneath
# us without review (#200). To bump: pick a new release tag, download the asset,
# `sha256sum` it and update the pair below.
LINUXDEPLOY_TAG="1-alpha-20251107-1"
LINUXDEPLOY_SHA256="c20cd71e3a4e3b80c3483cef793cda3f4e990aca14014d23c544ca3ce1270b4d"
LINUXDEPLOY_PLUGIN_QT_TAG="1-alpha-20250213-1"
LINUXDEPLOY_PLUGIN_QT_SHA256="15106be885c1c48a021198e7e1e9a48ce9d02a86dd0a1848f00bdbf3c1c92724"

# Downloads $1 to file $2 and refuses to keep it unless its SHA-256 is $3, so a
# replaced release asset stops the build instead of running (#200).
fetch_verified() {
    local url=$1 file=$2 sha256=$3
    rm -f "$file"
    wget -q -O "$file" "$url" || return 1
    if ! echo "${sha256}  ${file}" | sha256sum -c -; then
        rm -f "$file"
        echo "error: SHA-256 mismatch for ${url}" >&2
        return 1
    fi
    chmod a+x "$file"
}

# Install into AppDir
DESTDIR=$(readlink -f appdir) ninja install

# Download linuxdeploy and its Qt plugin
fetch_verified \
    "https://github.com/linuxdeploy/linuxdeploy/releases/download/${LINUXDEPLOY_TAG}/linuxdeploy-x86_64.AppImage" \
    linuxdeploy-x86_64.AppImage "$LINUXDEPLOY_SHA256"
fetch_verified \
    "https://github.com/linuxdeploy/linuxdeploy-plugin-qt/releases/download/${LINUXDEPLOY_PLUGIN_QT_TAG}/linuxdeploy-plugin-qt-x86_64.AppImage" \
    linuxdeploy-plugin-qt-x86_64.AppImage "$LINUXDEPLOY_PLUGIN_QT_SHA256"

# Bundle libssl into AppDir
mkdir -p appdir/usr/lib
cp /lib/x86_64-linux-gnu/libssl* appdir/usr/lib

# Build the AppImage
export VERSION=$LOGSQUIRL_VERSION
export EXTRA_QT_PLUGINS="iconengines;imageformats;platforms"
./linuxdeploy-x86_64.AppImage --appdir appdir \
    --desktop-file appdir/usr/share/applications/*.desktop \
    --plugin qt \
    --output appimage

mkdir ./packages
cp "./LogSquirl-${LOGSQUIRL_VERSION}-x86_64.AppImage" "./packages/logsquirl-${LOGSQUIRL_VERSION}-x86_64.AppImage"
