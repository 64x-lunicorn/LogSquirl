add_library(
  logsquirl_logformat STATIC
  ${CMAKE_CURRENT_SOURCE_DIR}/include/logformatdefinition.h
  ${CMAKE_CURRENT_SOURCE_DIR}/include/logformatparser.h
  ${CMAKE_CURRENT_SOURCE_DIR}/include/logformatcatalog.h
  ${CMAKE_CURRENT_SOURCE_DIR}/include/formatrecognition.h
  ${CMAKE_CURRENT_SOURCE_DIR}/include/logfieldextractor.h
  ${CMAKE_CURRENT_SOURCE_DIR}/include/jsonlogline.h
  ${CMAKE_CURRENT_SOURCE_DIR}/include/logfmtlogline.h
  ${CMAKE_CURRENT_SOURCE_DIR}/include/logformattablemodel.h
  ${CMAKE_CURRENT_SOURCE_DIR}/include/rowmapping.h
  ${CMAKE_CURRENT_SOURCE_DIR}/include/timestampreader.h
  ${CMAKE_CURRENT_SOURCE_DIR}/include/timelookup.h
  ${CMAKE_CURRENT_SOURCE_DIR}/src/logformatparser.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/logformatcatalog.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/formatrecognition.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/logfieldextractor.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/jsonlogline.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/logfmtlogline.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/logformattablemodel.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/timestampreader.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/timelookup.cpp
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
