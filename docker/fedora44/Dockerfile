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
ENV PATH=${Qt6_DIR}/bin:$PATH
ENV LD_LIBRARY_PATH=${Qt6_DIR}/lib
ENV CMAKE_PREFIX_PATH=${Qt6_DIR}

# sccache (compiler cache used by CI); version and checksum live in shared/install-sccache.sh.
COPY shared/install-sccache.sh /tmp/
RUN sh /tmp/install-sccache.sh && rm /tmp/install-sccache.sh

ENV PATH=/opt/bin:$PATH
