set(UI_TEST_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/qtests_main.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/filewatcher_test.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/indexing_test.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/indexingblocks_test.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/logdata_test.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/logfiltereddata_test.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/filteredviewscrolling_test.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/filteredviewquickfind_test.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/filteredviewsave_test.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/filteredviewselection_test.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/marknavigation_test.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/overview_test.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/crawlerwidget_test.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/crawlerwidget_formatrecognition_test.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/worker_destruction_test.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/policy_change_test.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/session_test.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/tabbedcrawlerwidget_test.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/settings_during_run_test.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/filterspanel_test.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/theme_switch_test.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/applicationplugins_window_test.cpp

    ${CMAKE_CURRENT_SOURCE_DIR}/theme_screenshots_test.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/sidebar_layout_test.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/overviewwidget_theme_test.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/viewportmargins_theme_test.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/radiobutton_slider_theme_test.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/checkbox_theme_test.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/shortcuts_table_test.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/theme_chrome_colors_test.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/theme_color_labels_test.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/theme_geometry_test.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/welcomedashboard_layout_test.cpp
)

if(NOT APPLE)
    LIST(APPEND UI_TEST_SOURCES  ${CMAKE_CURRENT_SOURCE_DIR}/mainwindow_test.cpp)
endif()

# theme_switch_test checks the icons a Theme switch loads.
list(APPEND UI_TEST_SOURCES
    ${CMAKE_SOURCE_DIR}/src/app/logsquirl.qrc
    ${CMAKE_SOURCE_DIR}/Resources/themes.qrc)

add_executable(logsquirl_itests ${UI_TEST_SOURCES} ${BACKWARD_ENABLE})

# tbb: indexing_test and logfiltereddata_test pin TBB's concurrency (#168).
target_link_libraries(logsquirl_itests logsquirl_ui logsquirl_filewatch logsquirl_utils tbb Catch2 Qt6::Test test_utils)
set_target_properties(logsquirl_itests PROPERTIES AUTOMOC ON AUTORCC ON)
# theme_switch_test opens dialogs whose headers include uic-generated headers.
target_include_directories(logsquirl_itests PRIVATE
    $<TARGET_PROPERTY:logsquirl_ui,BINARY_DIR>/logsquirl_ui_autogen/include)

# The plugins load once per application, after the first window shows (#303):
# a slow converter plugin built for the test shows that a Log File opened
# before it loaded still goes through it.
add_library(logsquirl_test_slow_converter MODULE plugins/slowconverter.cpp)
target_include_directories(logsquirl_test_slow_converter PRIVATE "${CMAKE_SOURCE_DIR}/src/plugins/include")
set_target_properties(logsquirl_test_slow_converter PROPERTIES CXX_VISIBILITY_PRESET hidden)
add_dependencies(logsquirl_itests logsquirl_test_slow_converter)
target_compile_definitions(logsquirl_itests PRIVATE
    LOGSQUIRL_TEST_SLOW_CONVERTER_PATH="$<TARGET_FILE:logsquirl_test_slow_converter>")

# theme_screenshots_test copies the demo log from test_data/ (#253).
target_compile_definitions(logsquirl_itests PRIVATE LOGSQUIRL_SOURCE_DIR="${CMAKE_SOURCE_DIR}")

add_backward(logsquirl_itests)

# Each test case is its own ctest test; the timeout stops a hanging case (e.g.
# on an unexpected modal dialog) and names it (#217).
logsquirl_add_catch_tests(logsquirl_itests
    EXTRA_ARGS -platform offscreen
    TIMEOUT 120)