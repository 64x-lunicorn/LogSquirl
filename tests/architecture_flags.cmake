# Fails when the compiler flags chosen for a target architecture are wrong:
# when an arm64 build is not given a portable CPU with LOGSQUIRL_GENERIC_CPU,
# when Linux aarch64 is handed x86 SIMD flags, or when the flags follow the
# machine building instead of the machine the build is for (#285).
#
# Usage: cmake -DMODULE_DIR=<cmake/> -P architecture_flags.cmake

cmake_minimum_required(VERSION 3.16)

include(${MODULE_DIR}/ArchitectureFlags.cmake)

# expect(<expected flags> <argument>...)
function(expect expected)
  logsquirl_architecture_flags(_actual ${ARGN})
  if(NOT "${_actual}" STREQUAL "${expected}")
    message(SEND_ERROR "logsquirl_architecture_flags(${ARGN})\n  expected: '${expected}'\n  actual:   '${_actual}'")
  endif()
endfunction()

# Generic CPU: a portable baseline per target.
expect("-mcpu=apple-m1" PROCESSOR arm64 SYSTEM_NAME Darwin GENERIC_CPU ON)
expect("-march=armv8-a;-mtune=generic" PROCESSOR aarch64 SYSTEM_NAME Linux GENERIC_CPU ON)
expect("-march=armv8-a;-mtune=generic" PROCESSOR arm64 SYSTEM_NAME Linux GENERIC_CPU ON)
expect("-mmmx;-msse;-msse2;-msse3;-mssse3;-msse4.1;-msse4.2;-mpopcnt;-march=x86-64;-mtune=generic"
       PROCESSOR x86_64 SYSTEM_NAME Linux GENERIC_CPU ON)
expect("-mmmx;-msse;-msse2;-msse3;-mssse3;-msse4.1;-msse4.2;-mpopcnt;-march=x86-64;-mtune=generic"
       PROCESSOR AMD64 SYSTEM_NAME Windows GENERIC_CPU ON)

# Local builds: the CPU of the machine building.
expect("-march=native;-mtune=generic" PROCESSOR arm64 SYSTEM_NAME Darwin GENERIC_CPU OFF)
expect("-march=native;-mtune=generic" PROCESSOR aarch64 SYSTEM_NAME Linux GENERIC_CPU OFF)
expect("-mmmx;-msse;-msse2;-msse3;-mssse3;-msse4.1;-msse4.2;-mpopcnt;-march=native;-mtune=generic"
       PROCESSOR x86_64 SYSTEM_NAME Darwin GENERIC_CPU OFF)

# macOS builds for the architectures CMAKE_OSX_ARCHITECTURES names, whatever
# CMAKE_SYSTEM_PROCESSOR says.
expect("-mmmx;-msse;-msse2;-msse3;-mssse3;-msse4.1;-msse4.2;-mpopcnt;-march=x86-64;-mtune=generic"
       PROCESSOR arm64 SYSTEM_NAME Darwin OSX_ARCHITECTURES x86_64 GENERIC_CPU ON)
expect("-mcpu=apple-m1" PROCESSOR x86_64 SYSTEM_NAME Darwin OSX_ARCHITECTURES arm64 GENERIC_CPU ON)
# A universal binary has no single architecture to tune for.
expect("" PROCESSOR arm64 SYSTEM_NAME Darwin OSX_ARCHITECTURES "arm64;x86_64" GENERIC_CPU ON)

# An architecture with no known flags gets none rather than x86 ones.
expect("" PROCESSOR riscv64 SYSTEM_NAME Linux GENERIC_CPU ON)
