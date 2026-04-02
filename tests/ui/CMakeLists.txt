set(UI_TEST_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/qtests_main.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/logdata_test.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/logfiltereddata_test.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/crawlerwidget_test.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/worker_destruction_test.cpp
)

if(NOT APPLE)
    LIST(APPEND UI_TEST_SOURCES  ${CMAKE_CURRENT_SOURCE_DIR}/mainwindow_test.cpp)
endif()

add_executable(logsquirl_itests ${UI_TEST_SOURCES} ${BACKWARD_ENABLE})

target_link_libraries(logsquirl_itests logsquirl_ui logsquirl_utils Catch2 Qt6::Test test_utils)
set_target_properties(logsquirl_itests PROPERTIES AUTOMOC ON)

add_backward(logsquirl_itests)

add_test(
    NAME logsquirl_itests
    COMMAND logsquirl_itests -platform offscreen
)
# Prevent infinite hangs in CI (e.g. from unexpected modal dialogs)
set_tests_properties(logsquirl_itests PROPERTIES TIMEOUT 120)