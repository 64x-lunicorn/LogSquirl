# The Open Log File is tested on its own: it follows a Log File without the
# UI library, a widget or the settings store (#244).
add_executable(logsquirl_openlogfile_tests
    openlogfile_tests_main.cpp
    openlogfile_test.cpp
    loadrule_test.cpp
)

target_link_libraries(logsquirl_openlogfile_tests logsquirl_openlogfile Catch2::Catch2 Qt6::Test test_utils)
set_target_properties(logsquirl_openlogfile_tests PROPERTIES AUTOMOC ON)

logsquirl_add_catch_tests(logsquirl_openlogfile_tests TIMEOUT 120)
