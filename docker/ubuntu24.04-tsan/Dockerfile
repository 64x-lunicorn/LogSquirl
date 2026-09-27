FROM ubuntu:noble@sha256:008173c23f95b170204355c12626cb5a965d779a7e1283b09e9cffbb1bf33ca3

# The image of the Sanitizers / tsan job (#510): docker/ubuntu24.04 with a Qt
# built from source with ThreadSanitizer instead of the prebuilt one, so TSan
# sees Qt's own locks and atomics and reports a race inside Qt like any other
# (docs/adr/0007-tsan-suppresses-onetbb-and-uninstrumented-qt-internals.md).
# Nothing is packaged from it. Its own directory gives it its own inputs hash:
# a change to docker/ubuntu24.04 does not rebuild this Qt, which takes about an
# hour.
#
# Qt's tools (moc, rcc, uic, lupdate, lrelease) are built with TSan as well
# and run while Qt and LogSquirl are built. GCC's libtsan needs
# vm.mmap_rnd_bits=28 on the host kernel for that, as the tests do: every
# workflow building or using this image sets it first.

ENV DEBIAN_FRONTEND=noninteractive

# The packages of docker/ubuntu24.04, less the packaging tools and the xcb
# libraries: the tests run on the offscreen platform, and this Qt has no xcb
# plugin.
RUN apt-get update -y && \
    apt-get install --no-install-recommends build-essential python3 python3-pip python-is-python3 \
                    libboost-dev libicu-dev libssl-dev libcurl4-openssl-dev \
                    ragel ninja-build zlib1g-dev git pkg-config \
                    wget ca-certificates file \
                    libgl-dev libegl-dev libdbus-1-dev \
                    libfontconfig1-dev libfreetype-dev \
                    libxkbcommon-dev -y && \
    apt-get clean && rm -rf /var/lib/apt/lists/*

# CMake with integrity verification; the version lives only in the ENV line
# below, so Renovate's bump leaves no stale number behind (#211). Qt's build
# needs it, so it comes first here.
# renovate: datasource=github-releases depName=Kitware/CMake
ENV CMAKE_VERSION=3.31.11
ENV CMAKE_SHA256=e1ddad49d24a196d3b09c6a29f49408a418a3de096fb10fbd3626aeb22d9312b
RUN wget -q https://github.com/Kitware/CMake/releases/download/v${CMAKE_VERSION}/cmake-${CMAKE_VERSION}-linux-x86_64.sh && \
    echo "${CMAKE_SHA256}  cmake-${CMAKE_VERSION}-linux-x86_64.sh" | sha256sum -c - && \
    chmod 755 cmake-${CMAKE_VERSION}-linux-x86_64.sh && \
    ./cmake-${CMAKE_VERSION}-linux-x86_64.sh --prefix=/opt/ --exclude-subdir --skip-license && \
    rm cmake-${CMAKE_VERSION}-linux-x86_64.sh
ENV PATH=/opt/bin:$PATH

# Qt from source, the same version as every other image (Renovate moves them
# together, and the SBOM job fails when they disagree). aqtinstall fetches the
# source archives from Qt's online repository and checks them against its
# hashes, as it does for the prebuilt Qt of the other images (#317). Only the
# modules LogSquirl's build and tests use: qtbase (Core, Gui, Widgets,
# Concurrent, Network, Xml, Test), qtsvg (the SVG icons) and qttools
# (LinguistTools: lupdate, lrelease, lconvert; Qt Linguist itself needs Qt
# Quick and is left out, and so are the other tools).
#
# -sanitize thread instruments every library and tool. -no-glib keeps GLib's
# event loop out of QtCore; GLib is not built with TSan, and its wake-up
# eventfd was reported as a race between GLib and itself (#482). Debug info
# gives TSan's reports Qt's function names and lines.
ENV QT_VERSION=6.11.3
COPY shared/aqtinstall-requirements.txt /tmp/
RUN pip3 install --no-cache-dir --require-hashes --only-binary :all: \
        --target /tmp/aqtinstall -r /tmp/aqtinstall-requirements.txt && \
    PYTHONPATH=/tmp/aqtinstall python3 -m aqt install-src linux desktop ${QT_VERSION} \
        --archives qtbase qtsvg qttools \
        --outputdir /tmp/qt-src && \
    rm -rf /tmp/aqtinstall /tmp/aqtinstall-requirements.txt && \
    src=/tmp/qt-src/${QT_VERSION}/Src && prefix=/opt/qt/${QT_VERSION}/gcc_64 && \
    mkdir /tmp/qt-build && cd /tmp/qt-build && \
    ${src}/qtbase/configure -prefix ${prefix} -release -force-debug-info \
        -sanitize thread -no-glib -no-xcb \
        -nomake examples -nomake tests && \
    cmake --build . --parallel && cmake --install . && \
    for module in qtsvg qttools; do \
        rm -rf /tmp/qt-build && mkdir /tmp/qt-build && cd /tmp/qt-build && \
        ${prefix}/bin/qt-configure-module ${src}/${module} -- \
            -DFEATURE_assistant=OFF -DFEATURE_designer=OFF -DFEATURE_pixeltool=OFF \
            -DFEATURE_qtdiag=OFF -DFEATURE_distancefieldgenerator=OFF && \
        cmake --build . --parallel && cmake --install . || exit 1; \
    done && \
    cd / && rm -rf /tmp/qt-build /tmp/qt-src

ENV Qt6_DIR=/opt/qt/${QT_VERSION}/gcc_64
ENV PATH=${Qt6_DIR}/bin:$PATH
ENV LD_LIBRARY_PATH=${Qt6_DIR}/lib
ENV CMAKE_PREFIX_PATH=${Qt6_DIR}

# sccache (compiler cache used by CI); version and checksum live in shared/install-sccache.sh.
COPY shared/install-sccache.sh /tmp/
RUN sh /tmp/install-sccache.sh && rm /tmp/install-sccache.sh
