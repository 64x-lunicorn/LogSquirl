add_library(
  logsquirl_settings STATIC
  ${CMAKE_CURRENT_SOURCE_DIR}/src/persistentinfo.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/configuration.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/shortcuts.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/styles.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/theme.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/settingspolicies.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/include/persistable.h
  ${CMAKE_CURRENT_SOURCE_DIR}/include/persistentinfo.h
  ${CMAKE_CURRENT_SOURCE_DIR}/include/configuration.h
  ${CMAKE_CURRENT_SOURCE_DIR}/include/styles.h
  ${CMAKE_CURRENT_SOURCE_DIR}/include/theme.h
  ${CMAKE_SOURCE_DIR}/Resources/theme.qrc
  ${CMAKE_CURRENT_SOURCE_DIR}/include/shortcuts.h
)

target_include_directories(logsquirl_settings PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
set_target_properties(logsquirl_settings PROPERTIES AUTORCC ON)
target_link_libraries(
  logsquirl_settings
  PUBLIC project_options
         project_warnings
         logsquirl_utils
         logsquirl_policies
         Qt6::Core
         Qt6::Gui
)

if(LOGSQUIRL_USE_LTO)
  set_property(TARGET logsquirl_settings PROPERTY INTERPROCEDURAL_OPTIMIZATION TRUE)
endif()
