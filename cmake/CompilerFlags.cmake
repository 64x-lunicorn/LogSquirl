function(set_project_compile_flags project_name)

  # No -ffast-math or /fp:fast: they let the compiler assume there are no
  # NaNs or infinities and reorder floating point arithmetic, so chart values
  # and aggregations could differ between compilers and optimization levels.
  set(MSVC_FLAGS)
  set(MSVC_DEFINITIONS -DNOMINMAX)

  set(CLANG_FLAGS)

  set(CLANG_DEFINITIONS)

  set(APPLE_CLANG_FLAGS ${CLANG_FLAGS} -stdlib=libc++)
  set(APPLE_CLANG_DEFINITIONS ${CLANG_DEFINITIONS})

  set(GCC_FLAGS ${CLANG_FLAGS})
  set(GCC_DEFINITIONS ${CLANG_DEFINITIONS})

  if(MSVC)
    set(PROJECT_FLAGS ${MSVC_FLAGS})
    set(PROJECT_DEFINITIONS ${MSVC_DEFINITIONS})
  elseif(CMAKE_CXX_COMPILER_ID MATCHES "AppleClang")
    set(PROJECT_FLAGS ${APPLE_CLANG_FLAGS})
    set(PROJECT_DEFINITIONS ${APPLE_CLANG_DEFINITIONS})
  elseif(CMAKE_CXX_COMPILER_ID MATCHES ".*Clang")
    set(PROJECT_FLAGS ${CLANG_FLAGS})
    set(PROJECT_DEFINITIONS ${CLANG_DEFINITIONS})
  elseif(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    set(PROJECT_FLAGS ${GCC_FLAGS})
    set(PROJECT_DEFINITIONS ${GCC_DEFINITIONS})
  else()
    message(AUTHOR_WARNING "No compiler flags set for '${CMAKE_CXX_COMPILER_ID}' compiler.")
  endif()

  message("Flags set for '${CMAKE_CXX_COMPILER_ID}' compiler:${PROJECT_FLAGS}")
  message("Defines set for '${CMAKE_CXX_COMPILER_ID}' compiler:${PROJECT_DEFINITIONS}")

  target_compile_options(${project_name} INTERFACE ${PROJECT_FLAGS})
  target_compile_definitions(${project_name} INTERFACE ${PROJECT_DEFINITIONS})

  set(PROJECT_COMPILER_FEATURES cxx_std_23)
  target_compile_features(${project_name} INTERFACE ${PROJECT_COMPILER_FEATURES})

endfunction()
