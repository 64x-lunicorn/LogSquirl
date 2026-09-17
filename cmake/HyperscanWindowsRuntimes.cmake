# Builds Hyperscan as a DLL for one instruction set of a Windows release (#281).
#
#   logsquirl_add_hyperscan_windows_runtime(<target>
#                                           SOURCE_DIR <hyperscan sources>
#                                           DLL_NAME <file name without .dll>
#                                           [EXTRA_FLAGS <compiler flag>...]
#                                           [IMPORT_LIBRARY_VAR <out_var>])
#
# Hyperscan's own CMake project cannot be added twice to one build (its target
# names would collide), so each build is an external project over the same
# sources: configured with this build's compiler, generator, build type and C
# and C++ flags plus EXTRA_FLAGS. The fork only configures as a static library,
# so the static library is linked into <DLL_NAME>.dll with the exports of
# Hyperscan's hs.def, in the directory the executables are built into. Both
# builds export the same functions. IMPORT_LIBRARY_VAR receives the import
# library of this build to link against.

include(ExternalProject)

function(logsquirl_add_hyperscan_windows_runtime TARGET)
  cmake_parse_arguments(ARG "" "SOURCE_DIR;DLL_NAME;IMPORT_LIBRARY_VAR" "EXTRA_FLAGS" ${ARGN})

  string(TOUPPER "${CMAKE_BUILD_TYPE}" config)
  list(JOIN ARG_EXTRA_FLAGS " " extra_flags)
  set(binary_dir "${CMAKE_CURRENT_BINARY_DIR}/${TARGET}")
  set(dll "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/${ARG_DLL_NAME}.dll")
  set(static_library "${binary_dir}/lib/hs.lib")
  set(import_library "${binary_dir}/${ARG_DLL_NAME}_import.lib")

  set(cache_args
      "-DCMAKE_BUILD_TYPE:STRING=${CMAKE_BUILD_TYPE}"
      "-DCMAKE_C_COMPILER:FILEPATH=${CMAKE_C_COMPILER}"
      "-DCMAKE_CXX_COMPILER:FILEPATH=${CMAKE_CXX_COMPILER}"
      "-DCMAKE_C_FLAGS:STRING=${CMAKE_C_FLAGS} ${extra_flags}"
      "-DCMAKE_CXX_FLAGS:STRING=${CMAKE_CXX_FLAGS} ${extra_flags}"
      "-DCMAKE_C_FLAGS_${config}:STRING=${CMAKE_C_FLAGS_${config}}"
      "-DCMAKE_CXX_FLAGS_${config}:STRING=${CMAKE_CXX_FLAGS_${config}}"
      "-DCMAKE_CXX_STANDARD:STRING=17"
      "-DCMAKE_POLICY_VERSION_MINIMUM:STRING=3.5"
      "-DBUILD_STATIC_LIBS:BOOL=ON"
      "-DBUILD_SHARED_LIBS:BOOL=OFF"
      "-DBUILD_AVX512:BOOL=OFF"
      "-DBUILD_AVX512VBMI:BOOL=OFF"
      "-DFAT_RUNTIME:BOOL=OFF"
  )
  # The compiler cache and the debug information format CI configures (see
  # .github/actions/agent-build), and a Boost given on the command line.
  foreach(
    var
    CMAKE_C_COMPILER_LAUNCHER
    CMAKE_CXX_COMPILER_LAUNCHER
    CMAKE_MSVC_DEBUG_INFORMATION_FORMAT
    CMAKE_POLICY_DEFAULT_CMP0141
    BOOST_ROOT
    Boost_INCLUDE_DIR
  )
    if(DEFINED ${var})
      list(APPEND cache_args "-D${var}:STRING=${${var}}")
    endif()
  endforeach()

  ExternalProject_Add(
    ${TARGET}
    SOURCE_DIR "${ARG_SOURCE_DIR}"
    BINARY_DIR "${binary_dir}"
    DOWNLOAD_COMMAND ""
    UPDATE_COMMAND ""
    CMAKE_GENERATOR "${CMAKE_GENERATOR}"
    CMAKE_CACHE_ARGS ${cache_args}
    BUILD_COMMAND "${CMAKE_COMMAND}" --build "${binary_dir}" --target hs
    COMMAND
      "${CMAKE_LINKER}" /nologo /DLL /DEBUG /INCREMENTAL:NO /OPT:REF /OPT:ICF "/DEF:${ARG_SOURCE_DIR}/hs.def"
      "/OUT:${binary_dir}/${ARG_DLL_NAME}.dll" "/IMPLIB:${import_library}" "${static_library}"
    INSTALL_COMMAND "${CMAKE_COMMAND}" -E copy "${binary_dir}/${ARG_DLL_NAME}.dll" "${binary_dir}/${ARG_DLL_NAME}.pdb"
                    "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}"
    BUILD_BYPRODUCTS "${static_library}" "${import_library}" "${binary_dir}/${ARG_DLL_NAME}.dll"
    INSTALL_BYPRODUCTS "${dll}"
    # One build at a time in the console pool, with its output visible.
    USES_TERMINAL_CONFIGURE YES
    USES_TERMINAL_BUILD YES
  )

  if(ARG_IMPORT_LIBRARY_VAR)
    set(${ARG_IMPORT_LIBRARY_VAR} "${import_library}" PARENT_SCOPE)
  endif()
endfunction()
