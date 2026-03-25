find_package(Qt6 COMPONENTS Test REQUIRED)

add_subdirectory(helpers)
add_subdirectory(unit)
add_subdirectory(ui)

add_dependencies(logsquirl_itests file_write_helper)
add_dependencies(ci_build logsquirl_tests logsquirl_itests)






