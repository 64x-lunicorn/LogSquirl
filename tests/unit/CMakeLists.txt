# Add test cpp file
add_executable(logsquirl_tests
    abstractlogview_test.cpp
    chartseries_test.cpp
    charttemplategenerator_test.cpp
    chipmunkimporter_test.cpp
    configuration_test.cpp
    encodingdetector_test.cpp
    fontutils_test.cpp
    indexcache_test.cpp
    jwtdecoder_test.cpp
    linedecorator_test.cpp
    linessaver_test.cpp
    linepositionarray_test.cpp
    linetypes_test.cpp
    formatrecognition_test.cpp
    logfieldextractor_test.cpp
    logformatcatalog_test.cpp
    logformatparser_test.cpp
    logformattablemodel_test.cpp
    logtablehighlightdelegate_test.cpp
    logtableview_test.cpp
    logviewpainting_test.cpp
    lz4device_test.cpp
    mergecontroller_test.cpp
    patternmatcher_test.cpp
    plugindialog_test.cpp
    pluginmanager_test.cpp
    pluginmetadata_test.cpp
    pluginrepository_test.cpp
    regularexpression_test.cpp
    regularexpressionpattern_test.cpp
    searchsession_test.cpp
    settingspolicies_test.cpp
    tableviewselection_test.cpp
    viewportlayout_test.cpp
    wrappedstring_test.cpp
    zstddevice_test.cpp
    tests_main.cpp
)

target_link_libraries(logsquirl_tests logsquirl_ui logsquirl_highlighting logsquirl_plugins logsquirl_logformat logsquirl_utils logsquirl_logging logsquirl_settings logsquirl_zstd logsquirl_lz4 Catch2 Qt6::Test test_utils)
set_target_properties(logsquirl_tests PROPERTIES AUTOMOC ON)

# The painting test reads its font and golden images from the source tree,
# and rewrites the golden images there when asked to (#135).
target_compile_definitions(logsquirl_tests PRIVATE
    LOGSQUIRL_PAINTING_TEST_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/data/painting")

add_test(
    NAME logsquirl_tests
    COMMAND logsquirl_tests -platform offscreen
)
set_tests_properties(logsquirl_tests PROPERTIES TIMEOUT 120)