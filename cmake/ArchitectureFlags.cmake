# Compiler flags that select the instruction set of the architecture a build
# is for (#285).
#
#   logsquirl_architecture_flags(<out_var>
#                                PROCESSOR <CMAKE_SYSTEM_PROCESSOR>
#                                SYSTEM_NAME <CMAKE_SYSTEM_NAME>
#                                [OSX_ARCHITECTURES <CMAKE_OSX_ARCHITECTURES>...]
#                                GENERIC_CPU <ON|OFF>
#                                [MSVC])
#
# Without MSVC the flags are GCC and Clang flags. With MSVC they are the
# compiler's: x86 and x64 builds get the SSE4 defines the code checks for, and
# /arch:AVX2 unless GENERIC_CPU; ARM64 gets no x86 flags. For MSVC pass the
# architecture the compiler generates code for
# (CMAKE_CXX_COMPILER_ARCHITECTURE_ID) as PROCESSOR.
#
# The flags follow the target, never the host: a cross build or a macOS build
# for another architecture gets the flags of the machine it runs on. On macOS
# CMAKE_OSX_ARCHITECTURES, when set, names that machine; a universal binary has
# no single architecture, so it gets no flags.
#
# With GENERIC_CPU the build runs on every CPU of the architecture that
# LogSquirl supports: x86-64 with SSE4.2, the Apple M1 on macOS arm64 and
# armv8-a on any other arm64. Without it the build targets the CPU compiling
# it. An architecture without known flags gets none.
function(logsquirl_architecture_flags out_var)
  cmake_parse_arguments(ARG "MSVC" "PROCESSOR;SYSTEM_NAME;GENERIC_CPU" "OSX_ARCHITECTURES" ${ARGN})

  set(processor "${ARG_PROCESSOR}")
  if(ARG_SYSTEM_NAME STREQUAL "Darwin" AND ARG_OSX_ARCHITECTURES)
    list(LENGTH ARG_OSX_ARCHITECTURES architecture_count)
    if(architecture_count EQUAL 1)
      set(processor "${ARG_OSX_ARCHITECTURES}")
    else()
      set(processor "")
    endif()
  endif()

  set(flags)
  if(ARG_MSVC)
    if(processor MATCHES "^(x86_64|X86_64|amd64|AMD64|x64|X64|i[3-6]86|x86|X86)$")
      set(flags /D__SSE4_1__=1 /D__SSE4_2__=1)
      if(NOT ARG_GENERIC_CPU)
        list(APPEND flags /arch:AVX2)
      endif()
    endif()
  elseif(processor MATCHES "^(arm64|ARM64|aarch64|AARCH64)$")
    if(NOT ARG_GENERIC_CPU)
      set(flags -march=native -mtune=generic)
    elseif(ARG_SYSTEM_NAME STREQUAL "Darwin")
      set(flags -mcpu=apple-m1)
    else()
      set(flags -march=armv8-a -mtune=generic)
    endif()
  elseif(processor MATCHES "^(x86_64|X86_64|amd64|AMD64|x64|i[3-6]86|x86)$")
    set(flags -mmmx -msse -msse2 -msse3 -mssse3 -msse4.1 -msse4.2 -mpopcnt)
    if(ARG_GENERIC_CPU)
      list(APPEND flags -march=x86-64 -mtune=generic)
    else()
      list(APPEND flags -march=native -mtune=generic)
    endif()
  endif()

  set(${out_var} "${flags}" PARENT_SCOPE)
endfunction()
