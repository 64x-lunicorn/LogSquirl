FROM oraclelinux:10

COPY epel.repo /etc/yum.repos.d

RUN dnf update -y && \
    dnf install -y make wget python3 rpm-build git \
    qt6-qtbase-devel qt6-qttools-devel qt6-qt5compat-devel qt6-linguist \
    openssl-devel boost-devel zlib-devel libcurl-devel \
    gcc gcc-c++ elfutils-devel && \
    dnf clean all

# ragel is not packaged for OL10; build from source
RUN wget https://www.colm.net/files/ragel/ragel-6.10.tar.gz && \
    tar xzf ragel-6.10.tar.gz && \
    cd ragel-6.10 && ./configure --prefix=/usr/local && make -j$(nproc) && make install && \
    cd .. && rm -rf ragel-6.10 ragel-6.10.tar.gz

RUN wget https://github.com/Kitware/CMake/releases/download/v3.28.3/cmake-3.28.3-linux-x86_64.sh && \
    chmod 755 cmake-3.28.3-linux-x86_64.sh && \
    ./cmake-3.28.3-linux-x86_64.sh --prefix=/opt/ --exclude-subdir --skip-license && \
    rm cmake-3.28.3-linux-x86_64.sh

RUN wget https://github.com/ninja-build/ninja/releases/download/v1.10.2/ninja-linux.zip && \
    unzip ninja-linux.zip && chmod 755 ninja && mv ninja /opt/bin && rm ninja-linux.zip

ENV PATH=/opt/bin:$PATH
