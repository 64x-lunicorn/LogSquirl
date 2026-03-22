set(QAPPLICATION_CLASS QApplication)

include(CPM)

set(_TMP_CPM_USE_LOCAL_PACKAGES ${CPM_USE_LOCAL_PACKAGES})

cpmaddpackage("gh:simdutf/simdutf@5.6.2")
cpmaddpackage("gh:foonathan/type_safe@0.2.4")
cpmaddpackage(
  NAME CRoaring
  GITHUB_REPOSITORY RoaringBitmap/CRoaring
  VERSION 4.2.1
  OPTIONS "ROARING_USE_CPM OFF" "ENABLE_ROARING_TESTS OFF"
)
cpmaddpackage("gh:lemire/streamvbyte@1.0.0")

if(APPLE)
  cpmaddpackage(
    NAME
    macdeployqtfix
    GITHUB_REPOSITORY
    arl/macdeployqtfix
    GIT_TAG
    df888505849d3c06d20a4338af276dfa7d11826a
    DOWNLOAD_ONLY
    YES
  )
  if(macdeployqtfix_ADDED)
    configure_file(${macdeployqtfix_SOURCE_DIR}/macdeployqtfix.py ${CMAKE_BINARY_DIR}/macdeployqtfix.py COPYONLY)
  endif()
endif()

cpmaddpackage(
  NAME
  maddy
  GITHUB_REPOSITORY
  variar/maddy
  GIT_TAG
  602e26613e624535e2de883b3f2c98a16729d1d4
  EXCLUDE_FROM_ALL
  YES
)

if(LOGSQUIRL_USE_HYPERSCAN)
  cpmaddpackage(
    NAME
    hyperscan
    GITHUB_REPOSITORY
    variar/hyperscan
    GIT_TAG
    0931a40e0cf1d7f92189bc546c3491ed5c113f8b
    EXCLUDE_FROM_ALL
    YES
  )
  message("Adding alias for hyperscan")
  add_library(logsquirl_hyperscan INTERFACE)
  target_link_libraries(logsquirl_hyperscan INTERFACE hs)
  target_include_directories(logsquirl_hyperscan INTERFACE ${hyperscan_SOURCE_DIR}/src)
elseif(LOGSQUIRL_USE_VECTORSCAN)
  cpmaddpackage(
    NAME
    vectorscan
    GITHUB_REPOSITORY
    VectorCamp/vectorscan
    GIT_TAG
    d29730e1cb9daaa66bda63426cdce83505d2c809
    EXCLUDE_FROM_ALL
    YES
    OPTIONS
    "BUILD_STATIC_LIBS ON"
    "BUILD_UNIT OFF"
    "BUILD_TOOLS OFF"
    "BUILD_EXAMPLES OFF"
    "BUILD_BENCHMARKS OFF"
    "BUILD_DOC OFF"
    "BUILD_CHIMERA OFF"
    "BUIlD_AVX2 OFF"
    "BUIlD_AVX512 OFF"
    "BUIlD_AVX512VBMI OFF"
    "FAT_RUNTIME OFF"
  )
 
  message("Adding alias for vectorscan")
  add_library(logsquirl_vectorscan INTERFACE)
  target_link_libraries(logsquirl_vectorscan INTERFACE hs)
  target_include_directories(logsquirl_vectorscan INTERFACE 
    ${vectorscan_SOURCE_DIR}/src
    ${vectorscan_BINARY_DIR}
  )
endif()

cpmaddpackage(
  NAME
  Uchardet
  GIT_REPOSITORY
  https://gitlab.freedesktop.org/uchardet/uchardet
  VERSION
  0.0.8
  EXCLUDE_FROM_ALL
  YES
  OPTIONS
  "BUILD_BINARY OFF"
)
if(Uchardet_ADDED)
  message("Adding alias for uchardet")
  add_library(logsquirl_uchardet_wrapper INTERFACE)
  target_link_libraries(logsquirl_uchardet_wrapper INTERFACE libuchardet)
  target_include_directories(logsquirl_uchardet_wrapper INTERFACE ${Uchardet_SOURCE_DIR}/src)
else()
  add_library(logsquirl_uchardet_wrapper INTERFACE)
  target_link_libraries(logsquirl_uchardet_wrapper INTERFACE ${UCHARDET_LIBRARY})
  target_include_directories(logsquirl_uchardet_wrapper INTERFACE ${UCHARDET_INCLUDE_DIR})
endif()

# KArchive: try system package first, fall back to CPM-fetched klogg_karchive fork
add_library(logsquirl_karchive INTERFACE)
set(_KARCHIVE_FOUND OFF)
if(${QT_VERSION_MAJOR} EQUAL 6)
  find_package(KF6Archive QUIET)
  if(KF6Archive_FOUND)
    target_link_libraries(logsquirl_karchive INTERFACE KF6::Archive)
    set(_KARCHIVE_FOUND ON)
  endif()
else()
  find_package(KF5Archive QUIET)
  if(KF5Archive_FOUND)
    target_link_libraries(logsquirl_karchive INTERFACE KF5::Archive)
    set(_KARCHIVE_FOUND ON)
  endif()
endif()

if(NOT _KARCHIVE_FOUND)
  set(CPM_USE_LOCAL_PACKAGES OFF)
  cpmaddpackage(
    NAME
    Klogg_KArchive
    GITHUB_REPOSITORY
    variar/klogg_karchive
    GIT_TAG
    f546bf6ae66a8d34b43da5a41afcfbf4e1a47906
    EXCLUDE_FROM_ALL
    YES
  )
  if(Klogg_KArchive_ADDED)
    # Qt 6.10 no longer allows QFlags to be passed to QString::arg() directly.
    # Patch the klogg_karchive source to cast to int.
    foreach(_karchive_src
      "${Klogg_KArchive_SOURCE_DIR}/karchive/src/karchive.cpp"
      "${Klogg_KArchive_SOURCE_DIR}/karchive/src/kar.cpp"
      "${Klogg_KArchive_SOURCE_DIR}/karchive/src/krcc.cpp"
    )
      if(EXISTS "${_karchive_src}")
        file(READ "${_karchive_src}" _karchive_content)
        string(REPLACE ".arg(mode)" ".arg(static_cast<int>(mode))" _karchive_content "${_karchive_content}")
        string(REPLACE ".arg(d->mode)" ".arg(static_cast<int>(d->mode))" _karchive_content "${_karchive_content}")
        file(WRITE "${_karchive_src}" "${_karchive_content}")
      endif()
    endforeach()
    target_link_libraries(logsquirl_karchive INTERFACE klogg_karchive)
  else()
    message(WARNING "KArchive not found and CPM fetch failed")
  endif()
endif()

if(NOT _KARCHIVE_FOUND)
  message("Resetting CPM_USE_LOCAL_PACKAGES to ${_TMP_CPM_USE_LOCAL_PACKAGES}")
  set(CPM_USE_LOCAL_PACKAGES ${_TMP_CPM_USE_LOCAL_PACKAGES})
endif()

cpmaddpackage(
  NAME
  robin_hood
  GITHUB_REPOSITORY
  martinus/robin-hood-hashing
  GIT_TAG
  3.11.5
  EXCLUDE_FROM_ALL
  YES
)
# GCC 14 compatibility: robin_hood 3.11.x has two issues:
# 1. Missing <cstdint> include for UINT64_C macro
# 2. Brace-init of Iter{} fails stricter overload resolution
if(robin_hood_ADDED)
  set(_rh_header "${robin_hood_SOURCE_DIR}/src/include/robin_hood.h")
  if(EXISTS "${_rh_header}")
    file(READ "${_rh_header}" _rh_content)
    # Add missing <cstdint> include
    string(REPLACE "#include <cstdlib>" "#include <cstdint>\n#include <cstdlib>" _rh_content "${_rh_content}")
    # Replace brace-init with parenthesized init for Iter construction (GCC 14 compat)
    string(REPLACE
      "iterator{mKeyVals + idx, mInfo + idx}"
      "iterator(mKeyVals + idx, mInfo + idx)"
      _rh_content "${_rh_content}")
    string(REPLACE
      "const_iterator{mKeyVals + idx, mInfo + idx}"
      "const_iterator(mKeyVals + idx, mInfo + idx)"
      _rh_content "${_rh_content}")
    string(REPLACE
      "iterator{reinterpret_cast_no_cast_align_warning<Node*>(mInfo), nullptr}"
      "iterator(reinterpret_cast_no_cast_align_warning<Node*>(mInfo), nullptr)"
      _rh_content "${_rh_content}")
    string(REPLACE
      "const_iterator{reinterpret_cast_no_cast_align_warning<Node*>(mInfo), nullptr}"
      "const_iterator(reinterpret_cast_no_cast_align_warning<Node*>(mInfo), nullptr)"
      _rh_content "${_rh_content}")
    string(REPLACE
      "erase(iterator{const_cast<Node*>(pos.mKeyVals), const_cast<uint8_t*>(pos.mInfo)})"
      "erase(iterator(const_cast<Node*>(pos.mKeyVals), const_cast<uint8_t*>(pos.mInfo)))"
      _rh_content "${_rh_content}")
    file(WRITE "${_rh_header}" "${_rh_content}")
  endif()
endif()
if(NOT TARGET robin_hood)
  message("Adding imported target for robin_hood")
  add_library(robin_hood INTERFACE)
  target_include_directories(robin_hood INTERFACE ${ROBIN_HOOD_INCLUDE_DIRS})
endif(NOT TARGET robin_hood)

if(LOGSQUIRL_BUILD_TESTS)
  cpmaddpackage("gh:bombela/backward-cpp@1.6")
  cpmaddpackage(
    NAME
    Catch2
    GITHUB_REPOSITORY
    catchorg/Catch2
    VERSION
    2.13.8
    EXCLUDE_FROM_ALL
    YES
  )
  if(NOT TARGET Catch2)
    message("Adding imported target for catch2")
    add_library(Catch2 INTERFACE)
    target_include_directories(Catch2 INTERFACE ${CATCH2_INCLUDE_DIRS})
  endif(NOT TARGET Catch2)
endif()

cpmaddpackage(
  NAME
  KDSingleApplication
  GITHUB_REPOSITORY
  variar/KDSingleApplication
  GIT_TAG
  5b30db30266f92bc01f1439777803ce8dbf16c79
  DOWNLOAD_ONLY
  YES
)
if(KDSingleApplication_ADDED)
  add_library(
    kdsingleapp STATIC ${KDSingleApplication_SOURCE_DIR}/src/kdsingleapplication.cpp
                     ${KDSingleApplication_SOURCE_DIR}/src/kdsingleapplication_localsocket.cpp
  )

  target_include_directories(kdsingleapp PUBLIC ${KDSingleApplication_SOURCE_DIR}/src)
  target_link_libraries(kdsingleapp Qt${QT_VERSION_MAJOR}::Core)
  target_compile_definitions(kdsingleapp PUBLIC KDSINGLEAPPLICATION_STATIC_BUILD)

  set_target_properties(kdsingleapp PROPERTIES AUTOMOC ON)
endif()


cpmaddpackage(
  NAME
  xxHash
  GITHUB_REPOSITORY
  Cyan4973/xxHash
  VERSION
  0.8.1
  DOWNLOAD_ONLY
  YES
)
if(xxHash_ADDED)
  set(XXHASH_BUILD_ENABLE_INLINE_API OFF)
  set(XXHASH_BUILD_XXHSUM OFF)
  add_subdirectory(${xxHash_SOURCE_DIR}/cmake_unofficial ${CMAKE_BINARY_DIR}/xxhash EXCLUDE_FROM_ALL)
endif()

cpmaddpackage(
  NAME
  whereami
  GITHUB_REPOSITORY
  gpakosz/whereami
  GIT_TAG
  dcb52a058dc14530ba9ae05e4339bd3ddfae0e0e
  DOWNLOAD_ONLY
  YES
)
if(whereami_ADDED)
  add_library(whereami STATIC ${whereami_SOURCE_DIR}/src/whereami.h ${whereami_SOURCE_DIR}/src/whereami.c)
  target_include_directories(whereami PUBLIC ${whereami_SOURCE_DIR}/src)
endif()

cpmaddpackage(
  NAME
  exprtk
  GITHUB_REPOSITORY
  variar/klogg_exprtk
  GIT_TAG
  1f9f4cd7d2620b7b24232de9ea22908d63913459
  DOWNLOAD_ONLY
  YES
)
if(exprtk_ADDED)
  add_library(exprtk INTERFACE)
  target_link_libraries(exprtk INTERFACE robin_hood)
  target_include_directories(exprtk SYSTEM INTERFACE ${exprtk_SOURCE_DIR})
  target_compile_definitions(
    exprtk
    INTERFACE -Dexprtk_disable_caseinsensitivity
              -Dexprtk_disable_comments
              -Dexprtk_disable_break_continue
              -Dexprtk_disable_return_statement
              -Dexprtk_disable_superscalar_unroll
              -Dexprtk_disable_rtl_io_file
              -Dexprtk_disable_rtl_vecops
              -Dexprtk_disable_string_capabilities
  )
endif()

cpmaddpackage(
  NAME
  KDToolBox
  GITHUB_REPOSITORY
  KDAB/KDToolBox
  GIT_TAG
  6468867d1a46eabe1bcb2cd342f338fe66f06675
  DOWNLOAD_ONLY
  YES
)
if(KDToolBox_ADDED)
  add_library(
    kdtoolbox STATIC ${KDToolBox_SOURCE_DIR}/qt/KDSignalThrottler/src/KDSignalThrottler.cpp
                     ${KDToolBox_SOURCE_DIR}/qt/KDSignalThrottler/src/KDSignalThrottler.h
  )

  target_include_directories(kdtoolbox PUBLIC ${KDToolBox_SOURCE_DIR}/qt/KDSignalThrottler/src)
  target_link_libraries(kdtoolbox Qt${QT_VERSION_MAJOR}::Core)
  set_target_properties(kdtoolbox PROPERTIES AUTOMOC ON)
endif()

cpmaddpackage(
  NAME
  efsw
  GITHUB_REPOSITORY
  SpartanJ/efsw
  GIT_TAG
  1.4.1
  EXCLUDE_FROM_ALL
  YES
  OPTIONS
  "BUILD_TEST_APP OFF"
)
if(efsw_ADDED)
  target_compile_definitions(efsw PRIVATE EFSW_FSEVENTS_NOT_SUPPORTED)
endif()

if(WIN32)
  set(BUILD_SHARED_LIBS ON)
endif()
cpmaddpackage(
  NAME
  tbb
  GITHUB_REPOSITORY
  variar/oneTBB
  GIT_TAG
  c9be1ac2930f02dea523003ed801b4489f3e6b6e
  EXCLUDE_FROM_ALL
  YES
  OPTIONS
  "TBB_TEST OFF"
  "TBB_EXAMPLES OFF"
  "TBB_STRICT OFF"
)

if(WIN32)
  set(BUILD_SHARED_LIBS OFF)
endif()

set(CPM_USE_LOCAL_PACKAGES OFF)
cpmaddpackage(
  NAME
  mimalloc
  GITHUB_REPOSITORY
  microsoft/mimalloc
  VERSION
  2.1.7
  EXCLUDE_FROM_ALL
  YES
  OPTIONS
  "MI_BUILD_TESTS OFF"
  "MI_SECURE OFF"
  "MI_BUILD_SHARED OFF"
  "MI_BUILD_STATIC ON"
  "MI_BUILD_OBJECT OFF"
  "MI_OVERRIDE OFF"
)
if(mimalloc_ADDED)
  message("Adding alias for mimalloc")
  add_library(logsquirl_mimalloc_wrapper INTERFACE)
  target_link_libraries(logsquirl_mimalloc_wrapper INTERFACE mimalloc-static)
  target_include_directories(logsquirl_mimalloc_wrapper INTERFACE ${mimalloc_SOURCE_DIR}/include)
else()
  add_library(logsquirl_mimalloc_wrapper INTERFACE)
  target_link_libraries(logsquirl_mimalloc_wrapper INTERFACE ${MIMALLOC_LIBRARY})
  target_include_directories(logsquirl_mimalloc_wrapper INTERFACE ${MIMALLOC_INCLUDE_DIR})
endif()

set(CPM_USE_LOCAL_PACKAGES ${_TMP_CPM_USE_LOCAL_PACKAGES})

if(LOGSQUIRL_USE_SENTRY)
  set(SENTRY_BACKEND
     "crashpad"
     CACHE INTERNAL "" FORCE
  )
  set(SENTRY_TRANSPORT
     "none"
     CACHE INTERNAL "" FORCE
  )
  set(SENTRY_BUILD_EXAMPLES
     OFF
     CACHE INTERNAL "" FORCE
  )
  set(CRASHPAD_ENABLE_INSTALL
     OFF
     CACHE INTERNAL "" FORCE
  )
  set(CRASHPAD_ENABLE_INSTALL_DEV
     OFF
     CACHE INTERNAL "" FORCE
  )

  # Crashpad requires CMAKE_OSX_SYSROOT to locate mach/*.defs files.
  # With Makefile/Ninja generators CMake may leave this empty; detect it.
  if(APPLE AND NOT CMAKE_OSX_SYSROOT)
    execute_process(
      COMMAND xcrun --show-sdk-path
      OUTPUT_VARIABLE _osx_sdk_path
      OUTPUT_STRIP_TRAILING_WHITESPACE
    )
    set(CMAKE_OSX_SYSROOT "${_osx_sdk_path}" CACHE STRING "" FORCE)
  endif()

  cpmaddpackage(
    NAME
    sentry
    GITHUB_REPOSITORY
    getsentry/sentry-native
    GIT_TAG
    a3d58622a807b9dda174cb9fc18fa0f98c89d043
    EXCLUDE_FROM_ALL
    YES
  )

  # GCC 15 / Clang 17+ no longer transitively include <cstdint> from standard
  # headers.  The pinned crashpad revision uses uint64_t etc. without including
  # <cstdint> in several files.  Force-include the header for all crashpad targets.
  # Use a generator expression so the C++ header is only applied to CXX sources
  # (crashpad_util also compiles MIG-generated .c files).
  if(MSVC)
    set(_sentry_cstdint_flag "$<$<COMPILE_LANGUAGE:CXX>:/FIcstdint>")
  else()
    set(_sentry_cstdint_flag "$<$<COMPILE_LANGUAGE:CXX>:SHELL:-include cstdint>")
  endif()
  set(_sentry_cstdint_targets
      sentry crashpad_client crashpad_compat crashpad_util crashpad_handler
      crashpad_minidump crashpad_snapshot crashpad_tools mini_chromium)
  foreach(_t ${_sentry_cstdint_targets})
    if(TARGET ${_t})
      get_target_property(_t_type ${_t} TYPE)
      if(_t_type STREQUAL "INTERFACE_LIBRARY")
        target_compile_options(${_t} INTERFACE ${_sentry_cstdint_flag})
      else()
        target_compile_options(${_t} PRIVATE ${_sentry_cstdint_flag})
      endif()
    endif()
  endforeach()

  if(WIN32)
    set_target_properties(crashpad_handler PROPERTIES OUTPUT_NAME "logsquirl_crashpad_handler.exe")
  else()
    set_target_properties(crashpad_handler PROPERTIES OUTPUT_NAME "logsquirl_crashpad_handler")
  endif()
endif(LOGSQUIRL_USE_SENTRY)

set(logsquirl_cpm_targets
    xxhash
    Catch2
    roaring
    roaring-headers-cpp
    libuchardet
    robin_hood
    whereami
    simdutf
    efsw
    SingleApplication
    hs
    tbb
    exprtk
    type_safe
    mimalloc-static
    sentry
    crashpad_client
    crashpad_compat
    crashpad_util
    mini_chromium
    streamvbyte
    FastPFor
)
foreach(target ${logsquirl_cpm_targets})
  if(TARGET ${target})
    get_property(
      ${target}_include_dirs
      TARGET ${target}
      PROPERTY INTERFACE_INCLUDE_DIRECTORIES
    )
    #message("Marking ${${target}_include_dirs} as system incldue")
    set_property(TARGET ${target} PROPERTY INTERFACE_SYSTEM_INCLUDE_DIRECTORIES ${${target}_include_dirs})
  endif()
endforeach()


