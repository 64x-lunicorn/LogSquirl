# Dedicated AppImage builder base.
#
# The AppImage produced here bundles Qt/libssl but deliberately does NOT bundle
# glibc or libstdc++ (linuxdeploy excludes them by design — they are provided by
# the host).  Building on Ubuntu 22.04 (jammy) therefore pins the compatibility
# floor to jammy's glibc 2.35 and its GCC 12 libstdc++ (GLIBCXX_3.4.30), so the
# resulting AppImage runs on Ubuntu 22.04 and every newer distribution.
#
# jammy's default toolchain is GCC 11, which lacks <expected>; the project uses
# std::expected (C++23), so GCC 12 (also shipped by jammy) is installed and made
# the default compiler.  GCC 12 keeps the libstdc++ requirement at jammy's own
# runtime level, unlike a PPA-sourced newer GCC which would raise the floor.
FROM ubuntu:jammy

ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update -y && \
    apt-get install --no-install-recommends build-essential gcc-12 g++-12 \
                    python3 python3-pip python-is-python3 \
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

# Make GCC 12 the default compiler (jammy defaults to GCC 11, which has no <expected>).
RUN update-alternatives --install /usr/bin/gcc gcc /usr/bin/gcc-12 100 && \
    update-alternatives --install /usr/bin/g++ g++ /usr/bin/g++-12 100 && \
    update-alternatives --install /usr/bin/cc  cc  /usr/bin/gcc-12 100 && \
    update-alternatives --install /usr/bin/c++ c++ /usr/bin/g++-12 100
ENV CC=gcc-12
ENV CXX=g++-12

# Qt 6.10.3 via aqtinstall (same version as macOS/Windows CI).
# jammy ships an old pip (22.x) that predates --break-system-packages; upgrade it
# first, then install aqt without that flag (jammy is not PEP 668 externally-managed).
ENV QT_VERSION=6.10.3
RUN pip3 install --upgrade pip && \
    pip3 install aqtinstall && \
    aqt install-qt linux desktop ${QT_VERSION} linux_gcc_64 \
        --modules qt5compat \
        --archives qtbase qtimageformats qtsvg qttools qttranslations icu \
        --outputdir /opt/qt && \
    pip3 uninstall -y aqtinstall && \
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
