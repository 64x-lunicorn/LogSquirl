# The Search Line is tested on its own: without the UI library, a widget or a
# QApplication (#399).
add_executable(logsquirl_searchline_tests
    searchline_tests_main.cpp
    searchline_test.cpp
)

target_link_libraries(logsquirl_searchline_tests logsquirl_searchline Catch2::Catch2 test_utils)

logsquirl_add_catch_tests(logsquirl_searchline_tests TIMEOUT 120)
