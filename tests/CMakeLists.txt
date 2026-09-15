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

# The plugin layer builds without Qt Widgets: no file of it may include a Qt
# Widgets header, and the library may not link Qt Widgets (#175).
add_test(
    NAME plugins_no_qt_widgets
    COMMAND ${CMAKE_COMMAND}
            -DSOURCES_DIR=${CMAKE_SOURCE_DIR}/src/plugins
            "-DWIDGETS_INCLUDE_DIRS=$<JOIN:$<TARGET_PROPERTY:Qt6::Widgets,INTERFACE_INCLUDE_DIRECTORIES>,|>"
            "-DLINK_LIBRARIES=$<JOIN:$<TARGET_PROPERTY:logsquirl_plugins,LINK_LIBRARIES>,|>"
            -P ${CMAKE_CURRENT_SOURCE_DIR}/plugins_no_qt_widgets.cmake
)






