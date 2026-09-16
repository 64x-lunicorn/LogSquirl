# The scrolling rules of a text view are tested on their own: without the UI
# library, a widget or a QApplication (#246).
add_executable(logsquirl_textviewscrolling_tests
    textviewscrolling_tests_main.cpp
    textviewscrolling_test.cpp
)

target_link_libraries(logsquirl_textviewscrolling_tests logsquirl_textviewscrolling Catch2 test_utils)

add_test(
    NAME logsquirl_textviewscrolling_tests
    COMMAND logsquirl_textviewscrolling_tests
)
set_tests_properties(logsquirl_textviewscrolling_tests PROPERTIES TIMEOUT 120)
