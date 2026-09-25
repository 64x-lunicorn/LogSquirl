#!/bin/bash
set -euo pipefail

# linuxdeploy and its Qt plugin run with the release build's AppDir in hand, so
# they are fetched at a fixed release tag and checked against a known SHA-256
# before they execute. The `continuous` tag is rewritten on every upstream
# push, which made the bundled tooling (and so the AppImage) change underneath
# us without review (#200). Renovate bumps each tag and
# .github/workflows/renovate-checksums.yml its hash (#211).
# renovate: datasource=github-releases depName=linuxdeploy/linuxdeploy
LINUXDEPLOY_TAG="1-alpha-20251107-1"
LINUXDEPLOY_SHA256="c20cd71e3a4e3b80c3483cef793cda3f4e990aca14014d23c544ca3ce1270b4d"
# renovate: datasource=github-releases depName=linuxdeploy/linuxdeploy-plugin-qt
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

# Build the AppImage.
# Of the platform plugins, linuxdeploy-plugin-qt deploys libqxcb.so and only
# the ones EXTRA_PLATFORM_PLUGINS names (PlatformPluginsDeployer.cpp), so the
# VNC platform plugin, with the authentication bypass of CVE-2026-79680, stays
# out (#514). The EXTRA_QT_PLUGINS="iconengines;imageformats;platforms" set
# here before was the plugin's deprecated name for EXTRA_QT_MODULES, and none of
# the three is a Qt module, so it deployed nothing; the plugin deploys the
# image formats itself and the SVG icon engine with Qt SVG.
export VERSION=$LOGSQUIRL_VERSION
unset EXTRA_PLATFORM_PLUGINS EXTRA_QT_MODULES EXTRA_QT_PLUGINS
./linuxdeploy-x86_64.AppImage --appdir appdir \
    --desktop-file appdir/usr/share/applications/*.desktop \
    --plugin qt \
    --output appimage

# The AppImage is the AppDir as it is now. LogSquirl needs the xcb platform on
# Linux (and would need wayland); any other platform plugin, above all VNC,
# fails the build instead of shipping (#514).
echo "Platform plugins in the AppImage:"
ls -1 appdir/usr/plugins/platforms
unexpected=$(find appdir -name 'libqvnc*' -o -path 'appdir/usr/plugins/platforms/*' \
    ! -name 'libqxcb.so' ! -name 'libqwayland*.so' | sort -u)
if [ -n "$unexpected" ]; then
    echo "::error::the AppImage would ship platform plugins LogSquirl does not use (#514): $(echo "$unexpected" | tr '\n' ' ')"
    exit 1
fi

mkdir ./packages
cp "./LogSquirl-${LOGSQUIRL_VERSION}-x86_64.AppImage" "./packages/logsquirl-${LOGSQUIRL_VERSION}-x86_64.AppImage"

# The Debian package and version of every system library linuxdeploy bundled,
# read from this image's dpkg database with the linker search path linuxdeploy
# used (LD_LIBRARY_PATH first). CI Release adds them to the release SBOM and
# keeps the file out of the release assets (#227).
repo_root=$(readlink -f "$(dirname "$0")/../../..")
python3 "$repo_root/scripts/sbom/logsquirl_sbom.py" appimage-debs --appdir appdir \
    --output ./packages/logsquirl_appimage_debs.json
