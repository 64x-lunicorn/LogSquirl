FROM oraclelinux:10

COPY epel.repo /etc/yum.repos.d

RUN dnf update -y && \
    dnf install -y ragel make wget python3 rpm-build git \
    qt6-qtbase-devel qt6-qttools-devel qt6-qt5compat-devel qt6-linguist \
    openssl-devel boost-devel zlib-devel libcurl-devel \
    gcc gcc-c++ elfutils-devel && \
    dnf clean all

RUN wget https://github.com/Kitware/CMake/releases/download/v3.20.2/cmake-3.20.2-linux-x86_64.sh && \
    chmod 755 cmake-3.20.2-linux-x86_64.sh && \
    ./cmake-3.20.2-linux-x86_64.sh --prefix=/opt/ --exclude-subdir --skip-license && \
    rm cmake-3.20.2-linux-x86_64.sh

RUN wget https://github.com/ninja-build/ninja/releases/download/v1.10.2/ninja-linux.zip && \
    unzip ninja-linux.zip && chmod 755 ninja && mv ninja /opt/bin && rm ninja-linux.zip

ENV PATH=/opt/bin:$PATH
