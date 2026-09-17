# The Plugin Catalog: what plugins are installed, read from their manifests.
# It loads no library and needs only Qt Core, so it is tested on its own (#178).
add_library(
  logsquirl_plugin_catalog STATIC
  ${CMAKE_CURRENT_SOURCE_DIR}/include/logsquirl_plugin_api.h
  ${CMAKE_CURRENT_SOURCE_DIR}/include/plugincatalog.h
  ${CMAKE_CURRENT_SOURCE_DIR}/include/pluginmetadata.h
  ${CMAKE_CURRENT_SOURCE_DIR}/src/plugincatalog.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/pluginmetadata.cpp
)

target_include_directories(logsquirl_plugin_catalog PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_link_libraries(
  logsquirl_plugin_catalog
  PUBLIC project_options
         project_warnings
         logsquirl_logging
         Qt6::Core
)

add_library(
  logsquirl_plugins STATIC
  ${CMAKE_CURRENT_SOURCE_DIR}/include/applicationplugins.h
  ${CMAKE_CURRENT_SOURCE_DIR}/include/pluginloader.h
  ${CMAKE_CURRENT_SOURCE_DIR}/include/pluginhost.h
  ${CMAKE_CURRENT_SOURCE_DIR}/include/pluginrepository.h
  ${CMAKE_CURRENT_SOURCE_DIR}/include/pluginuiport.h
  ${CMAKE_CURRENT_SOURCE_DIR}/include/streamwriter.h
  ${CMAKE_CURRENT_SOURCE_DIR}/src/applicationplugins.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/pluginloader.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/pluginhost.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/pluginrepository.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/streamwriter.cpp
)

set_target_properties(logsquirl_plugins PROPERTIES AUTOMOC ON)

# The plugin layer knows no widgets: what plugins show goes through the
# Plugin UI Port, which the UI library implements (#175).
# tests/plugins_no_qt_widgets.cmake guards the includes.
# Deliberately not logsquirl_settings either: the Plugin Host is handed which
# plugins to load, and its caller keeps what it enables on a first run (#236).
target_include_directories(logsquirl_plugins PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_link_libraries(
  logsquirl_plugins
  PUBLIC project_options
         project_warnings
         logsquirl_plugin_catalog
         logsquirl_logging
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
