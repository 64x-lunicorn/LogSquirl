# Add test cpp file
add_executable(logsquirl_tests
    linepositionarray_test.cpp
    patternmatcher_test.cpp
    tests_main.cpp
)

target_link_libraries(logsquirl_tests logsquirl_ui logsquirl_utils logsquirl_logging Catch2 Qt${QT_VERSION_MAJOR}::Test)
set_target_properties(logsquirl_tests PROPERTIES AUTOMOC ON)

add_test(
    NAME logsquirl_tests
    COMMAND logsquirl_tests -platform offscreen
)