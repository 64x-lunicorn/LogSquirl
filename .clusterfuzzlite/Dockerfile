# Build image of the ClusterFuzzLite fuzzers (#477). The base is pinned by
# digest like the CI images in docker/, and is the Ubuntu 24.04 flavour of the
# OSS-Fuzz builder so its Clang, libstdc++ and glibc match the Qt below.
#
# Qt is copied out of the Ubuntu 24.04 build image instead of downloaded: the
# download failed both fuzz jobs while download.qt.io was mid-update (#850).
# The Fuzzing workflow verifies the build image of this checkout's docker/
# files by signature, pulls it by digest and tags it `latest` on the runner's
# daemon before the build, so the build uses that image (`docker build`
# without --pull prefers a local tag); a local `docker build` takes GHCR's
# latest.
FROM ghcr.io/64x-lunicorn/logsquirl-ubuntu-noble:latest AS build-image

FROM gcr.io/oss-fuzz-base/base-builder:ubuntu-24-04@sha256:76e5325862a2b138737e3348537550f05ee6518470d5190bcb7501cf210d6fc0

ENV DEBIAN_FRONTEND=noninteractive

# The same packages as docker/ubuntu24.04, less the packaging tools and pip.
RUN apt-get update -y && \
    apt-get install --no-install-recommends -y \
                    python3 \
                    libboost-dev libicu-dev libssl-dev libcurl4-openssl-dev \
                    ragel ninja-build zlib1g-dev git pkg-config zip \
                    wget ca-certificates file \
                    libgl-dev libegl-dev libdbus-1-dev \
                    libfontconfig1-dev libfreetype-dev libglib2.0-0 \
                    libxkbcommon-dev libxkbcommon-x11-dev \
                    libxcb-cursor0 libxcb-icccm4 libxcb-keysyms1 libxcb-shape0 \
                    libxcb-xkb-dev libxcb-render-util0-dev libxcb-randr0-dev \
                    libxcb-xinerama0-dev libxcb-image0-dev libxcb-util-dev && \
    apt-get clean && rm -rf /var/lib/apt/lists/*

# The build image's Qt, whatever its version (docker/ubuntu24.04 installs it
# with aqtinstall under /opt/qt/<version>/gcc_64): the fuzzers do not care
# which, and a fixed version here would go stale when Renovate moves it there.
COPY --from=build-image /opt/qt /opt/qt
RUN set -eu; set -- /opt/qt/*/gcc_64; \
    if [ "$#" -ne 1 ] || [ ! -d "$1" ]; then \
        echo "expected one Qt under /opt/qt, found: $*" >&2; exit 1; \
    fi; \
    ln -s "$1" /opt/qt/gcc_64

ENV Qt6_DIR=/opt/qt/gcc_64
ENV LD_LIBRARY_PATH=${Qt6_DIR}/lib
ENV CMAKE_PREFIX_PATH=${Qt6_DIR}

# CMake with integrity verification, same version and checksum as the CI images.
ENV CMAKE_VERSION=3.31.11
ENV CMAKE_SHA256=e1ddad49d24a196d3b09c6a29f49408a418a3de096fb10fbd3626aeb22d9312b
RUN wget -q https://github.com/Kitware/CMake/releases/download/v${CMAKE_VERSION}/cmake-${CMAKE_VERSION}-linux-x86_64.sh && \
    echo "${CMAKE_SHA256}  cmake-${CMAKE_VERSION}-linux-x86_64.sh" | sha256sum -c - && \
    chmod 755 cmake-${CMAKE_VERSION}-linux-x86_64.sh && \
    ./cmake-${CMAKE_VERSION}-linux-x86_64.sh --prefix=/opt/ --exclude-subdir --skip-license && \
    rm cmake-${CMAKE_VERSION}-linux-x86_64.sh
ENV PATH=/opt/bin:$PATH

COPY . $SRC/logsquirl
COPY .clusterfuzzlite/build.sh $SRC/build.sh
WORKDIR $SRC/logsquirl
