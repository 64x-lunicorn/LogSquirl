add_executable(file_write_helper
        file_write_helper.cpp
        file_write_helper.h
)

target_link_libraries(file_write_helper logsquirl_utils tbb)

add_library(test_utils INTERFACE)
target_include_directories(test_utils SYSTEM INTERFACE ${CMAKE_CURRENT_SOURCE_DIR})

# A data race on purpose, which the tsan_canary test runs through the ctest
# runner to prove that a race in LogSquirl's code fails its case (#439).
if(ENABLE_SANITIZER_THREAD)
  add_executable(logsquirl_tsan_canary tsan_canary.cpp)
  target_link_libraries(logsquirl_tsan_canary PRIVATE project_options project_warnings)
  add_dependencies(ci_build logsquirl_tsan_canary)
endif()
