add_executable(file_write_helper
        file_write_helper.cpp
        file_write_helper.h
)

target_link_libraries(file_write_helper logsquirl_utils tbb)

add_library(test_utils INTERFACE)
target_include_directories(test_utils SYSTEM INTERFACE ${CMAKE_CURRENT_SOURCE_DIR})