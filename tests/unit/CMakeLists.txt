# Add test cpp file
add_executable(logsquirl_tests
    chipmunkimporter_test.cpp
    configuration_test.cpp
    encodingdetector_test.cpp
    jwtdecoder_test.cpp
    linepositionarray_test.cpp
    linetypes_test.cpp
    patternmatcher_test.cpp
    regularexpression_test.cpp
    tests_main.cpp
)

target_link_libraries(logsquirl_tests logsquirl_ui logsquirl_utils logsquirl_logging logsquirl_settings Catch2 Qt${QT_VERSION_MAJOR}::Test)
set_target_properties(logsquirl_tests PROPERTIES AUTOMOC ON)

add_test(
    NAME logsquirl_tests
    COMMAND logsquirl_tests -platform offscreen
)