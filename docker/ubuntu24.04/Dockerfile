FROM ubuntu:noble

ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update -y && \
    apt-get install --no-install-recommends build-essential python3 python-is-python3 \
                    qt6-base-dev qt6-5compat-dev qt6-translations-l10n \
                    qt6-tools-dev-tools qt6-base-dev-tools qt6-tools-dev \
                    libboost-dev libicu-dev libssl-dev libcurl4-openssl-dev \
                    ragel ninja-build zlib1g-dev git pkg-config \
                    wget ca-certificates fuse libfuse2 file -y && \
    apt-get clean && rm -rf /var/lib/apt/lists/*

# CMake 3.31.11 with integrity verification
ENV CMAKE_VERSION=3.31.11
ENV CMAKE_SHA256=e1ddad49d24a196d3b09c6a29f49408a418a3de096fb10fbd3626aeb22d9312b
RUN wget -q https://github.com/Kitware/CMake/releases/download/v${CMAKE_VERSION}/cmake-${CMAKE_VERSION}-linux-x86_64.sh && \
    echo "${CMAKE_SHA256}  cmake-${CMAKE_VERSION}-linux-x86_64.sh" | sha256sum -c - && \
    chmod 755 cmake-${CMAKE_VERSION}-linux-x86_64.sh && \
    ./cmake-${CMAKE_VERSION}-linux-x86_64.sh --prefix=/opt/ --exclude-subdir --skip-license && \
    rm cmake-${CMAKE_VERSION}-linux-x86_64.sh

ENV PATH=/opt/bin:$PATH
