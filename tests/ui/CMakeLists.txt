set(UI_TEST_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/qtests_main.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/filewatcher_test.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/indexing_test.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/logdata_test.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/logfiltereddata_test.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/filteredviewscrolling_test.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/filteredviewquickfind_test.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/filteredviewsave_test.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/marknavigation_test.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/crawlerwidget_test.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/crawlerwidget_formatrecognition_test.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/worker_destruction_test.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/policy_change_test.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/settings_during_run_test.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/filterspanel_test.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/theme_switch_test.cpp
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
target_link_libraries(logsquirl_itests logsquirl_ui logsquirl_utils tbb Catch2 Qt6::Test test_utils)
set_target_properties(logsquirl_itests PROPERTIES AUTOMOC ON AUTORCC ON)
# theme_switch_test opens dialogs whose headers include uic-generated headers.
target_include_directories(logsquirl_itests PRIVATE
    $<TARGET_PROPERTY:logsquirl_ui,BINARY_DIR>/logsquirl_ui_autogen/include)

add_backward(logsquirl_itests)

add_test(
    NAME logsquirl_itests
    COMMAND logsquirl_itests -platform offscreen
)
# Prevent infinite hangs in CI (e.g. from unexpected modal dialogs)
set_tests_properties(logsquirl_itests PROPERTIES TIMEOUT 240)