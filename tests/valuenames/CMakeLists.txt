# The rule engine of Value Names is tested on its own: without the UI library,
# a widget or a QApplication (#647).
add_executable(logsquirl_valuenames_tests
    valuenames_tests_main.cpp
    valuenamer_test.cpp
    nametablecsv_test.cpp
    shownline_test.cpp
)

target_link_libraries(logsquirl_valuenames_tests logsquirl_valuenames Catch2::Catch2 test_utils)

logsquirl_add_catch_tests(logsquirl_valuenames_tests TIMEOUT 120)
