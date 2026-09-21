# Registers every Catch2 test case of a test executable as its own CTest test,
# so one failing or hanging case turns only itself red and reports its own name
# (#217).
#
#   logsquirl_add_catch_tests(<target> TIMEOUT <seconds> [EXTRA_ARGS <arg>...])
#
# Each test is named "<target>: <test case name>". EXTRA_ARGS are passed both to
# the listing and to every test case (e.g. "-platform offscreen"); TIMEOUT is
# the timeout of each test case, not of the whole executable.
#
# Catch2 v2's own catch_discover_tests is not used: it lists the test cases in
# a POST_BUILD step, which runs every test executable during the build without
# its arguments (a headless container has no display without
# "-platform offscreen"), without the environment the tests run in (Qt DLLs on
# Windows, sanitizer options), and a listing that crashes registers no tests at
# all instead of a failing one. Here the listing runs when ctest reads its test
# list, in ctest's environment, with the tests' own arguments, and a listing
# that fails registers a failing test.

set(_LOGSQUIRL_CATCH_ADD_TESTS_SCRIPT "${CMAKE_CURRENT_LIST_DIR}/CatchTestDiscoveryAddTests.cmake")
# Each registered test case runs through this, beside a settings file of its own
# (#370).
set(_LOGSQUIRL_CATCH_RUN_TEST_SCRIPT "${CMAKE_CURRENT_LIST_DIR}/CatchTestDiscoveryRunTest.cmake")

function(logsquirl_add_catch_tests TARGET)
    cmake_parse_arguments(PARSE_ARGV 1 arg "" "TIMEOUT" "EXTRA_ARGS")
    if(NOT arg_TIMEOUT)
        message(FATAL_ERROR "logsquirl_add_catch_tests(${TARGET}): TIMEOUT is required")
    endif()

    # The executable's path depends on the configuration, so its parameters are
    # generated per configuration and ctest picks the one it runs.
    set(params_base "${CMAKE_CURRENT_BINARY_DIR}/${TARGET}_catch_tests")
    file(GENERATE
        OUTPUT "${params_base}-$<CONFIG>.cmake"
        CONTENT
"set(_logsquirl_catch_target [==[${TARGET}]==])
set(_logsquirl_catch_executable [==[$<TARGET_FILE:${TARGET}>]==])
set(_logsquirl_catch_extra_args [==[${arg_EXTRA_ARGS}]==])
set(_logsquirl_catch_timeout [==[${arg_TIMEOUT}]==])
set(_logsquirl_catch_working_dir [==[${CMAKE_CURRENT_BINARY_DIR}]==])
set(_logsquirl_catch_cmake [==[${CMAKE_COMMAND}]==])
set(_logsquirl_catch_run_script [==[${_LOGSQUIRL_CATCH_RUN_TEST_SCRIPT}]==])
include([==[${_LOGSQUIRL_CATCH_ADD_TESTS_SCRIPT}]==])
")

    get_property(multi_config GLOBAL PROPERTY GENERATOR_IS_MULTI_CONFIG)
    set(include_file "${params_base}_include.cmake")
    if(multi_config)
        file(WRITE "${include_file}"
"if(EXISTS [==[${params_base}-]==]\${CTEST_CONFIGURATION_TYPE}.cmake)
  include([==[${params_base}-]==]\${CTEST_CONFIGURATION_TYPE}.cmake)
else()
  add_test([==[${TARGET}: pass the configuration to ctest with -C]==] [==[${TARGET}_NO_CONFIGURATION]==])
endif()
")
    else()
        file(WRITE "${include_file}" "include([==[${params_base}-${CMAKE_BUILD_TYPE}.cmake]==])\n")
    endif()

    set_property(DIRECTORY APPEND PROPERTY TEST_INCLUDE_FILES "${include_file}")
endfunction()
