add_library(
  logsquirl_compression STATIC
  ${CMAKE_CURRENT_SOURCE_DIR}/include/decompressor.h
  ${CMAKE_CURRENT_SOURCE_DIR}/include/lz4device.h
  ${CMAKE_CURRENT_SOURCE_DIR}/include/zstddevice.h
  ${CMAKE_CURRENT_SOURCE_DIR}/src/decompressor.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/lz4device.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/zstddevice.cpp
)

set_target_properties(logsquirl_compression PROPERTIES AUTOMOC ON)

if(LOGSQUIRL_USE_LTO)
  set_property(TARGET logsquirl_compression PROPERTY INTERPROCEDURAL_OPTIMIZATION TRUE)
endif()

target_include_directories(logsquirl_compression PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")

# decompressor.h uses the header-only atomicflag.h. Take only the include
# directory of logsquirl_utils: linking it would pull in Qt Widgets.
target_include_directories(
  logsquirl_compression PUBLIC "$<TARGET_PROPERTY:logsquirl_utils,INTERFACE_INCLUDE_DIRECTORIES>"
)

target_link_libraries(
  logsquirl_compression
  PUBLIC project_options
         project_warnings
         logsquirl_logging
         logsquirl_karchive
         logsquirl_zstd
         logsquirl_lz4
         Qt6::Core
         Qt6::Concurrent
)

# The compression library must stay free of Qt Widgets (#169). Walk its link
# graph at configure time and fail loudly if Widgets sneaks in.
function(logsquirl_collect_link_libraries target out_var)
  set(_seen ${${out_var}})
  foreach(_property LINK_LIBRARIES INTERFACE_LINK_LIBRARIES)
    get_target_property(_libs ${target} ${_property})
    if(NOT _libs)
      continue()
    endif()
    foreach(_lib IN LISTS _libs)
      string(REGEX REPLACE "^\\$<LINK_ONLY:(.*)>$" "\\1" _lib "${_lib}")
      if(_lib MATCHES "^\\$<" OR NOT TARGET "${_lib}")
        continue()
      endif()
      get_target_property(_aliased ${_lib} ALIASED_TARGET)
      if(_aliased)
        set(_lib ${_aliased})
      endif()
      if(NOT _lib IN_LIST _seen)
        list(APPEND _seen ${_lib})
        set(${out_var} ${_seen})
        logsquirl_collect_link_libraries(${_lib} ${out_var})
        set(_seen ${${out_var}})
      endif()
    endforeach()
  endforeach()
  set(${out_var} ${_seen} PARENT_SCOPE)
endfunction()

set(_compression_deps "")
logsquirl_collect_link_libraries(logsquirl_compression _compression_deps)
get_target_property(_qt_widgets Qt6::Widgets ALIASED_TARGET)
if(NOT _qt_widgets)
  set(_qt_widgets Qt6::Widgets)
endif()
if(Qt6::Widgets IN_LIST _compression_deps OR _qt_widgets IN_LIST _compression_deps)
  message(FATAL_ERROR "logsquirl_compression must not link Qt Widgets (#169)")
endif()
