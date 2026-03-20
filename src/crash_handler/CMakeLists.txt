add_library(
  logsquirl_crash_handler STATIC
  ${CMAKE_CURRENT_SOURCE_DIR}/include/crashhandler.h
  ${CMAKE_CURRENT_SOURCE_DIR}/include/issuereporter.h
  ${CMAKE_CURRENT_SOURCE_DIR}/include/memory_info.h
  ${CMAKE_CURRENT_SOURCE_DIR}/src/issuereporter.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/memory_info.cpp
)

set_target_properties(logsquirl_crash_handler PROPERTIES AUTOMOC ON)

target_include_directories(logsquirl_crash_handler PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_link_libraries(
  logsquirl_crash_handler
  PUBLIC project_options
         project_warnings
         logsquirl_version
         logsquirl_utils
         tbb
         Qt${QT_VERSION_MAJOR}::Gui
         Qt${QT_VERSION_MAJOR}::Widgets
)

if(LOGSQUIRL_USE_SENTRY)
  target_compile_definitions(logsquirl_crash_handler PUBLIC -DLOGSQUIRL_USE_SENTRY)
  target_sources(logsquirl_crash_handler PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src/crashhandler.cpp)
  target_link_libraries(logsquirl_crash_handler PUBLIC sentry crashpad_client)
endif()

if(LOGSQUIRL_USE_LTO)
  set_property(TARGET logsquirl_crash_handler PROPERTY INTERPROCEDURAL_OPTIMIZATION TRUE)
endif()
