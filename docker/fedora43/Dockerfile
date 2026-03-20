FROM fedora:43

RUN dnf update -y && \
    dnf install -y ragel make wget python3 rpm-build git unzip \
    qt6-qtbase-devel qt6-qttools-devel qt6-qt5compat-devel \
    openssl-devel boost-devel zlib-devel libcurl-devel \
    gcc gcc-c++ elfutils-devel ninja-build cmake && \
    dnf clean all
