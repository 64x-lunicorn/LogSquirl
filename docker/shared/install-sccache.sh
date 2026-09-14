#!/bin/sh
# The one definition of the sccache every Linux build image carries (the
# compiler cache CI uses). Bumping the version or rotating the checksum is an
# edit to this file only; the Dockerfiles just run it.
#
# Installs to /opt/bin (not /usr/local) because CI mounts the workspace over /usr/local.
set -eu

SCCACHE_VERSION=0.17.0
SCCACHE_SHA256=67c4a96dd237c1f518f6b36083f270f9976d516f1e57fce891755ea782e50006
ARCHIVE=sccache-v${SCCACHE_VERSION}-x86_64-unknown-linux-musl

cd /tmp
wget -q "https://github.com/mozilla/sccache/releases/download/v${SCCACHE_VERSION}/${ARCHIVE}.tar.gz"
echo "${SCCACHE_SHA256}  ${ARCHIVE}.tar.gz" | sha256sum -c -
tar xzf "${ARCHIVE}.tar.gz"
install -D -m 755 "${ARCHIVE}/sccache" /opt/bin/sccache
rm -rf "${ARCHIVE}" "${ARCHIVE}.tar.gz"
/opt/bin/sccache --version
