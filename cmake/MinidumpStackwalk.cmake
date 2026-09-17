# minidump-stackwalk of rust-minidump, the tool the crash handler runs on a
# pending crash report to show the user what it contains. The build ships it
# next to the app as logsquirl_minidump_dump(.exe) (#318).
#
# The release archive of the pinned version is downloaded and checked against
# its SHA-256 at configure time, like the pinned release tooling of the CI
# (#200). Renovate bumps the version below; the Renovate Checksums workflow
# (.github/scripts/update-checksums.py) refreshes the hashes. The release SBOM
# (scripts/sbom/logsquirl_sbom.py) reads the version from this file.
#
# -DLOGSQUIRL_MINIDUMP_STACKWALK=<path> uses an existing executable instead,
# e.g. a distribution's package or an offline build.

# renovate: datasource=github-releases depName=rust-minidump/rust-minidump
set(MINIDUMP_STACKWALK_VERSION "0.27.0")
set(MINIDUMP_STACKWALK_SHA256_MACOS_ARM64 "5fcd4d25a5b0bbdc8faac6f4f81629bc67483c3eb458b86a8304733a7c6a2ca9")
set(MINIDUMP_STACKWALK_SHA256_MACOS_X64 "801fa500d159fe474260ef1aad80706bed822434aced7fdb21c1b2c7a06afe6f")
set(MINIDUMP_STACKWALK_SHA256_LINUX_X64 "bfaf97804965c87155b91765f4042440a38bb002f20a62fc4da9b6b6bcb20faf")
set(MINIDUMP_STACKWALK_SHA256_WINDOWS_X64 "f6f2d7f1665843c4a270cd13fcd1458fed3b19013ea5dd909e12bc3599b959f4")

set(LOGSQUIRL_MINIDUMP_STACKWALK
    ""
    CACHE FILEPATH "minidump-stackwalk executable to ship; empty downloads the pinned release"
)

# Sets out_var to the minidump-stackwalk executable for the target platform, or
# to an empty string where rust-minidump publishes no build for it.
function(logsquirl_minidump_stackwalk out_var)
  if(LOGSQUIRL_MINIDUMP_STACKWALK)
    if(NOT EXISTS "${LOGSQUIRL_MINIDUMP_STACKWALK}")
      message(FATAL_ERROR "LOGSQUIRL_MINIDUMP_STACKWALK=${LOGSQUIRL_MINIDUMP_STACKWALK} does not exist")
    endif()
    set(${out_var} "${LOGSQUIRL_MINIDUMP_STACKWALK}" PARENT_SCOPE)
    return()
  endif()

  if(WIN32)
    if(MSVC)
      set(_arch "${MSVC_CXX_ARCHITECTURE_ID}")
    else()
      set(_arch "${CMAKE_SYSTEM_PROCESSOR}")
    endif()
  elseif(APPLE AND CMAKE_OSX_ARCHITECTURES)
    set(_arch "${CMAKE_OSX_ARCHITECTURES}")
  else()
    set(_arch "${CMAKE_SYSTEM_PROCESSOR}")
  endif()
  string(TOLOWER "${_arch}" _arch)

  set(_exe "minidump-stackwalk")
  if(WIN32 AND _arch MATCHES "^(x64|amd64|x86_64)$" AND CMAKE_SIZEOF_VOID_P EQUAL 8)
    set(_triple "x86_64-pc-windows-msvc")
    set(_ext "zip")
    set(_exe "minidump-stackwalk.exe")
    set(_sha256 "${MINIDUMP_STACKWALK_SHA256_WINDOWS_X64}")
  elseif(APPLE AND _arch STREQUAL "arm64")
    set(_triple "aarch64-apple-darwin")
    set(_ext "tar.xz")
    set(_sha256 "${MINIDUMP_STACKWALK_SHA256_MACOS_ARM64}")
  elseif(APPLE AND _arch STREQUAL "x86_64")
    set(_triple "x86_64-apple-darwin")
    set(_ext "tar.xz")
    set(_sha256 "${MINIDUMP_STACKWALK_SHA256_MACOS_X64}")
  elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux" AND _arch MATCHES "^(x86_64|amd64)$")
    # The static musl build runs on every distribution the packages target.
    set(_triple "x86_64-unknown-linux-musl")
    set(_ext "tar.xz")
    set(_sha256 "${MINIDUMP_STACKWALK_SHA256_LINUX_X64}")
  else()
    message(WARNING "rust-minidump publishes no minidump-stackwalk for ${CMAKE_SYSTEM_NAME} ${_arch}; "
                    "the crash report dialog shows no stack trace. Set LOGSQUIRL_MINIDUMP_STACKWALK to ship one.")
    set(${out_var} "" PARENT_SCOPE)
    return()
  endif()

  set(_name "minidump-stackwalk-${_triple}")
  set(_url "https://github.com/rust-minidump/rust-minidump/releases/download/v${MINIDUMP_STACKWALK_VERSION}/${_name}.${_ext}")
  if(CPM_SOURCE_CACHE)
    set(_archive_dir "${CPM_SOURCE_CACHE}/minidump-stackwalk/${MINIDUMP_STACKWALK_VERSION}")
  else()
    set(_archive_dir "${CMAKE_BINARY_DIR}/_deps/minidump-stackwalk/${MINIDUMP_STACKWALK_VERSION}")
  endif()
  set(_archive "${_archive_dir}/${_name}.${_ext}")

  # A cached archive is hashed again on every configure; file(DOWNLOAD) skips
  # the download when the file already matches EXPECTED_HASH.
  foreach(_attempt 1 2 3)
    file(
      DOWNLOAD "${_url}" "${_archive}"
      EXPECTED_HASH SHA256=${_sha256}
      TLS_VERIFY ON
      STATUS _status
    )
    list(GET _status 0 _code)
    if(_code EQUAL 0)
      break()
    endif()
    file(REMOVE "${_archive}")
    message(STATUS "Downloading ${_url} failed (attempt ${_attempt}): ${_status}")
  endforeach()
  if(NOT _code EQUAL 0)
    message(FATAL_ERROR "Could not download ${_url} with SHA-256 ${_sha256}: ${_status}")
  endif()

  set(_extract_dir "${CMAKE_BINARY_DIR}/_deps/minidump-stackwalk/${MINIDUMP_STACKWALK_VERSION}-${_triple}")
  set(_tool "${_extract_dir}/${_name}/${_exe}")
  set(_stamp "${_extract_dir}/sha256.txt")
  if(EXISTS "${_stamp}")
    file(READ "${_stamp}" _extracted_sha256)
  else()
    set(_extracted_sha256 "")
  endif()
  if(NOT EXISTS "${_tool}" OR NOT _extracted_sha256 STREQUAL _sha256)
    file(REMOVE_RECURSE "${_extract_dir}")
    file(MAKE_DIRECTORY "${_extract_dir}")
    execute_process(
      COMMAND "${CMAKE_COMMAND}" -E tar xf "${_archive}"
      WORKING_DIRECTORY "${_extract_dir}"
      RESULT_VARIABLE _result
    )
    if(NOT _result EQUAL 0 OR NOT EXISTS "${_tool}")
      message(FATAL_ERROR "Extracting ${_archive} did not produce ${_name}/${_exe}")
    endif()
    file(WRITE "${_stamp}" "${_sha256}")
  endif()

  message(STATUS "minidump-stackwalk ${MINIDUMP_STACKWALK_VERSION} (${_triple}): ${_tool}")
  set(${out_var} "${_tool}" PARENT_SCOPE)
endfunction()
