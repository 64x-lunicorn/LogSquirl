FROM oraclelinux:10@sha256:1628d6e6586862766a2d5925f0fab6c6e1a468bc33874ae68046c7ca57406baf

COPY oracle10/epel.repo /etc/yum.repos.d/

RUN dnf update -y && \
    dnf install -y make wget python3 python3-pip rpm-build git unzip \
    openssl-devel boost-devel zlib-devel libcurl-devel \
    gcc gcc-c++ elfutils-devel \
    mesa-libGL-devel libxkbcommon-devel fontconfig-devel freetype-devel \
    dejavu-sans-mono-fonts \
    xcb-util-cursor xcb-util-keysyms xcb-util-wm dbus-devel && \
    dnf clean all

# The Table View paints Log Lines in a fixed-pitch font, so the image has to
# carry one: without it fontconfig resolves the "monospace" alias to whatever
# proportional face is present and the font tests fail far from the cause.
RUN fc-list | grep -qi "DejaVu Sans Mono" \
    || { echo "No fixed-pitch font in image; fontconfig would alias monospace to a proportional face" >&2; exit 1; }

# Qt via aqtinstall (same version as macOS/Windows CI; Renovate moves them together, #211).
# aqtinstall and its dependencies come from the hash-locked requirements file,
# wheels only, and go to a throwaway directory instead of the system
# site-packages: nothing of them stays in the image (#317). --target also
# bypasses the PEP 668 externally-managed check without overriding it.
ENV QT_VERSION=6.11.2
COPY shared/aqtinstall-requirements.txt /tmp/
RUN pip3 install --no-cache-dir --require-hashes --only-binary :all: \
        --target /tmp/aqtinstall -r /tmp/aqtinstall-requirements.txt && \
    PYTHONPATH=/tmp/aqtinstall python3 -m aqt install-qt linux desktop ${QT_VERSION} linux_gcc_64 \
        --modules qt5compat \
        --archives qtbase qtdeclarative qtimageformats qtsvg qttools qttranslations icu \
        --outputdir /opt/qt && \
    rm -rf /tmp/aqtinstall /tmp/aqtinstall-requirements.txt

ENV Qt6_DIR=/opt/qt/${QT_VERSION}/gcc_64
ENV PATH=${Qt6_DIR}/bin:/opt/bin:$PATH
ENV LD_LIBRARY_PATH=${Qt6_DIR}/lib
ENV CMAKE_PREFIX_PATH=${Qt6_DIR}

# ragel is not packaged for OL10; build from source
# Install to /usr (not /usr/local) because CI mounts workspace over /usr/local
# Ragel 7 is a development line; allowedVersions in renovate.json5 keeps 6.x.
# renovate: datasource=github-tags depName=adrian-thurston/ragel extractVersion=^ragel-(?<version>.*)$
ENV RAGEL_VERSION=6.10
ENV RAGEL_SHA256=5f156edb65d20b856d638dd9ee2dfb43285914d9aa2b6ec779dac0270cd56c3f
RUN wget -q https://www.colm.net/files/ragel/ragel-${RAGEL_VERSION}.tar.gz && \
    echo "${RAGEL_SHA256}  ragel-${RAGEL_VERSION}.tar.gz" | sha256sum -c - && \
    tar xzf ragel-${RAGEL_VERSION}.tar.gz && \
    cd ragel-${RAGEL_VERSION} && ./configure --prefix=/usr && make -j$(nproc) && make install && \
    cd .. && rm -rf ragel-${RAGEL_VERSION} ragel-${RAGEL_VERSION}.tar.gz

# CMake with integrity verification; the version lives only in the ENV line
# below, so Renovate's bump leaves no stale number behind (#211).
# renovate: datasource=github-releases depName=Kitware/CMake
ENV CMAKE_VERSION=3.31.11
ENV CMAKE_SHA256=e1ddad49d24a196d3b09c6a29f49408a418a3de096fb10fbd3626aeb22d9312b
RUN wget -q https://github.com/Kitware/CMake/releases/download/v${CMAKE_VERSION}/cmake-${CMAKE_VERSION}-linux-x86_64.sh && \
    echo "${CMAKE_SHA256}  cmake-${CMAKE_VERSION}-linux-x86_64.sh" | sha256sum -c - && \
    chmod 755 cmake-${CMAKE_VERSION}-linux-x86_64.sh && \
    ./cmake-${CMAKE_VERSION}-linux-x86_64.sh --prefix=/opt/ --exclude-subdir --skip-license && \
    rm cmake-${CMAKE_VERSION}-linux-x86_64.sh

# Ninja with integrity verification
# renovate: datasource=github-releases depName=ninja-build/ninja
ENV NINJA_VERSION=1.13.2
ENV NINJA_SHA256=5749cbc4e668273514150a80e387a957f933c6ed3f5f11e03fb30955e2bbead6
RUN wget -q https://github.com/ninja-build/ninja/releases/download/v${NINJA_VERSION}/ninja-linux.zip && \
    echo "${NINJA_SHA256}  ninja-linux.zip" | sha256sum -c - && \
    unzip ninja-linux.zip && chmod 755 ninja && mv ninja /opt/bin && rm ninja-linux.zip

ENV PATH=/opt/bin:$PATH

# sccache (compiler cache used by CI); version and checksum live in shared/install-sccache.sh.
COPY shared/install-sccache.sh /tmp/
RUN sh /tmp/install-sccache.sh && rm /tmp/install-sccache.sh
