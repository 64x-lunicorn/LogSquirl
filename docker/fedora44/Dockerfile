# The release here must match the directory name, the GHCR image name and
# CI Build's os_version/check_container, or the RPM is built on one Fedora and
# install-checked on another (#198).
FROM fedora:44@sha256:43b29f65a41eb9c35e1cd5323e3bdf3b655c2357a9f4f1ff2f9c2798e5045d80

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

# sccache (compiler cache used by CI); version and checksum live in shared/install-sccache.sh.
COPY shared/install-sccache.sh /tmp/
RUN sh /tmp/install-sccache.sh && rm /tmp/install-sccache.sh

ENV PATH=/opt/bin:$PATH
