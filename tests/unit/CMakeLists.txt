# Add test cpp file
add_executable(logsquirl_tests
    abstractlogview_test.cpp
    chartseries_test.cpp
    charttemplategenerator_test.cpp
    chipmunkimporter_test.cpp
    configuration_test.cpp
    decorationsetup_test.cpp
    displayedlines_test.cpp
    encodingdetector_test.cpp
    fontutils_test.cpp
    indexcache_test.cpp
    headerandtaildigests_test.cpp
    indexedhash_test.cpp
    jwtdecoder_test.cpp
    linedecorator_test.cpp
    linemapping_test.cpp
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
    logviewinteractions_test.cpp
    logviewpainting_test.cpp
    mergecontroller_test.cpp
    optionsdialog_test.cpp
    patternmatcher_test.cpp
    plugindialog_test.cpp
    pluginhost_test.cpp
    pluginmetadata_test.cpp
    pluginrepository_test.cpp
    pluginuiadapter_test.cpp
    quickfindmux_test.cpp
    quickfindwidget_test.cpp
    regularexpression_test.cpp
    regularexpressionpattern_test.cpp
    searchfailure_test.cpp
    searchsession_test.cpp
    settingspolicies_test.cpp
    tabbarstyle_test.cpp
    tableviewselection_test.cpp
    theme_test.cpp
    viewportlayout_test.cpp
    viewset_test.cpp
    wrappedstring_test.cpp
    tests_main.cpp
)

target_link_libraries(logsquirl_tests logsquirl_ui logsquirl_highlighting logsquirl_plugins logsquirl_logformat logsquirl_utils logsquirl_logging logsquirl_settings Catch2 Qt6::Test test_utils)
set_target_properties(logsquirl_tests PROPERTIES AUTOMOC ON)

# The Options Dialog test needs the dialog's generated form header, and the
# dialog lists its languages from the application's resources (#108).
target_include_directories(logsquirl_tests PRIVATE
    "$<TARGET_PROPERTY:logsquirl_ui,BINARY_DIR>/logsquirl_ui_autogen/include")
qt_add_resources(logsquirl_tests "optionsdialog_languages"
    PREFIX "/"
    BASE "${CMAKE_SOURCE_DIR}/src/app"
    FILES "${CMAKE_SOURCE_DIR}/src/app/i18n/Languages.xml")

# The painting test reads its font and golden images from the source tree,
# and rewrites the golden images there when asked to (#135).
target_compile_definitions(logsquirl_tests PRIVATE
    LOGSQUIRL_PAINTING_TEST_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/data/painting"
    LOGSQUIRL_CONFIGURATION_TEST_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/data/configuration")

# The Plugin UI Port test loads a real plugin library that hands its host
# callback table back to the test (#175).
add_library(logsquirl_ui_port_probe MODULE plugins/uiportprobe.cpp)
target_include_directories(logsquirl_ui_port_probe PRIVATE "${CMAKE_SOURCE_DIR}/src/plugins/include")
set_target_properties(logsquirl_ui_port_probe PROPERTIES CXX_VISIBILITY_PRESET hidden)
add_dependencies(logsquirl_tests logsquirl_ui_port_probe)
target_compile_definitions(logsquirl_tests PRIVATE
    LOGSQUIRL_UI_PORT_PROBE_PATH="$<TARGET_FILE:logsquirl_ui_port_probe>")

# Each test case is its own ctest test (#217). Its timeout is the one the whole
# executable had: the slowest case, the 10,000 Visual Lines scenario, takes a
# third of the suite in a Debug build and is slower under the sanitizers.
logsquirl_add_catch_tests(logsquirl_tests
    EXTRA_ARGS -platform offscreen
    TIMEOUT 120)

# The compression library is tested on its own, without the UI library or
# Qt Widgets (#169).
add_executable(logsquirl_compression_tests
    compression_tests_main.cpp
    lz4device_test.cpp
    zstddevice_test.cpp
)

target_link_libraries(logsquirl_compression_tests logsquirl_compression Catch2)

logsquirl_add_catch_tests(logsquirl_compression_tests TIMEOUT 60)

# The Plugin Catalog is tested on its own: it reads plugin manifests without
# loading a plugin library, the plugin host, or a Qt GUI (#178).
add_executable(logsquirl_plugin_catalog_tests
    plugin_catalog_tests_main.cpp
    plugincatalog_test.cpp
)

target_link_libraries(logsquirl_plugin_catalog_tests logsquirl_plugin_catalog Catch2)

logsquirl_add_catch_tests(logsquirl_plugin_catalog_tests TIMEOUT 60)
