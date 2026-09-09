# Micro-benchmarks for hot paths that need no GUI to measure. Deliberately
# linked against logsquirl_highlighting only -- no logsquirl_ui, no
# QApplication -- so the numbers reflect the decoration path alone.
add_executable(logsquirl_decoration_benchmark
    linedecorator_benchmark.cpp
)

target_link_libraries(logsquirl_decoration_benchmark logsquirl_highlighting Catch2)
set_target_properties(logsquirl_decoration_benchmark PROPERTIES AUTOMOC ON)
