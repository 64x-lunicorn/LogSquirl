add_library(
  logsquirl_plugins STATIC
  ${CMAKE_CURRENT_SOURCE_DIR}/include/logsquirl_plugin_api.h
  ${CMAKE_CURRENT_SOURCE_DIR}/include/pluginmetadata.h
  ${CMAKE_CURRENT_SOURCE_DIR}/include/pluginloader.h
  ${CMAKE_CURRENT_SOURCE_DIR}/include/pluginmanager.h
  ${CMAKE_CURRENT_SOURCE_DIR}/include/pluginrepository.h
  ${CMAKE_CURRENT_SOURCE_DIR}/include/pluginuiport.h
  ${CMAKE_CURRENT_SOURCE_DIR}/include/streamwriter.h
  ${CMAKE_CURRENT_SOURCE_DIR}/src/pluginmetadata.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/pluginloader.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/pluginmanager.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/pluginrepository.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/streamwriter.cpp
)

set_target_properties(logsquirl_plugins PROPERTIES AUTOMOC ON)

# The plugin layer knows no widgets: what plugins show goes through the
# Plugin UI Port, which the UI library implements (#175).
# tests/plugins_no_qt_widgets.cmake guards the includes.
target_include_directories(logsquirl_plugins PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_link_libraries(
  logsquirl_plugins
  PUBLIC project_options
         project_warnings
         logsquirl_logging
         logsquirl_settings
         logsquirl_karchive
         Qt6::Core
         Qt6::Gui
         Qt6::Network
)

# Install the MIT-licensed C ABI header as part of the Plugin SDK.
# This is the only file external plugin developers need.
install(
  FILES ${CMAKE_CURRENT_SOURCE_DIR}/include/logsquirl_plugin_api.h
  DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/logsquirl
  COMPONENT sdk
)
