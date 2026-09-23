# Micro-benchmarks for hot paths that need no GUI to measure. Deliberately
# linked against logsquirl_highlighting only -- no logsquirl_ui, no
# QApplication -- so the numbers reflect the decoration path alone.
add_executable(logsquirl_decoration_benchmark
    linedecorator_benchmark.cpp
)

target_link_libraries(logsquirl_decoration_benchmark logsquirl_highlighting Catch2::Catch2WithMain)
set_target_properties(logsquirl_decoration_benchmark PROPERTIES AUTOMOC ON)

# The scrolling rules of a text view, without a widget (#246): links
# logsquirl_textviewscrolling only.
add_executable(logsquirl_textviewscrolling_benchmark
    textviewscrolling_benchmark.cpp
)

target_link_libraries(logsquirl_textviewscrolling_benchmark logsquirl_textviewscrolling Catch2::Catch2)

# Scrolling a shown text view through Qt events. Uses only what the text view
# offered before #246, so the same file measures both sides of an A/B
# comparison (see README.md).
add_executable(logsquirl_textview_scroll_benchmark
    textview_scroll_benchmark.cpp
)

target_link_libraries(logsquirl_textview_scroll_benchmark logsquirl_ui Catch2::Catch2 test_utils)

# Indexing a Log File and reading its Log Lines (#275), on Log Files of about
# 1 GB generated at run time: links logsquirl_logdata only.
add_executable(logsquirl_logdata_benchmark
    logdata_benchmark.cpp
)

target_link_libraries(logsquirl_logdata_benchmark logsquirl_logdata Catch2::Catch2 test_utils)

# Reading Log Lines from a Log File, hiding ANSI color sequences and showing
# them (#278). Links logsquirl_logdata only; uses only what LogData offered
# before #278, so the same file measures both sides of an A/B comparison.
add_executable(logsquirl_logdata_read_benchmark
    logdata_read_benchmark.cpp
)

target_include_directories(logsquirl_logdata_read_benchmark PRIVATE "${CMAKE_SOURCE_DIR}/tests/helpers")
target_link_libraries(logsquirl_logdata_read_benchmark logsquirl_logdata Catch2::Catch2)

# Restoring a Session of 20 tabs at startup (#301): the Session info, tab
# names and tab groups read from the settings store; and a Session of several
# large Log Files until its current tab, or every tab, has loaded (#300). Uses
# only what the Session and the tab area offered before #301 (see README.md).
add_executable(logsquirl_session_restore_benchmark
    session_restore_benchmark.cpp
)

target_link_libraries(logsquirl_session_restore_benchmark logsquirl_ui Catch2::Catch2 test_utils)
set_target_properties(logsquirl_session_restore_benchmark PROPERTIES AUTOMOC ON)

# Matching a block of Log Lines with patterns Vectorscan rejects and with
# boolean expressions, on both regex engines (#279). Links logsquirl_regex
# only; uses only what the regex module offered before #279, so the same file
# measures both sides of an A/B comparison.
add_executable(logsquirl_regex_matcher_benchmark
    regex_matcher_benchmark.cpp
)

target_link_libraries(logsquirl_regex_matcher_benchmark logsquirl_regex Catch2::Catch2WithMain)

# Painting a shown Table View (#294): a viewport of 50 Rows and 6 columns with
# a Highlighter Set, the mouse moving between Rows, and the in-cell hit test.
# Uses only what the Table View offered before #294 (see README.md).
add_executable(logsquirl_tableview_paint_benchmark
    tableview_paint_benchmark.cpp
)

target_link_libraries(logsquirl_tableview_paint_benchmark logsquirl_ui Catch2::Catch2 test_utils)

# The Displayed Lines of a Search with a million Matches (#292): progress
# ticks, completion, continuation and toggling a Mark. Links logsquirl_logdata
# only; builds on commits from before #292 too (see README.md).
add_executable(logsquirl_displayedlines_benchmark
    displayedlines_benchmark.cpp
)

target_link_libraries(logsquirl_displayedlines_benchmark logsquirl_logdata Catch2::Catch2WithMain)

# A QuickFind without a match over a Log File of about 1 GB generated at run
# time (#287). Uses only what QuickFind and LogData offered before #287, so
# the same file measures both sides of an A/B comparison (see README.md).
add_executable(logsquirl_quickfind_benchmark
    quickfind_benchmark.cpp
)

target_link_libraries(logsquirl_quickfind_benchmark logsquirl_ui Catch2::Catch2 test_utils)

# The readers of a Search's Displayed Lines (#288): the Filtered View painting,
# saving displayed lines, removing the longest of many Marks, and the command
# line tool printing its matches, which it runs from next to itself. Uses only
# what they offered before #288, so the same file measures both sides.
add_executable(logsquirl_filteredview_read_benchmark
    filteredview_read_benchmark.cpp
)

target_link_libraries(logsquirl_filteredview_read_benchmark logsquirl_ui Catch2::Catch2 test_utils)

# A chart following a growing Log File (#298): Log Lines appended to a Log File
# of about 128 MiB and the chart brought up to date after each load. Uses only
# what the chart panel offered before #298 (see README.md).
add_executable(logsquirl_chart_follow_benchmark
    chart_follow_benchmark.cpp
)

target_link_libraries(logsquirl_chart_follow_benchmark logsquirl_ui Catch2::Catch2 test_utils)

# Painting and hovering a chart of millions of points (#299), fitted and zoomed
# in to 1 %. Uses only what the chart widget offered before #299 (see
# README.md).
add_executable(logsquirl_chart_paint_benchmark
    chart_paint_benchmark.cpp
)

# test_utils: its main() relaunches itself beside its own settings file (#370).
target_link_libraries(logsquirl_chart_paint_benchmark logsquirl_ui Catch2::Catch2 test_utils)

# The overview with a million Matches and extending a long selection line by
# line (#297). Uses only what the Overview and the text view offered before
# #297, so the same file measures both sides of an A/B comparison (see
# README.md).
add_executable(logsquirl_overview_selection_benchmark
    overview_selection_benchmark.cpp
)

target_link_libraries(logsquirl_overview_selection_benchmark logsquirl_ui Catch2::Catch2 test_utils)

# The line positions of an Index (#321): appending them line by line and a
# block at a time, and reading them back one by one and as a range. Links
# logsquirl_logdata only; uses only what LinePositionArray offered before
# #321, so the same file measures both sides of an A/B comparison.
add_executable(logsquirl_linepositionarray_benchmark
    linepositionarray_benchmark.cpp
)

target_link_libraries(logsquirl_linepositionarray_benchmark logsquirl_logdata Catch2::Catch2WithMain)

# "Go to timestamp" on a Log File of 10 million Log Lines (#435): the lookup by
# binary search through the Index. Run it in an optimized build.
add_executable(logsquirl_timelookup_benchmark
    timelookup_benchmark.cpp
)

target_include_directories(logsquirl_timelookup_benchmark PRIVATE "${CMAKE_SOURCE_DIR}/tests/helpers")
target_link_libraries(logsquirl_timelookup_benchmark logsquirl_logformat logsquirl_logdata Catch2::Catch2)
