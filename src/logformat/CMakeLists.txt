add_library(
  logsquirl_logformat STATIC
  ${CMAKE_CURRENT_SOURCE_DIR}/include/logformatdefinition.h
  ${CMAKE_CURRENT_SOURCE_DIR}/include/logformatparser.h
  ${CMAKE_CURRENT_SOURCE_DIR}/include/logformatregistry.h
  ${CMAKE_CURRENT_SOURCE_DIR}/include/logformatmatcher.h
  ${CMAKE_CURRENT_SOURCE_DIR}/include/logfieldextractor.h
  ${CMAKE_CURRENT_SOURCE_DIR}/include/logformattablemodel.h
  ${CMAKE_CURRENT_SOURCE_DIR}/src/logformatparser.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/logformatregistry.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/logformatmatcher.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/logfieldextractor.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/logformattablemodel.cpp
  ${CMAKE_SOURCE_DIR}/Resources/formats/formats.qrc
)
target_include_directories(logsquirl_logformat PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
set_target_properties(logsquirl_logformat PROPERTIES AUTOMOC ON AUTORCC ON)
target_link_libraries(
  logsquirl_logformat
  PUBLIC project_options
         project_warnings
         Qt6::Core
         logsquirl_logdata
)

if(LOGSQUIRL_USE_LTO)
  set_property(TARGET logsquirl_logformat PROPERTY INTERPROCEDURAL_OPTIMIZATION TRUE)
endif()
