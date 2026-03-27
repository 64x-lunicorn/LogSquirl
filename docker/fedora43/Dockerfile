FROM fedora:43

RUN dnf update -y && \
    dnf install -y ragel make wget python3 python3-pip rpm-build git unzip \
    openssl-devel boost-devel zlib-devel libcurl-devel \
    gcc gcc-c++ elfutils-devel ninja-build cmake \
    mesa-libGL-devel libxkbcommon-devel fontconfig-devel freetype-devel \
    xcb-util-cursor xcb-util-keysyms xcb-util-wm dbus-devel && \
    dnf clean all

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
