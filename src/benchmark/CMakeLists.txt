# What a benchmark run of the application reports (#666): the events of a
# scenario with their times, the peak RSS, and the JSON that carries them, and
# the registry of the scenarios. Qt Core only: the scenarios themselves, which
# drive windows, live with the application (src/app/benchmark).
add_library(
  logsquirl_benchmark STATIC
  ${CMAKE_CURRENT_SOURCE_DIR}/include/benchmarkreport.h
  ${CMAKE_CURRENT_SOURCE_DIR}/include/benchmarkscenario.h
  ${CMAKE_CURRENT_SOURCE_DIR}/include/frametimes.h
  ${CMAKE_CURRENT_SOURCE_DIR}/include/inputlatency.h
  ${CMAKE_CURRENT_SOURCE_DIR}/include/processclock.h
  ${CMAKE_CURRENT_SOURCE_DIR}/src/benchmarkreport.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/benchmarkscenario.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/frametimes.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/inputlatency.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/processclock.cpp
)

target_include_directories(logsquirl_benchmark PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_link_libraries(logsquirl_benchmark PUBLIC project_options project_warnings Qt6::Core)

# GetProcessMemoryInfo, for the peak working set.
if(WIN32)
  target_link_libraries(logsquirl_benchmark PRIVATE psapi)
endif()
