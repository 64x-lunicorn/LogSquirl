#!/bin/bash

DESTDIR=$(readlink -f appdir) ninja install
wget -c -q "https://github.com/probonopd/linuxdeployqt/releases/download/continuous/linuxdeployqt-continuous-x86_64.AppImage"
chmod a+x linuxdeployqt-continuous-x86_64.AppImage

VERSION=$LOGSQUIRL_VERSION ./linuxdeployqt-continuous-x86_64.AppImage appdir/usr/share/applications/*.desktop -bundle-non-qt-libs

mkdir -p appdir/usr/lib
cp /lib/x86_64-linux-gnu/libssl* appdir/usr/lib

VERSION=$LOGSQUIRL_VERSION ./linuxdeployqt-continuous-x86_64.AppImage appdir/usr/share/applications/*.desktop -appimage

mkdir ./packages
cp ./LogSquirl-$LOGSQUIRL_VERSION-x86_64.AppImage ./packages/logsquirl-$LOGSQUIRL_VERSION-x86_64.AppImage
