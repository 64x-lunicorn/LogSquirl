FROM fedora:44

RUN dnf update -y && \
    dnf install -y ragel make wget tar python3 python3-pip rpm-build git unzip \
    openssl-devel boost-devel zlib-devel libcurl-devel \
    gcc gcc-c++ elfutils-devel ninja-build cmake \
    mesa-libGL-devel libxkbcommon-devel fontconfig-devel freetype-devel \
    dejavu-sans-mono-fonts \
    xcb-util-cursor xcb-util-keysyms xcb-util-wm dbus-devel && \
    dnf clean all

# The Table View paints Log Lines in a fixed-pitch font, so the image has to
# carry one: without it fontconfig resolves the "monospace" alias to whatever
# proportional face is present and the font tests fail far from the cause.
RUN fc-list | grep -qi "DejaVu Sans Mono" \
    || { echo "No fixed-pitch font in image; fontconfig would alias monospace to a proportional face" >&2; exit 1; }

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
ENV PATH=${Qt6_DIR}/bin:$PATH
ENV LD_LIBRARY_PATH=${Qt6_DIR}/lib
ENV CMAKE_PREFIX_PATH=${Qt6_DIR}

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

ENV PATH=/opt/bin:$PATH
