# libFuzzer targets for the code that reads bytes from anywhere before a user
# sees anything (#319). Off by default: they need Clang, and the sanitizers
# and the fuzzing engine come from the environment that runs them, ClusterFuzzLite
# in CI (.clusterfuzzlite/, .github/workflows/cflite.yml) or a developer's
# Clang (see BUILD.md). The whole build, libraries included, must be compiled
# with -fsanitize=fuzzer-no-link (ClusterFuzzLite does through CFLAGS);
# otherwise the fuzzers get no coverage feedback from anything but the harness.
set(LOGSQUIRL_FUZZ_ENGINE "-fsanitize=fuzzer" CACHE STRING
    "Linker flags that provide main() and the fuzzing engine to every fuzz target")

function(logsquirl_add_fuzzer name)
  add_executable(${name} ${ARGN})
  target_link_libraries(${name} PRIVATE project_options ${LOGSQUIRL_FUZZ_ENGINE})
  # LOGSQUIRL_FUZZ_ENGINE may be an archive rather than a flag; only the flag
  # form has the compiler instrumenting the harness itself.
  if(LOGSQUIRL_FUZZ_ENGINE STREQUAL "-fsanitize=fuzzer")
    target_compile_options(${name} PRIVATE -fsanitize=fuzzer-no-link)
  endif()
  add_dependencies(logsquirl_fuzzers ${name})
endfunction()

add_custom_target(logsquirl_fuzzers)

logsquirl_add_fuzzer(indexing_blocks_fuzzer indexing_blocks_fuzzer.cpp)
target_include_directories(indexing_blocks_fuzzer PRIVATE "${CMAKE_SOURCE_DIR}/src/logdata/src")
target_link_libraries(indexing_blocks_fuzzer PRIVATE logsquirl_logdata)

logsquirl_add_fuzzer(log_format_fuzzer log_format_fuzzer.cpp)
target_link_libraries(log_format_fuzzer PRIVATE logsquirl_logformat)

logsquirl_add_fuzzer(ansi_color_fuzzer ansi_color_fuzzer.cpp)
target_link_libraries(ansi_color_fuzzer PRIVATE logsquirl_logdata)
