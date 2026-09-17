# Micro-benchmarks for hot paths that need no GUI to measure. Deliberately
# linked against logsquirl_highlighting only -- no logsquirl_ui, no
# QApplication -- so the numbers reflect the decoration path alone.
add_executable(logsquirl_decoration_benchmark
    linedecorator_benchmark.cpp
)

target_link_libraries(logsquirl_decoration_benchmark logsquirl_highlighting Catch2)
set_target_properties(logsquirl_decoration_benchmark PROPERTIES AUTOMOC ON)

# The scrolling rules of a text view, without a widget (#246): links
# logsquirl_textviewscrolling only.
add_executable(logsquirl_textviewscrolling_benchmark
    textviewscrolling_benchmark.cpp
)

target_link_libraries(logsquirl_textviewscrolling_benchmark logsquirl_textviewscrolling Catch2)

# Scrolling a shown text view through Qt events. Uses only what the text view
# offered before #246, so the same file measures both sides of an A/B
# comparison (see README.md).
add_executable(logsquirl_textview_scroll_benchmark
    textview_scroll_benchmark.cpp
)

target_link_libraries(logsquirl_textview_scroll_benchmark logsquirl_ui Catch2 test_utils)

# Indexing a Log File and reading its Log Lines (#275), on Log Files of about
# 1 GB generated at run time: links logsquirl_logdata only.
add_executable(logsquirl_logdata_benchmark
    logdata_benchmark.cpp
)

target_link_libraries(logsquirl_logdata_benchmark logsquirl_logdata Catch2 test_utils)

# Reading Log Lines from a Log File, hiding ANSI color sequences and showing
# them (#278). Links logsquirl_logdata only; uses only what LogData offered
# before #278, so the same file measures both sides of an A/B comparison.
add_executable(logsquirl_logdata_read_benchmark
    logdata_read_benchmark.cpp
)

target_include_directories(logsquirl_logdata_read_benchmark PRIVATE "${CMAKE_SOURCE_DIR}/tests/helpers")
target_link_libraries(logsquirl_logdata_read_benchmark logsquirl_logdata Catch2)

# Restoring a Session of 20 tabs at startup (#301): the Session info, tab
# names and tab groups read from the settings store. Uses only what the
# Session and the tab area offered before #301 (see README.md).
add_executable(logsquirl_session_restore_benchmark
    session_restore_benchmark.cpp
)

target_link_libraries(logsquirl_session_restore_benchmark logsquirl_ui Catch2 test_utils)
set_target_properties(logsquirl_session_restore_benchmark PROPERTIES AUTOMOC ON)

# Matching a block of Log Lines with patterns Vectorscan rejects and with
# boolean expressions, on both regex engines (#279). Links logsquirl_regex
# only; uses only what the regex module offered before #279, so the same file
# measures both sides of an A/B comparison.
add_executable(logsquirl_regex_matcher_benchmark
    regex_matcher_benchmark.cpp
)

target_link_libraries(logsquirl_regex_matcher_benchmark logsquirl_regex Catch2)

# A QuickFind without a match over a Log File of about 1 GB generated at run
# time (#287). Uses only what QuickFind and LogData offered before #287, so
# the same file measures both sides of an A/B comparison (see README.md).
add_executable(logsquirl_quickfind_benchmark
    quickfind_benchmark.cpp
)

target_link_libraries(logsquirl_quickfind_benchmark logsquirl_ui Catch2 test_utils)
