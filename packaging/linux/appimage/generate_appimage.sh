#!/bin/bash
set -euo pipefail

# Install into AppDir
DESTDIR=$(readlink -f appdir) ninja install

# Download linuxdeploy and its Qt plugin
wget -c -q "https://github.com/linuxdeploy/linuxdeploy/releases/download/continuous/linuxdeploy-x86_64.AppImage"
wget -c -q "https://github.com/linuxdeploy/linuxdeploy-plugin-qt/releases/download/continuous/linuxdeploy-plugin-qt-x86_64.AppImage"
chmod a+x linuxdeploy-x86_64.AppImage linuxdeploy-plugin-qt-x86_64.AppImage

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
cp ./LogSquirl-$LOGSQUIRL_VERSION-x86_64.AppImage ./packages/logsquirl-$LOGSQUIRL_VERSION-x86_64.AppImage
