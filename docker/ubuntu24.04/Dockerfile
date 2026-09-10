FROM ubuntu:noble

ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update -y && \
    apt-get install --no-install-recommends build-essential python3 python3-pip python-is-python3 \
                    libboost-dev libicu-dev libssl-dev libcurl4-openssl-dev \
                    ragel ninja-build zlib1g-dev git pkg-config \
                    wget ca-certificates fuse libfuse2 file \
                    libgl-dev libegl-dev libdbus-1-dev \
                    libfontconfig1-dev libfreetype-dev libglib2.0-0 \
                    libxkbcommon-dev libxkbcommon-x11-dev \
                    libxcb-cursor0 libxcb-icccm4 libxcb-keysyms1 libxcb-shape0 \
                    libxcb-xkb-dev libxcb-render-util0-dev libxcb-randr0-dev \
                    libxcb-xinerama0-dev libxcb-image0-dev libxcb-util-dev -y && \
    apt-get clean && rm -rf /var/lib/apt/lists/*

# Qt 6.10.3 via aqtinstall (same version as macOS/Windows CI)
ENV QT_VERSION=6.10.3
RUN pip3 install --break-system-packages aqtinstall && \
    aqt install-qt linux desktop ${QT_VERSION} linux_gcc_64 \
        --modules qt5compat \
        --archives qtbase qtimageformats qtsvg qttools qttranslations icu \
        --outputdir /opt/qt && \
    pip3 uninstall --break-system-packages -y aqtinstall && \
    rm -rf /root/.cache/pip

ENV Qt6_DIR=/opt/qt/${QT_VERSION}/gcc_64
ENV PATH=${Qt6_DIR}/bin:/opt/bin:$PATH
ENV LD_LIBRARY_PATH=${Qt6_DIR}/lib
ENV CMAKE_PREFIX_PATH=${Qt6_DIR}

# CMake 3.31.11 with integrity verification
ENV CMAKE_VERSION=3.31.11
ENV CMAKE_SHA256=e1ddad49d24a196d3b09c6a29f49408a418a3de096fb10fbd3626aeb22d9312b
RUN wget -q https://github.com/Kitware/CMake/releases/download/v${CMAKE_VERSION}/cmake-${CMAKE_VERSION}-linux-x86_64.sh && \
    echo "${CMAKE_SHA256}  cmake-${CMAKE_VERSION}-linux-x86_64.sh" | sha256sum -c - && \
    chmod 755 cmake-${CMAKE_VERSION}-linux-x86_64.sh && \
    ./cmake-${CMAKE_VERSION}-linux-x86_64.sh --prefix=/opt/ --exclude-subdir --skip-license && \
    rm cmake-${CMAKE_VERSION}-linux-x86_64.sh

# sccache 0.17.0 with integrity verification (compiler cache used by CI).
# Install to /opt/bin (not /usr/local) because CI mounts the workspace over /usr/local.
ENV SCCACHE_VERSION=0.17.0
ENV SCCACHE_SHA256=67c4a96dd237c1f518f6b36083f270f9976d516f1e57fce891755ea782e50006
RUN wget -q https://github.com/mozilla/sccache/releases/download/v${SCCACHE_VERSION}/sccache-v${SCCACHE_VERSION}-x86_64-unknown-linux-musl.tar.gz && \
    echo "${SCCACHE_SHA256}  sccache-v${SCCACHE_VERSION}-x86_64-unknown-linux-musl.tar.gz" | sha256sum -c - && \
    tar xzf sccache-v${SCCACHE_VERSION}-x86_64-unknown-linux-musl.tar.gz && \
    install -D -m 755 sccache-v${SCCACHE_VERSION}-x86_64-unknown-linux-musl/sccache /opt/bin/sccache && \
    rm -rf sccache-v${SCCACHE_VERSION}-x86_64-unknown-linux-musl sccache-v${SCCACHE_VERSION}-x86_64-unknown-linux-musl.tar.gz && \
    /opt/bin/sccache --version
