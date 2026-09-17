# Set a default build type if none was specified
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  message(STATUS "Setting build type to 'RelWithDebInfo' as none was specified.")
  set(CMAKE_BUILD_TYPE
      RelWithDebInfo
      CACHE STRING "Choose the type of build." FORCE
  )
  # Set the possible values of build type for cmake-gui, ccmake
  set_property(
    CACHE CMAKE_BUILD_TYPE
    PROPERTY STRINGS
             "Debug"
             "Release"
             "MinSizeRel"
             "RelWithDebInfo"
  )
endif()

# Generate compile_commands.json to make it easier to work with clang based
# tools
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# organize targets into folders for IDE
set_property(GLOBAL PROPERTY USE_FOLDERS ON)

# RelWithDebInfo, the build type of the releases for Windows and Linux,
# optimizes as fully as Release and keeps its debug information (#280). The
# flags apply to every target, the third-party libraries linked into the
# binaries included. Link time optimization is set in the top-level
# CMakeLists.txt (LOGSQUIRL_USE_LTO).
include(FullOptimization)
foreach(lang C CXX)
  logsquirl_full_optimization_flags(
    CMAKE_${lang}_FLAGS_RELWITHDEBINFO
    KIND COMPILE
    MSVC "${MSVC}"
    FLAGS "${CMAKE_${lang}_FLAGS_RELWITHDEBINFO}"
  )
endforeach()
foreach(kind EXE SHARED MODULE)
  logsquirl_full_optimization_flags(
    CMAKE_${kind}_LINKER_FLAGS_RELWITHDEBINFO
    KIND LINK
    MSVC "${MSVC}"
    FLAGS "${CMAKE_${kind}_LINKER_FLAGS_RELWITHDEBINFO}"
  )
endforeach()
