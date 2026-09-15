find_package(Qt6 COMPONENTS Test REQUIRED)

add_subdirectory(helpers)
add_subdirectory(unit)
add_subdirectory(ui)
add_subdirectory(benchmarks)

add_dependencies(logsquirl_itests file_write_helper)
add_dependencies(ci_build logsquirl_tests logsquirl_compression_tests logsquirl_itests logsquirl_decoration_benchmark)

# TBB is a private dependency of the log data library: no public header of
# it may include a TBB header (#168).
add_test(
    NAME logdata_public_headers_no_tbb
    COMMAND ${CMAKE_COMMAND}
            -DHEADERS_DIR=${CMAKE_SOURCE_DIR}/src/logdata/include
            -P ${CMAKE_CURRENT_SOURCE_DIR}/logdata_public_headers_no_tbb.cmake
)






