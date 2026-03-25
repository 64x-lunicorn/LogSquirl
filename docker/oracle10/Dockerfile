FROM oraclelinux:10

COPY epel.repo /etc/yum.repos.d

RUN dnf update -y && \
    dnf install -y make wget python3 rpm-build git unzip \
    qt6-qtbase-devel qt6-qttools-devel qt6-qt5compat-devel qt6-linguist \
    openssl-devel boost-devel zlib-devel libcurl-devel \
    gcc gcc-c++ elfutils-devel && \
    dnf clean all

# ragel is not packaged for OL10; build from source
# Install to /usr (not /usr/local) because CI mounts workspace over /usr/local
ENV RAGEL_VERSION=6.10
ENV RAGEL_SHA256=5f156edb65d20b856d638dd9ee2dfb43285914d9aa2b6ec779dac0270cd56c3f
RUN wget -q https://www.colm.net/files/ragel/ragel-${RAGEL_VERSION}.tar.gz && \
    echo "${RAGEL_SHA256}  ragel-${RAGEL_VERSION}.tar.gz" | sha256sum -c - && \
    tar xzf ragel-${RAGEL_VERSION}.tar.gz && \
    cd ragel-${RAGEL_VERSION} && ./configure --prefix=/usr && make -j$(nproc) && make install && \
    cd .. && rm -rf ragel-${RAGEL_VERSION} ragel-${RAGEL_VERSION}.tar.gz

# CMake 3.31.11 with integrity verification
ENV CMAKE_VERSION=3.31.11
ENV CMAKE_SHA256=e1ddad49d24a196d3b09c6a29f49408a418a3de096fb10fbd3626aeb22d9312b
RUN wget -q https://github.com/Kitware/CMake/releases/download/v${CMAKE_VERSION}/cmake-${CMAKE_VERSION}-linux-x86_64.sh && \
    echo "${CMAKE_SHA256}  cmake-${CMAKE_VERSION}-linux-x86_64.sh" | sha256sum -c - && \
    chmod 755 cmake-${CMAKE_VERSION}-linux-x86_64.sh && \
    ./cmake-${CMAKE_VERSION}-linux-x86_64.sh --prefix=/opt/ --exclude-subdir --skip-license && \
    rm cmake-${CMAKE_VERSION}-linux-x86_64.sh

# Ninja 1.13.2 with integrity verification
ENV NINJA_VERSION=1.13.2
ENV NINJA_SHA256=5749cbc4e668273514150a80e387a957f933c6ed3f5f11e03fb30955e2bbead6
RUN wget -q https://github.com/ninja-build/ninja/releases/download/v${NINJA_VERSION}/ninja-linux.zip && \
    echo "${NINJA_SHA256}  ninja-linux.zip" | sha256sum -c - && \
    unzip ninja-linux.zip && chmod 755 ninja && mv ninja /opt/bin && rm ninja-linux.zip

ENV PATH=/opt/bin:$PATH
