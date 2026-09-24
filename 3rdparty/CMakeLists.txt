set(QAPPLICATION_CLASS QApplication)

# Third-party code is not built with the warnings LogSquirl holds its own code
# to (#452). Set here, before the first package is added, these flags reach
# every directory CPM adds below this one and no directory above it: src/ and
# tests/ keep the /W4 the top-level CMakeLists.txt gave them. The targets the
# packages define are gone over again at the end of this file, for the packages
# that turn warnings back on for themselves.
include(ThirdPartyWarnings)
if(MSVC)
  set(_LOGSQUIRL_THIRD_PARTY_MSVC ON)
else()
  set(_LOGSQUIRL_THIRD_PARTY_MSVC OFF)
endif()
logsquirl_third_party_quiet_flags(CMAKE_C_FLAGS MSVC ${_LOGSQUIRL_THIRD_PARTY_MSVC} FLAGS "${CMAKE_C_FLAGS}")
logsquirl_third_party_quiet_flags(CMAKE_CXX_FLAGS MSVC ${_LOGSQUIRL_THIRD_PARTY_MSVC} FLAGS "${CMAKE_CXX_FLAGS}")

include(CPM)

set(_TMP_CPM_USE_LOCAL_PACKAGES ${CPM_USE_LOCAL_PACKAGES})

# Every package below is fetched by a full 40-character commit SHA (#209).
# A tag or a bare VERSION (which CPM turns into the tag v<VERSION>) can be
# moved upstream, and CPM verifies nothing, so the same LogSquirl commit could
# build different third-party code. Packages cut from a release keep VERSION
# and a "# <tag>" comment next to the SHA: that is the human-readable version to
# review and bump, and CPM still uses VERSION for find_package() when
# CPM_USE_LOCAL_PACKAGES is on. With both given, CPM fetches GIT_TAG. Forks
# without releases (maddy, tbb, ...) were already pinned by SHA. To update a package, resolve the new tag's commit with
#   git ls-remote https://github.com/<repo> 'refs/tags/<tag>' 'refs/tags/<tag>^{}'
# (the ^{} line, when present, is the commit of an annotated tag).
#
# The release SBOM is generated from the CPMAddPackage calls in this file
# (scripts/sbom/logsquirl_sbom.py, #212). Keep the NAME/GITHUB_REPOSITORY or
# GIT_REPOSITORY/GIT_TAG form; a package under a new if() condition fails the
# SBOM job until the condition is classified in CONDITION_EFFECTS there.

cpmaddpackage(
  NAME simdutf
  GITHUB_REPOSITORY simdutf/simdutf
  VERSION 5.6.2
  GIT_TAG 58c12ff5d84665e26e80547e5f0d72b29ba1c5d4 # v5.6.2
  # The gh: shorthand this and the other expanded calls came from implied
  # EXCLUDE_FROM_ALL and SYSTEM; without them their install rules (simdutf's
  # tools) join the packages (#209).
  EXCLUDE_FROM_ALL YES
  SYSTEM YES
)
cpmaddpackage(
  NAME type_safe
  GITHUB_REPOSITORY foonathan/type_safe
  VERSION 0.2.4
  GIT_TAG 1dbea7936a1e389caa692d01bb3f5ba4b6da7d82 # v0.2.4
  EXCLUDE_FROM_ALL YES
  SYSTEM YES
)
cpmaddpackage(
  NAME CRoaring
  GITHUB_REPOSITORY RoaringBitmap/CRoaring
  VERSION 4.2.1
  GIT_TAG 3ef7b44a4f74af09fe19bb84d30bc35cab130561 # v4.2.1
  OPTIONS "ROARING_USE_CPM OFF" "ENABLE_ROARING_TESTS OFF"
  # Without it CRoaring's install rules put libroaring.a, its headers and CMake
  # config into the deb and rpm packages (#226).
  EXCLUDE_FROM_ALL YES
)
cpmaddpackage(
  NAME streamvbyte
  GITHUB_REPOSITORY lemire/streamvbyte
  VERSION 1.0.0
  GIT_TAG c43294a81501e0fdf14adc83818d47f7f9bc1bb6 # v1.0.0
  EXCLUDE_FROM_ALL YES
  SYSTEM YES
  # On by default, and it calls add_compile_options(/Wall) on MSVC, which cl
  # reports as "D9025: overriding '/W4' with '/Wall'" once per file -- the
  # directory flags this file sets cannot take back what a package adds after
  # them (#452).
  OPTIONS "STREAMVBYTE_WALL OFF"
)

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

if(LOGSQUIRL_USE_VECTORSCAN)
  # Both Hyperscan (Intel) and Vectorscan (VectorCamp) provide the identical
  # hs_* API.  Vectorscan does not support MSVC, so on Windows we use the
  # variar/hyperscan fork which carries MSVC patches and a bundled ragel.exe.
  # On all other platforms we use VectorCamp/vectorscan (latest develop).
  set(_SAVED_CXX_STANDARD ${CMAKE_CXX_STANDARD})
  set(CMAKE_CXX_STANDARD 17)

  if(WIN32)
    # -----------------------------------------------------------------
    #  Windows — use variar/hyperscan (Intel Hyperscan fork, MSVC-ready)
    # -----------------------------------------------------------------
    # A local build is compiled for its own CPU (/arch:AVX2 unless
    # LOGSQUIRL_GENERIC_CPU) and links Hyperscan statically. A generic x64
    # build, the release, must run on CPUs without AVX2 but use AVX2 where
    # the CPU has it. Hyperscan's fat runtime cannot do that here: it relies
    # on ELF ifunc and objcopy symbol renaming, which MSVC and PE do not have,
    # and two static copies would define the same symbols. So the release
    # ships Hyperscan twice as DLLs with the same exports, hs.dll for SSE4.2
    # and hs_avx2.dll built with /arch:AVX2; LogSquirl delay-loads hs.dll,
    # and the delay-load hook loads the build chosen for the CPU at the first
    # call into Hyperscan (src/regex/include/hyperscanruntime.h, #281).
    if(LOGSQUIRL_GENERIC_CPU AND LOGSQUIRL_TARGET_PROCESSOR MATCHES "^(AMD64|amd64|x64|x86_64)$")
      set(_HS_RUNTIME_DISPATCH ON)
    else()
      set(_HS_RUNTIME_DISPATCH OFF)
    endif()

    cpmaddpackage(
      NAME
      hyperscan
      GITHUB_REPOSITORY
      variar/hyperscan
      GIT_TAG
      0931a40e0cf1d7f92189bc546c3491ed5c113f8b
      EXCLUDE_FROM_ALL
      YES
      DOWNLOAD_ONLY
      ${_HS_RUNTIME_DISPATCH}
      OPTIONS
      "BUILD_STATIC_LIBS ON"
      "BUILD_SHARED_LIBS OFF"
      "BUILD_AVX512 OFF"
      "BUILD_AVX512VBMI OFF"
      "FAT_RUNTIME OFF"
    )

    # The fork ends its CMakeLists.txt with an unconditional
    # `target_compile_options(hs PRIVATE "-fpermissive")`, once for each of its
    # three libraries. cl does not know that GCC option and reports "D9002:
    # ignoring unknown option" for it once per file -- 320 times in a Windows
    # release build, where the two runtimes below compile these sources again
    # through their own CMake run, which nothing outside it can reach into. The
    # option is taken out of the sources rather than off the targets, the way
    # the KArchive fork is patched further down; for a build that adds the fork
    # as a subdirectory instead, the same option is taken off the targets by
    # logsquirl_third_party_build_quietly() at the end of this file (#452).
    # Only written when it actually changes something: this is the CPM source
    # cache, shared with every other worktree and build directory that pins the
    # same commit, so a configure that has nothing to change leaves its
    # timestamp alone. And it says so when the pattern is not there at all, the
    # one way this could quietly become a no-op if the fork ever reformats
    # those lines.
    if(_HS_RUNTIME_DISPATCH AND EXISTS "${hyperscan_SOURCE_DIR}/CMakeLists.txt")
      file(READ "${hyperscan_SOURCE_DIR}/CMakeLists.txt" _hyperscan_cmake)
      string(
        REPLACE " PRIVATE \"-fpermissive\")"
                " PRIVATE)"
                _hyperscan_patched
                "${_hyperscan_cmake}"
      )
      if(NOT _hyperscan_patched STREQUAL _hyperscan_cmake)
        file(WRITE "${hyperscan_SOURCE_DIR}/CMakeLists.txt" "${_hyperscan_patched}")
      elseif(NOT _hyperscan_cmake MATCHES " PRIVATE\\)")
        message(
          WARNING
            "The hyperscan fork no longer sets -fpermissive the way 3rdparty/CMakeLists.txt "
            "takes it out; a Windows build will print D9002 for it again (#452)."
        )
      endif()
    endif()

    add_library(logsquirl_vectorscan INTERFACE)
    target_include_directories(logsquirl_vectorscan INTERFACE ${hyperscan_SOURCE_DIR}/src)

    if(_HS_RUNTIME_DISPATCH)
      message("Adding hyperscan runtimes for SSE4.2 and AVX2 (Windows)")
      include(HyperscanWindowsRuntimes)
      logsquirl_add_hyperscan_windows_runtime(
        logsquirl_hyperscan_sse42
        SOURCE_DIR "${hyperscan_SOURCE_DIR}"
        DLL_NAME hs
        IMPORT_LIBRARY_VAR _HS_IMPORT_LIBRARY
      )
      logsquirl_add_hyperscan_windows_runtime(
        logsquirl_hyperscan_avx2
        SOURCE_DIR "${hyperscan_SOURCE_DIR}"
        DLL_NAME hs_avx2
        EXTRA_FLAGS /arch:AVX2
      )
      target_link_libraries(logsquirl_vectorscan INTERFACE "${_HS_IMPORT_LIBRARY}" delayimp)
      target_link_options(logsquirl_vectorscan INTERFACE /DELAYLOAD:hs.dll)
      target_compile_definitions(logsquirl_vectorscan INTERFACE LOGSQUIRL_HYPERSCAN_RUNTIME_DISPATCH)
    else()
      message("Adding alias for hyperscan (Windows)")
      target_link_libraries(logsquirl_vectorscan INTERFACE hs)
      target_include_directories(logsquirl_vectorscan INTERFACE ${hyperscan_BINARY_DIR})
    endif()
  else()
    # -----------------------------------------------------------------
    #  Linux / macOS — use VectorCamp/vectorscan (develop branch)
    # -----------------------------------------------------------------
    # FAT_RUNTIME is only supported on Linux x86_64.
    # AVX2/AVX512 are x86-only features (work on Linux and macOS).
    if(CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64|amd64|AMD64|x86")
      set(_VS_BUILD_AVX2 ON)
      set(_VS_BUILD_AVX512 ON)
    else()
      set(_VS_BUILD_AVX2 OFF)
      set(_VS_BUILD_AVX512 OFF)
    endif()

    if(CMAKE_SYSTEM_NAME STREQUAL "Linux" AND _VS_BUILD_AVX2)
      set(_VS_FAT_RUNTIME ON)
    else()
      set(_VS_FAT_RUNTIME OFF)
    endif()

    cpmaddpackage(
      NAME
      vectorscan
      GITHUB_REPOSITORY
      VectorCamp/vectorscan
      GIT_TAG
      b585ad466658624bb31fb1d194cdb168df34833c # vectorscan/5.4.12
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
      "BUILD_AVX2 ${_VS_BUILD_AVX2}"
      "BUILD_AVX512 ${_VS_BUILD_AVX512}"
      "BUILD_AVX512VBMI OFF"
      "FAT_RUNTIME ${_VS_FAT_RUNTIME}"
    )

    if(_VS_FAT_RUNTIME)
      # FAT_RUNTIME multiversions each source file through vectorscan's own
      # build_wrapper.sh (one compile per microarch variant, e.g. core2,
      # corei7, avx2), which post-processes the object file sccache just
      # produced. That breaks sccache's own bookkeeping of the object it
      # wrote ("failed to zip up compiler outputs" / file not found), so
      # these targets must build without the global compiler launcher.
      get_property(_VS_TARGETS DIRECTORY ${vectorscan_SOURCE_DIR} PROPERTY BUILDSYSTEM_TARGETS)
      foreach(_VS_TARGET ${_VS_TARGETS})
        set_target_properties(${_VS_TARGET} PROPERTIES C_COMPILER_LAUNCHER "" CXX_COMPILER_LAUNCHER "")
      endforeach()
    endif()

    message("Adding alias for vectorscan")
    add_library(logsquirl_vectorscan INTERFACE)
    target_link_libraries(logsquirl_vectorscan INTERFACE hs)
    target_include_directories(logsquirl_vectorscan INTERFACE
      ${vectorscan_SOURCE_DIR}/src
      ${vectorscan_BINARY_DIR}
    )
  endif()

  set(CMAKE_CXX_STANDARD ${_SAVED_CXX_STANDARD})
endif()

# MSVC's own spelling of the architecture ("x64", "ARM64"), or empty to let
# uchardet detect it as before -- see the OPTIONS of the call below. A 32-bit
# MSVC build is spelled "X86" and would still take uchardet's GCC branch;
# LogSquirl ships no 32-bit Windows package, so that stays as it is.
if(MSVC)
  # LOGSQUIRL_TARGET_PROCESSOR falls back to CMAKE_SYSTEM_PROCESSOR, which
  # spells the same architecture "AMD64" -- and uchardet's match does cover
  # that one.
  string(
    REPLACE "AMD64"
            "x64"
            _LOGSQUIRL_UCHARDET_ARCHITECTURE
            "${LOGSQUIRL_TARGET_PROCESSOR}"
  )
else()
  set(_LOGSQUIRL_UCHARDET_ARCHITECTURE "")
endif()

cpmaddpackage(
  NAME
  Uchardet
  GIT_REPOSITORY
  https://gitlab.freedesktop.org/uchardet/uchardet
  VERSION
  0.0.8
  GIT_TAG
  ae6302a016088ad07177f86d417b20010053632b # v0.0.8
  EXCLUDE_FROM_ALL
  YES
  OPTIONS
  "BUILD_BINARY OFF"
  # uchardet decides between -msse2 -mfpmath=sse and -ffloat-store whenever it
  # reads the target architecture as x86, amd or i686, and puts the answer into
  # CMAKE_C_FLAGS and CMAKE_CXX_FLAGS. Both answers are GCC options: on MSVC the
  # check fails, -ffloat-store is handed to cl, and cl reports "D9002: ignoring
  # unknown option" once per file, 50 times in a Windows build. Naming the
  # architecture the way MSVC spells it -- which that match does not cover --
  # skips the question. Nothing is lost: an MSVC x64 build has SSE2 either way,
  # and the flag it would otherwise add is one cl does not know (#452).
  "TARGET_ARCHITECTURE ${_LOGSQUIRL_UCHARDET_ARCHITECTURE}"
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
find_package(KF6Archive QUIET)
if(KF6Archive_FOUND)
  target_link_libraries(logsquirl_karchive INTERFACE KF6::Archive)
  set(_KARCHIVE_FOUND ON)
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
    target_compile_definitions(logsquirl_karchive INTERFACE LOGSQUIRL_KARCHIVE)
  else()
    message(WARNING "KArchive not found and CPM fetch failed")
  endif()
endif()

if(NOT _KARCHIVE_FOUND)
  message("Resetting CPM_USE_LOCAL_PACKAGES to ${_TMP_CPM_USE_LOCAL_PACKAGES}")
  set(CPM_USE_LOCAL_PACKAGES ${_TMP_CPM_USE_LOCAL_PACKAGES})
endif()

# ── Zstd: streaming decompression for .zst / .zstd files ─────────────────────
# Same version used by LogSquirl-Format-SDK; static build, no CLI tools.
add_library(logsquirl_zstd INTERFACE)
CPMAddPackage(
  NAME zstd
  GITHUB_REPOSITORY facebook/zstd
  VERSION 1.5.6
  GIT_TAG 794ea1b0afca0f020f4e57b6732332231fb23c70 # v1.5.6
  SOURCE_SUBDIR build/cmake
  EXCLUDE_FROM_ALL YES
  OPTIONS
    "ZSTD_BUILD_PROGRAMS OFF"
    "ZSTD_BUILD_TESTS OFF"
    "ZSTD_BUILD_SHARED OFF"
    "ZSTD_BUILD_STATIC ON"
)
if(TARGET libzstd_static)
  target_link_libraries(logsquirl_zstd INTERFACE libzstd_static)
elseif(TARGET zstd::libzstd_static)
  target_link_libraries(logsquirl_zstd INTERFACE zstd::libzstd_static)
else()
  message(WARNING "zstd static target not found — .zst decompression disabled")
endif()

# ── LZ4: streaming decompression for .lz4 files ─────────────────────────────
# Same version used by LogSquirl-Format-SDK; static build, no CLI tools.
add_library(logsquirl_lz4 INTERFACE)
CPMAddPackage(
  NAME lz4
  GITHUB_REPOSITORY lz4/lz4
  VERSION 1.10.0
  GIT_TAG ebb370ca83af193212df4dcbadcc5d87bc0de2f0 # v1.10.0
  SOURCE_SUBDIR build/cmake
  EXCLUDE_FROM_ALL YES
  OPTIONS
    "LZ4_BUILD_CLI OFF"
    "LZ4_BUILD_LEGACY_LZ4C OFF"
    "BUILD_SHARED_LIBS OFF"
    "BUILD_STATIC_LIBS ON"
)
if(TARGET lz4_static)
  target_link_libraries(logsquirl_lz4 INTERFACE lz4_static)
elseif(TARGET LZ4::lz4_static)
  target_link_libraries(logsquirl_lz4 INTERFACE LZ4::lz4_static)
else()
  message(WARNING "lz4 static target not found — .lz4 decompression disabled")
endif()

cpmaddpackage(
  NAME
  robin_hood
  GITHUB_REPOSITORY
  martinus/robin-hood-hashing
  VERSION
  3.11.5
  GIT_TAG
  9145f963d80d6a02f0f96a47758050a89184a3ed # 3.11.5
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
  cpmaddpackage(
    NAME backward-cpp
    GITHUB_REPOSITORY bombela/backward-cpp
    VERSION 1.6
    GIT_TAG 3bb9240cb15459768adb3e7d963a20e1523a6294 # v1.6
    EXCLUDE_FROM_ALL YES
    SYSTEM YES
  )
  cpmaddpackage(
    NAME
    Catch2
    GITHUB_REPOSITORY
    catchorg/Catch2
    VERSION
    2.13.8
    GIT_TAG
    216713a4066b79d9803d374f261ccb30c0fb451f # v2.13.8
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
  target_link_libraries(kdsingleapp Qt6::Core)
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
  GIT_TAG
  35b0373c697b5f160d3db26b1cbb45a0d5ba788c # v0.8.1
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
  target_link_libraries(kdtoolbox Qt6::Core)
  set_target_properties(kdtoolbox PROPERTIES AUTOMOC ON)
endif()

# efsw: at least 1.7.0: 1.4.1's Windows backend freed a removed directory watch
# while its cancelled read could still complete into it, corrupting the heap
# (#116); 1.7.0 frees it only once that read has completed. At least 1.7.2,
# whose FileWatchListener signature filewatcher.cpp overrides. (Above the call,
# not between GITHUB_REPOSITORY and VERSION, so Renovate's CPM pattern reads the
# block and its version bump cannot rewrite the numbers here; #211.)
cpmaddpackage(
  NAME
  efsw
  GITHUB_REPOSITORY
  SpartanJ/efsw
  VERSION
  1.7.2
  GIT_TAG
  41ddf6822f2d0dec7e14fafa09c4cef391137b20 # 1.7.2
  EXCLUDE_FROM_ALL
  YES
  OPTIONS
  "BUILD_TEST_APP OFF"
  # Since 1.7 efsw also builds an efsw-static target by default; the efsw
  # target is already static here (BUILD_SHARED_LIBS is OFF), so nothing
  # links the second one.
  "BUILD_STATIC_LIBS OFF"
)
if(efsw_ADDED)
  target_compile_definitions(efsw PRIVATE EFSW_FSEVENTS_NOT_SUPPORTED)
endif()

# The variar/oneTBB fork's compiled code is not C++23-compatible on MSVC,
# causing heap corruption (STATUS_HEAP_CORRUPTION 0xc0000374) in TBB flow
# graph operations during parallel search. Build with C++17 like vectorscan.
set(_SAVED_CXX_STANDARD_TBB ${CMAKE_CXX_STANDARD})
set(CMAKE_CXX_STANDARD 17)

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

set(CMAKE_CXX_STANDARD ${_SAVED_CXX_STANDARD_TBB})

# TBB's usage requirements without its library. A target whose own sources
# include a TBB header, but that already gets the TBB library itself through
# another library it links, uses this instead of tbb: naming tbb again would
# put the archive on the link line a second time, which Apple's linker warns
# about since Xcode 15 (#450). $<COMPILE_ONLY:tbb> would say the same in one
# line but needs CMake 3.27, and the project requires 3.16.
#
# A TBB found on the build host (CPM_USE_LOCAL_PACKAGES, which the Arch
# PKGBUILD and the Gentoo ebuild both set) is an imported TBB::tbb and no
# target named tbb, and $<TARGET_PROPERTY:tbb,...> would be a generate-time
# error there rather than the -ltbb the plain name falls back to.
add_library(logsquirl_tbb_headers INTERFACE)
if(TARGET tbb)
  target_include_directories(
    logsquirl_tbb_headers SYSTEM INTERFACE "$<TARGET_PROPERTY:tbb,INTERFACE_INCLUDE_DIRECTORIES>"
  )
  target_compile_definitions(
    logsquirl_tbb_headers INTERFACE "$<TARGET_PROPERTY:tbb,INTERFACE_COMPILE_DEFINITIONS>"
  )
elseif(TARGET TBB::tbb)
  target_link_libraries(logsquirl_tbb_headers INTERFACE TBB::tbb)
endif()

set(CPM_USE_LOCAL_PACKAGES OFF)
# Windows has no build-time equivalent of Linux's ELF symbol interposition
# (see CMakeLists.txt for why, and for why macOS's own dynamic-library
# equivalent is refused instead of wired here), so its override is a shared
# mimalloc built alongside the static one, plus mimalloc-redirect.dll (#282).
if(LOGSQUIRL_MIMALLOC_OVERRIDE AND WIN32)
  set(_LOGSQUIRL_MIMALLOC_BUILD_SHARED ON)
else()
  set(_LOGSQUIRL_MIMALLOC_BUILD_SHARED OFF)
endif()
cpmaddpackage(
  NAME
  mimalloc
  GITHUB_REPOSITORY
  microsoft/mimalloc
  VERSION
  2.5.2
  GIT_TAG
  2406cc5f3ab6d6e54fe560cdd66fa46eee775312 # v2.5.2
  EXCLUDE_FROM_ALL
  YES
  OPTIONS
  "MI_BUILD_TESTS OFF"
  "MI_SECURE OFF"
  "MI_BUILD_SHARED ${_LOGSQUIRL_MIMALLOC_BUILD_SHARED}"
  "MI_BUILD_STATIC ON"
  "MI_BUILD_OBJECT OFF"
  "MI_OVERRIDE ${LOGSQUIRL_MIMALLOC_OVERRIDE}"
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
# Whatever links mimalloc initializes it at process start and reports its
# memory telemetry with crash reports (#282).
target_compile_definitions(logsquirl_mimalloc_wrapper INTERFACE LOGSQUIRL_USE_MIMALLOC)
if(LOGSQUIRL_MIMALLOC_OVERRIDE)
  target_compile_definitions(logsquirl_mimalloc_wrapper INTERFACE LOGSQUIRL_MIMALLOC_OVERRIDE)
endif()

if(_LOGSQUIRL_MIMALLOC_BUILD_SHARED AND mimalloc_ADDED)
  # `mimalloc` (the shared library) is otherwise EXCLUDE_FROM_ALL, like the
  # rest of this fetched dependency, and nothing links against it — only
  # mimalloc-static above does. Force it into the default build so
  # `cmake --build .` produces it for the Windows process-wide override
  # measurement (mimalloc-redirect.dll; see CMakeLists.txt). Its build output
  # lands in the project's shared output directory ($<TARGET_FILE:mimalloc>,
  # same as every other shared library this project builds) rather than being
  # explicitly copied — nothing in the shipped application links against it,
  # so only the override measurement itself needs to find it there.
  set_target_properties(mimalloc PROPERTIES EXCLUDE_FROM_ALL FALSE)
  if(WIN32 AND TARGET mimalloc-redirect)
    set_target_properties(mimalloc-redirect PROPERTIES EXCLUDE_FROM_ALL FALSE)
  endif()
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

# Every target the packages above defined, in this directory and below it: no
# warnings, and its headers are system headers where LogSquirl includes them
# (#452). This used to be a hand-written list of target names that only did the
# second half, and had gone stale in both directions -- it still named
# SingleApplication and FastPFor, which no package defines any more, and did
# not name roaring-headers, which src/logdata links.
logsquirl_third_party_build_quietly("${CMAKE_CURRENT_SOURCE_DIR}")


