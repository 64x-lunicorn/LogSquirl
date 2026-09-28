cmake_minimum_required(VERSION 3.16)
project(my_plugin LANGUAGES CXX)

# The plugin includes the SDK header and nothing else of LogSquirl.
if(NOT EXISTS "${LOGSQUIRL_SDK_DIR}/logsquirl_plugin_api.h")
  message(FATAL_ERROR "Set LOGSQUIRL_SDK_DIR to the directory that holds logsquirl_plugin_api.h")
endif()

# A MODULE is a library that is loaded at runtime and never linked against.
add_library(my_plugin MODULE src/my_plugin.cpp)
target_include_directories(my_plugin PRIVATE "${LOGSQUIRL_SDK_DIR}")
target_compile_features(my_plugin PRIVATE cxx_std_20)
# Export only the functions marked LOGSQUIRL_PLUGIN_EXPORT.
set_target_properties(my_plugin PROPERTIES CXX_VISIBILITY_PRESET hidden)
