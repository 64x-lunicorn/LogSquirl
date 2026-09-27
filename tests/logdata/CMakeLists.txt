# The engine is tested on its own: the log data, its Searches and its
# indexing run without the UI library, a widget or the settings store (#554).
add_executable(logsquirl_logdata_tests
    logdata_tests_main.cpp
    logdata_test.cpp
    logfiltereddata_test.cpp
    indexing_test.cpp
    indexingblocks_test.cpp
    worker_destruction_test.cpp
)

# indexing_test and logfiltereddata_test pin TBB's concurrency (#168);
# logsquirl_tbb_headers carries TBB's include directory and compile
# definitions, and the TBB library itself arrives through the log data
# library (#450).
target_link_libraries(logsquirl_logdata_tests logsquirl_logdata logsquirl_tbb_headers Catch2::Catch2 Qt6::Test test_utils)
set_target_properties(logsquirl_logdata_tests PROPERTIES AUTOMOC ON)

# logdata_test writes its growing Log Files with a helper process beside it.
add_dependencies(logsquirl_logdata_tests file_write_helper)

logsquirl_add_catch_tests(logsquirl_logdata_tests TIMEOUT 120)
