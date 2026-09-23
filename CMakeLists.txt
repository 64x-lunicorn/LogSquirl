# 3.16, not less: from that minimum on, policy CMP0096 defaults to NEW and
# project() keeps a version component's leading zero, so a build without a
# version from the environment calls itself 26.07.0, the way this file spells
# it, instead of 26.7.0 (#372).
cmake_minimum_required(VERSION 3.16)

# Allow old third-party deps (debug_assert, ucm, …) that call
# cmake_minimum_required(VERSION <3.5) to configure under CMake 4.x.
if(CMAKE_VERSION VERSION_GREATER_EQUAL 4.0)
  set(CMAKE_POLICY_VERSION_MINIMUM 3.5)
endif()

project(
  logsquirl
  VERSION 26.10.0
  DESCRIPTION "logsquirl log viewer"
  LANGUAGES C CXX ASM
)

set(PROJECT_HOMEPAGE_URL "https://github.com/64x-lunicorn/LogSquirl")

string(TIMESTAMP PRODUCT_CURRENT_YEAR "%Y")
set(COMPANY "LogSquirl Contributors")
set(COPYRIGHT "Copyright (c) ${PRODUCT_CURRENT_YEAR} LogSquirl Contributors. Based on klogg by Anton Filimonov and glogg by Nicolas Bonnefon.")
set(IDENTIFIER "io.github.logsquirl")
set(PROJECT_DESCRIPTION "${PROJECT_NAME} log viewer")

option(LOGSQUIRL_BUILD_TESTS "Build tests" ON)
option(LOGSQUIRL_USE_LTO "Use link time optimization" ON)
option(LOGSQUIRL_USE_SENTRY "Use Sentry" OFF)
option(LOGSQUIRL_GENERIC_CPU "Build for generic CPU" OFF)
option(LOGSQUIRL_OSX_DEPLOYMENT_TARGET "Override target MacOS version" "")
# Off: only LogSquirl's own containers, roaring and Vectorscan allocate through
# mimalloc. On: mimalloc serves malloc and operator new for the whole process,
# Qt included. Off until A/B measurements decide it per platform (#282).
#
# The mechanism differs per platform, because only an ELF executable's malloc
# interposes its shared libraries' calls the same way:
# - Linux: a statically linked mimalloc (MI_OVERRIDE) interposes libc's malloc
#   for the whole process, Qt included, with no further wiring. Measure with
#   `benchmarks.yml` (see BUILD.md); a decision is pending.
# - macOS: measured for #282, twice, both broken:
#   1. Static interposition (MI_OVERRIDE with MI_OSX_ZONE/MI_OSX_INTERPOSE,
#      mimalloc's normal macOS mechanism) does not reach Qt: a statically
#      linked, overridden mimalloc served logsquirl_tests' own std::malloc
#      calls but not QByteArray's, which stayed on the system allocator
#      (`logsquirl_tests "[allocator]"` fails that check). Confirmed again
#      here the same way.
#   2. mimalloc's other documented macOS override, a dynamic library preloaded
#      ahead of the system allocator
#      (https://github.com/microsoft/mimalloc#environment-options,
#      `DYLD_INSERT_LIBRARIES=<path>/libmimalloc.dylib`), was built and tried
#      here against logsquirl_tests and logsquirl_logdata_read_benchmark: both
#      crash on startup with `objc[...]: realized class ... has corrupt data
#      pointer` — the Objective-C runtime's own metadata, allocated before
#      mimalloc's zone took over, gets freed or resized through it afterwards.
#      A non-AppKit binary (`logsquirl_grep`) did not show this crash.
#   Decision: off on macOS, and configuring with it on fails here rather than
#   producing a build that starts up broken — a GUI build always loads AppKit.
# - Windows: mimalloc's documented override links `mimalloc.lib` (the shared
#   import library, built when MI_BUILD_SHARED and MI_OVERRIDE are both on)
#   and ships `mimalloc-redirect.dll` next to the executable, which patches
#   ucrtbase's allocator at load time
#   (https://github.com/microsoft/mimalloc#override-on-windows). Wired below
#   but not built or measured here (no Windows machine in this environment) —
#   verify in CI before turning it on there.
option(LOGSQUIRL_MIMALLOC_OVERRIDE "Let mimalloc serve malloc and new for the whole process" OFF)
if(LOGSQUIRL_MIMALLOC_OVERRIDE AND APPLE)
  message(
    FATAL_ERROR
    "LOGSQUIRL_MIMALLOC_OVERRIDE crashes on macOS (see the comment above) and is not supported there"
  )
endif()

if(CMAKE_SIZEOF_VOID_P EQUAL 4)
  set(LOGSQUIRL_ARCH 32)
else()
  set(LOGSQUIRL_ARCH 64)
endif()

if(LOGSQUIRL_ARCH EQUAL 64)
  option(LOGSQUIRL_USE_VECTORSCAN "Use Vectorscan" ON)
else()
  option(LOGSQUIRL_USE_VECTORSCAN "Use Vectorscan" OFF)
endif()

set(BUILD_VERSION
    $ENV{LOGSQUIRL_VERSION}
    CACHE STRING "build version"
)
set(BUILD_NUMBER
    ""
    CACHE STRING "build number"
)

set(NOTICE_FILE ${CMAKE_CURRENT_SOURCE_DIR}/NOTICE)
set(COPYING_FILE ${CMAKE_CURRENT_SOURCE_DIR}/COPYING)

set(README_FILE ${CMAKE_CURRENT_SOURCE_DIR}/README.md)

set(DOCUMENTATION_FILE ${CMAKE_CURRENT_SOURCE_DIR}/DOCUMENTATION.md)
set(DOCUMENTATION_HTML ${CMAKE_BINARY_DIR}/generated/documentation.html)
set(DOCUMENTATION_RESOURCE ${CMAKE_BINARY_DIR}/generated/documentation.qrc)

if(WIN32)
  set(ICON_NAME logsquirl.ico)
elseif(APPLE)
  set(ICON_NAME logsquirl.icns)
endif()

if(NOT
   ${ICON_NAME}
   STREQUAL
   ""
)
  set(ICON_FILE ${CMAKE_CURRENT_SOURCE_DIR}/Resources/${ICON_NAME})
endif()

if(MSVC AND CMAKE_CXX_COMPILER_VERSION VERSION_GREATER_EQUAL 19.14)
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /experimental:external /external:W0 ")
  set(CMAKE_INCLUDE_SYSTEM_FLAG_CXX "/external:I ")
endif()

set(out_dir "${CMAKE_BINARY_DIR}/output")
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${out_dir}")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${out_dir}")
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${out_dir}")

set(CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/cmake")

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
#set(CMAKE_CXX_EXTENSIONS OFF)

include(StandardProjectSettings)
include(ucm)
include(prepare_version)
include(FindPkgConfig)
include(GNUInstallDirs)

if(MSVC)
  if(CMAKE_CXX_FLAGS MATCHES "/W[0-4]")
    string(
      REGEX
      REPLACE "/W[0-4]"
              "/W4"
              CMAKE_CXX_FLAGS
              "${CMAKE_CXX_FLAGS}"
    )
  else(CMAKE_CXX_FLAGS MATCHES "/W[0-4]")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /W4")
  endif(CMAKE_CXX_FLAGS MATCHES "/W[0-4]")

  if(CMAKE_C_FLAGS MATCHES "/W[0-4]")
    string(
      REGEX
      REPLACE "/W[0-4]"
              "/W4"
              CMAKE_C_FLAGS
              "${CMAKE_C_FLAGS}"
    )
  else(CMAKE_C_FLAGS MATCHES "/W[0-4]")
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} /W4")
  endif(CMAKE_C_FLAGS MATCHES "/W[0-4]")

  # Chosen for the architecture the compiler generates code for, so an ARM64
  # build gets no x86 flags (#285).
  include(ArchitectureFlags)
  if(CMAKE_CXX_COMPILER_ARCHITECTURE_ID)
    set(LOGSQUIRL_TARGET_PROCESSOR "${CMAKE_CXX_COMPILER_ARCHITECTURE_ID}")
  else()
    set(LOGSQUIRL_TARGET_PROCESSOR "${CMAKE_SYSTEM_PROCESSOR}")
  endif()
  logsquirl_architecture_flags(
    LOGSQUIRL_ARCHITECTURE_FLAGS
    MSVC
    PROCESSOR "${LOGSQUIRL_TARGET_PROCESSOR}"
    SYSTEM_NAME "${CMAKE_SYSTEM_NAME}"
    GENERIC_CPU "${LOGSQUIRL_GENERIC_CPU}"
  )
  message("Architecture flags for ${CMAKE_SYSTEM_NAME} ${LOGSQUIRL_TARGET_PROCESSOR}: ${LOGSQUIRL_ARCHITECTURE_FLAGS}")
  ucm_add_flags(C CXX ${LOGSQUIRL_ARCHITECTURE_FLAGS})

  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /bigobj ")
else()
  ucm_add_flags(CXX "-fno-sized-deallocation -fno-omit-frame-pointer")

  # Chosen for the target architecture, not the host (#285).
  include(ArchitectureFlags)
  logsquirl_architecture_flags(
    LOGSQUIRL_ARCHITECTURE_FLAGS
    PROCESSOR "${CMAKE_SYSTEM_PROCESSOR}"
    SYSTEM_NAME "${CMAKE_SYSTEM_NAME}"
    OSX_ARCHITECTURES ${CMAKE_OSX_ARCHITECTURES}
    GENERIC_CPU "${LOGSQUIRL_GENERIC_CPU}"
  )
  message("Architecture flags for ${CMAKE_SYSTEM_NAME} ${CMAKE_SYSTEM_PROCESSOR}: ${LOGSQUIRL_ARCHITECTURE_FLAGS}")
  ucm_add_flags(C CXX ${LOGSQUIRL_ARCHITECTURE_FLAGS})
endif(MSVC)

if(LOGSQUIRL_USE_SENTRY)
  include(MinidumpStackwalk)
  logsquirl_minidump_stackwalk(LOGSQUIRL_MINIDUMP_TOOL)
endif()

# standard compiler warnings
# Link this 'library' to use the warnings specified in CompilerWarnings.cmake
include(CompilerWarnings)
add_library(project_warnings INTERFACE)
set_project_warnings(project_warnings)

# Link this 'library' to set the c++ standard / compile-time options requested
include(CompilerFlags)
add_library(project_options INTERFACE)
set_project_compile_flags(project_options)

# sanitizer options if supported by compiler
include(Sanitizers)
enable_sanitizers(project_options)

# allow for static analysis options
include(StaticAnalyzers)

ucm_print_flags()

if(APPLE)
  if(CMAKE_HOST_SYSTEM_PROCESSOR MATCHES "arm64")
    message("Adding missing Threads workaround")
    set(CMAKE_THREAD_LIBS_INIT "-lpthread")
    set(CMAKE_HAVE_THREADS_LIBRARY 1)
    set(CMAKE_USE_WIN32_THREADS_INIT 0)
    set(CMAKE_USE_PTHREADS_INIT 1)
    set(THREADS_PREFER_PTHREAD_FLAG ON)
  endif()
endif()

find_package(
  Qt6
  COMPONENTS Core
             Widgets
             Concurrent
             Network
             Xml
  REQUIRED
)

set(LOGSQUIRL_QT_VERSION ${Qt6Core_VERSION})

message("Qt version ${LOGSQUIRL_QT_VERSION}")
message("LTO ${LOGSQUIRL_USE_LTO}")

if(APPLE)
  if(${LOGSQUIRL_OSX_DEPLOYMENT_TARGET})
    set(CMAKE_OSX_DEPLOYMENT_TARGET ${LOGSQUIRL_OSX_DEPLOYMENT_TARGET})
    message("OSX target ${CMAKE_OSX_DEPLOYMENT_TARGET}")
  endif()
endif()

set(CMAKE_AUTOMOC OFF)
set(CMAKE_INCLUDE_CURRENT_DIR OFF)
set(BUILD_SHARED_LIBS OFF)

add_custom_target(ci_build)

add_subdirectory(3rdparty)

# Link time optimization covers every target of LogSquirl itself, the
# libraries, the applications, the tests and the benchmarks, and nothing else
# (#280). Set here, after the third-party libraries were added, it does not
# reach their targets, whose own build systems decide.
if(LOGSQUIRL_USE_LTO)
  include(CheckIPOSupported)
  check_ipo_supported(RESULT LOGSQUIRL_IPO_SUPPORTED OUTPUT LOGSQUIRL_IPO_OUTPUT LANGUAGES C CXX)
  if(NOT LOGSQUIRL_IPO_SUPPORTED)
    message(SEND_ERROR "Link time optimization is not supported: ${LOGSQUIRL_IPO_OUTPUT}\n"
                       "Configure with -DLOGSQUIRL_USE_LTO=OFF to build without it."
    )
  endif()
  set(CMAKE_INTERPROCEDURAL_OPTIMIZATION ON)
endif()

add_subdirectory(src)
add_subdirectory(website)

enable_testing()

# The application starts and prints its version. It runs with its own home,
# temporary directory and QStandardPaths locations, so ctest on a developer
# machine cannot read or change that developer's own settings, Session, cache,
# Log Formats or plugins (#328).
add_test(
    NAME logsquirl_smoke
    COMMAND ${CMAKE_COMMAND}
            -DLOGSQUIRL=$<TARGET_FILE:logsquirl>
            -DWORK_DIR=${CMAKE_CURRENT_BINARY_DIR}/logsquirl_smoke
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/logsquirl_smoke.cmake
)

if(${LOGSQUIRL_BUILD_TESTS})
  add_subdirectory(tests)
endif()

# Installation and packaging
set(CMAKE_SKIP_INSTALL_ALL_DEPENDENCY true)
set(CPACK_SOURCE_IGNORE_FILES "build_root;.git")

set(CPACK_OUTPUT_FILE_PREFIX "packages")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY ${PROJECT_DESCRIPTION})
set(CPACK_PACKAGE_VENDOR ${COMPANY})
set(CPACK_PACKAGE_CONTACT "anton.filimonov@gmail.com")
set(CPACK_PACKAGE_DESCRIPTION_FILE "${CMAKE_CURRENT_SOURCE_DIR}/packaging/description.txt")
set(CPACK_RESOURCE_FILE_LICENSE "${COPYING_FILE}")
set(CPACK_PACKAGE_VERSION
    "${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}.${PROJECT_VERSION_PATCH}.${PROJECT_VERSION_TWEAK}"
)
set(CPACK_PACKAGE_ICON "${ICON_FILE}")

if(UNIX)
  include(InstallRequiredSystemLibraries)

  # The command line tool ships in the packages beside the application
  # (src/app/CMakeLists.txt), and is stripped there the way it is (#430).
  set(CPACK_STRIP_FILES "bin/logsquirl" "bin/logsquirl_grep")

  if(APPLE)
    set(CPACK_GENERATOR "DragNDrop")
    set(CPACK_DMG_FORMAT "UDBZ")
    set(CPACK_DMG_VOLUME_NAME "${PROJECT_NAME}")
    set(CPACK_SYSTEM_NAME "OSX")
    set(CPACK_DMG_DS_STORE "${CMAKE_CURRENT_SOURCE_DIR}/packaging/osx/DS_Store")

  else()
    install(FILES ${CMAKE_CURRENT_SOURCE_DIR}/src/app/images/hicolor/16x16/logsquirl.png
            DESTINATION share/icons/hicolor/16x16/apps
    )

    install(FILES ${CMAKE_CURRENT_SOURCE_DIR}/src/app/images/hicolor/32x32/logsquirl.png
            DESTINATION share/icons/hicolor/32x32/apps
    )

    install(FILES ${CMAKE_CURRENT_SOURCE_DIR}/src/app/images/hicolor/48x48/logsquirl.png
            DESTINATION share/icons/hicolor/48x48/apps
    )

    install(FILES ${CMAKE_CURRENT_SOURCE_DIR}/src/app/images/hicolor/scalable/logsquirl.svg
            DESTINATION share/icons/hicolor/scalable/apps
    )

    install(
      FILES ${COPYING_FILE}
            ${NOTICE_FILE}
            ${README_FILE}
            ${DOCUMENTATION_HTML}
            ${DOCUMENTATION_FILE}
      DESTINATION ${CMAKE_INSTALL_DOCDIR}
    )

    install(FILES ${CMAKE_CURRENT_SOURCE_DIR}/packaging/linux/logsquirl.desktop DESTINATION share/applications)

    set(CPACK_GENERATOR DEB)

    #deb
    set(CPACK_DEBIAN_PACKAGE_ARCHITECTURE "amd64")
    set(CPACK_DEBIAN_PACKAGE_SECTION "utils")
    set(CPACK_DEBIAN_PACKAGE_HOMEPAGE "https://github.com/64x-lunicorn/LogSquirl")
    set(CPACK_DEBIAN_PACKAGE_MAINTAINER "LogSquirl Contributors <${CPACK_PACKAGE_CONTACT}>")
    # Qt is installed via aqtinstall (not distro packages), so auto-dependency
    # detection would find no package owning libQt6Core.so.6 and fail.
    set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS NO)

    # The packages do not bundle Qt, but the binary needs at least the Qt it was
    # built against (it references the Qt_6.<minor> symbol versions). So they
    # declare the distribution's Qt packages with that version as the minimum:
    # on a distribution with an older Qt the install fails, instead of the
    # start. Users of such a distribution take the AppImage (#226).
    # Core, Network, Concurrent, Xml; Gui, Widgets; the xcb platform plugin;
    # the SVG image format and icon engine plugins.
    set(LOGSQUIRL_QT_MIN "${LOGSQUIRL_QT_VERSION}")
    set(_deb_qt_depends)
    foreach(_lib core gui widgets network concurrent xml)
      # Ubuntu 24.04 and Debian 13 renamed the libraries for the 64-bit time_t transition.
      list(APPEND _deb_qt_depends "libqt6${_lib}6t64 (>= ${LOGSQUIRL_QT_MIN}) | libqt6${_lib}6 (>= ${LOGSQUIRL_QT_MIN})")
    endforeach()
    foreach(_pkg libqt6svg6 qt6-qpa-plugins)
      list(APPEND _deb_qt_depends "${_pkg} (>= ${LOGSQUIRL_QT_MIN})")
    endforeach()
    list(JOIN _deb_qt_depends ", " CPACK_DEBIAN_PACKAGE_DEPENDS)

    #RPM
    set(CPACK_RPM_PACKAGE_ARCHITECTURE "x86_64")
    set(CPACK_RPM_PACKAGE_LICENSE "GPLv3")
    set(CPACK_RPM_PACKAGE_AUTOREQ NO)
    # Fedora and Oracle Linux package names; see the deb dependencies above (#226).
    set(_rpm_qt_requires)
    foreach(_pkg qt6-qtbase qt6-qtbase-gui qt6-qt5compat qt6-qtsvg)
      list(APPEND _rpm_qt_requires "${_pkg} >= ${LOGSQUIRL_QT_MIN}")
    endforeach()
    list(JOIN _rpm_qt_requires ", " CPACK_RPM_PACKAGE_REQUIRES)
    set(CPACK_RPM_PACKAGE_URL "https://github.com/64x-lunicorn/LogSquirl")
    set(CPACK_RPM_EXCLUDE_FROM_AUTO_FILELIST_ADDITION
        /usr/share/applications
        /usr/share/icons
        /usr/share/icons/hicolor
        /usr/share/icons/hicolor/16x16
        /usr/share/icons/hicolor/16x16/apps
        /usr/share/icons/hicolor/32x32
        /usr/share/icons/hicolor/32x32/apps
        /usr/share/icons/hicolor/scalable
        /usr/share/icons/hicolor/scalable/apps
    )
  endif()
  include(CPack)
endif()

# for IDE grouping
add_custom_target(
  installation
  SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/packaging/windows/logsquirl.nsi
          ${CMAKE_CURRENT_SOURCE_DIR}/packaging/linux/logsquirl.desktop
          ${CMAKE_CURRENT_SOURCE_DIR}/cmake/MacOSXBundleInfo.plist.in
)

add_custom_target(
  logsquirl_docs
  SOURCES ${README_FILE}
          ${COPYING_FILE}
          ${NOTICE_FILE}
          ${DOCUMENTATION_FILE}
)

add_custom_target(logsquirl_stuff SOURCES .clang-format .clang-tidy .cmake-format)
