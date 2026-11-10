# The order of the steps that open a Log File is tested on its own: without
# the UI library, a window or the plugins (#949).
add_executable(logsquirl_opensteps_tests
    opensteps_tests_main.cpp
    openstep_test.cpp
)

target_link_libraries(logsquirl_opensteps_tests logsquirl_opensteps Catch2::Catch2)

logsquirl_add_catch_tests(logsquirl_opensteps_tests TIMEOUT 60)
