# KArchive, KDE Frameworks' archive library, built from the upstream sources
# CPM fetched into ${KArchive_SOURCE_DIR} (3rdparty/CMakeLists.txt, #834).
#
# Upstream's own CMake files need Extra CMake Modules, KDE's install layout and
# pkg-config, and look for their compression libraries on the system. Here the
# sources are compiled directly, the way the klogg_karchive fork this replaced
# compiled its 2022 copy of them, against the zlib, bzip2 and xz packages
# 3rdparty/CMakeLists.txt adds beside KArchive. The files ECM would generate
# are stood in for:
#   karchive_export.h       export and deprecation macros, static library
#   loggingcategory.h/.cpp  the kf.archive logging category
#   config-compression.h    configured below from upstream's template
# Left out: the translations loader (ecm_create_qm_loader; the messages stay
# English), OpenSSL (AES-encrypted 7z archives) and zstd (LogSquirl's own
# ZstdDevice in src/compression reads .zst).
#
# Nothing of LogSquirl's includes KArchive through a KF6::Archive target name:
# src/ links logsquirl_karchive, which 3rdparty/CMakeLists.txt points either
# here or at a system KF6Archive.

set(_karchive_src "${KArchive_SOURCE_DIR}/src")

set(HAVE_BZIP2_SUPPORT 1)
# bzip2 1.0.x names its functions BZ2_*.
set(NEED_BZ2_PREFIX 1)
set(HAVE_XZ_SUPPORT 1)
set(HAVE_OPENSSL_SUPPORT 0)
set(HAVE_ZSTD_SUPPORT 0)
configure_file("${_karchive_src}/config-compression.h.cmake" "${CMAKE_CURRENT_BINARY_DIR}/config-compression.h")

# The source list of upstream's src/CMakeLists.txt: the unconditional sources,
# then the ones it adds with bzip2 and with liblzma.
add_library(
  karchive STATIC
  ${_karchive_src}/karchive.cpp
  ${_karchive_src}/kar.cpp
  ${_karchive_src}/kcompressiondevice.cpp
  ${_karchive_src}/kfilterbase.cpp
  ${_karchive_src}/kgzipfilter.cpp
  ${_karchive_src}/klimitediodevice.cpp
  ${_karchive_src}/knonefilter.cpp
  ${_karchive_src}/ktar.cpp
  ${_karchive_src}/kzip.cpp
  ${_karchive_src}/krcc.cpp
  ${_karchive_src}/kbzip2filter.cpp
  ${_karchive_src}/kxzfilter.cpp
  ${_karchive_src}/klzfilter.cpp
  ${_karchive_src}/k7zip.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/loggingcategory.cpp
)

# KCompressionDevice and KLimitedIODevice are QObjects.
set_target_properties(karchive PROPERTIES AUTOMOC ON)

# The public headers include karchive_export.h, so the stand-ins are public
# too; config-compression.h is only included by the sources and the filters'
# private headers. liblzma's headers are named by hand: xz's CMake files put
# them on the target for an installed copy only (INSTALL_INTERFACE).
target_include_directories(
  karchive
  PUBLIC "${_karchive_src}" "${CMAKE_CURRENT_SOURCE_DIR}"
  PRIVATE "${CMAKE_CURRENT_BINARY_DIR}" "${xz_SOURCE_DIR}/src/liblzma/api"
)
target_link_libraries(
  karchive
  PUBLIC Qt6::Core
  PRIVATE logsquirl_zlib logsquirl_bzip2 liblzma
)
# What upstream's src/CMakeLists.txt sets for the whole directory.
target_compile_definitions(karchive PRIVATE QT_NO_CAST_FROM_ASCII)
