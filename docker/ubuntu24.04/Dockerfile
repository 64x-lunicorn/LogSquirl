FROM ubuntu:noble@sha256:69cecf4bbf72d2d44a9eef1b71fb98c7fb973d78af11399deccef19beb008ad9

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

# sccache (compiler cache used by CI); version and checksum live in shared/install-sccache.sh.
COPY shared/install-sccache.sh /tmp/
RUN sh /tmp/install-sccache.sh && rm /tmp/install-sccache.sh
