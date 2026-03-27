add_library(
  logsquirl_plugins STATIC
  ${CMAKE_CURRENT_SOURCE_DIR}/include/logsquirl_plugin_api.h
  ${CMAKE_CURRENT_SOURCE_DIR}/include/pluginmetadata.h
  ${CMAKE_CURRENT_SOURCE_DIR}/include/pluginloader.h
  ${CMAKE_CURRENT_SOURCE_DIR}/include/pluginmanager.h
  ${CMAKE_CURRENT_SOURCE_DIR}/include/pluginrepository.h
  ${CMAKE_CURRENT_SOURCE_DIR}/include/pluginrepositorydialog.h
  ${CMAKE_CURRENT_SOURCE_DIR}/include/streamwriter.h
  ${CMAKE_CURRENT_SOURCE_DIR}/src/pluginmetadata.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/pluginloader.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/pluginmanager.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/pluginrepository.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/pluginrepositorydialog.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/streamwriter.cpp
)

set_target_properties(logsquirl_plugins PROPERTIES AUTOMOC ON)

target_include_directories(logsquirl_plugins PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_link_libraries(
  logsquirl_plugins
  PUBLIC project_options
         project_warnings
         logsquirl_logging
         logsquirl_settings
         logsquirl_karchive
         Qt6::Core
         Qt6::Network
         Qt6::Widgets
)

# Install the MIT-licensed C ABI header as part of the Plugin SDK.
# This is the only file external plugin developers need.
install(
  FILES ${CMAKE_CURRENT_SOURCE_DIR}/include/logsquirl_plugin_api.h
  DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/logsquirl
  COMPONENT sdk
)

# ── Lua scripting support (Phase 6, optional) ──────────────────────
if(LOGSQUIRL_USE_LUA)
  target_sources(logsquirl_plugins PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include/luapluginwrapper.h
    ${CMAKE_CURRENT_SOURCE_DIR}/src/luapluginwrapper.cpp
  )
  target_link_libraries(logsquirl_plugins PUBLIC sol2 lua_static)
  target_compile_definitions(logsquirl_plugins PUBLIC LOGSQUIRL_HAS_LUA=1)
endif()
