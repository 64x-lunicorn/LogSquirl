find_package(Qt6 COMPONENTS Test REQUIRED)

include(CatchTestDiscovery)

add_subdirectory(helpers)
add_subdirectory(unit)
add_subdirectory(openlogfile)
add_subdirectory(textviewscrolling)
add_subdirectory(ui)
add_subdirectory(benchmarks)

add_dependencies(logsquirl_itests file_write_helper)
add_dependencies(ci_build logsquirl_grep logsquirl_tests logsquirl_compression_tests logsquirl_plugin_catalog_tests logsquirl_openlogfile_tests logsquirl_textviewscrolling_tests logsquirl_itests logsquirl_decoration_benchmark logsquirl_textviewscrolling_benchmark logsquirl_textview_scroll_benchmark logsquirl_logdata_benchmark logsquirl_logdata_read_benchmark logsquirl_session_restore_benchmark logsquirl_regex_matcher_benchmark logsquirl_tableview_paint_benchmark logsquirl_displayedlines_benchmark logsquirl_filteredview_read_benchmark logsquirl_chart_follow_benchmark logsquirl_chart_paint_benchmark logsquirl_overview_selection_benchmark logsquirl_versioncheck_tests)

# TBB is a private dependency of the log data library: no public header of
# it may include a TBB header (#168).
add_test(
    NAME logdata_public_headers_no_tbb
    COMMAND ${CMAKE_COMMAND}
            -DHEADERS_DIR=${CMAKE_SOURCE_DIR}/src/logdata/include
            -P ${CMAKE_CURRENT_SOURCE_DIR}/logdata_public_headers_no_tbb.cmake
)

# The plugin layer builds without Qt Widgets: no file of it may include a Qt
# Widgets header, and the library may not link Qt Widgets (#175). Nor may it
# link the settings store: the Plugin Host is handed its configuration (#236).
add_test(
    NAME plugins_no_qt_widgets
    COMMAND ${CMAKE_COMMAND}
            -DSOURCES_DIR=${CMAKE_SOURCE_DIR}/src/plugins
            "-DWIDGETS_INCLUDE_DIRS=$<JOIN:$<TARGET_PROPERTY:Qt6::Widgets,INTERFACE_INCLUDE_DIRECTORIES>,|>"
            "-DLINK_LIBRARIES=$<JOIN:$<TARGET_PROPERTY:logsquirl_plugins,LINK_LIBRARIES>,|>"
            -P ${CMAKE_CURRENT_SOURCE_DIR}/plugins_no_qt_widgets.cmake
)

# The Open Log File builds without Qt Widgets: no file of it may include a Qt
# Widgets header, and the library may link neither Qt Widgets, the settings
# store nor the UI library (#244).
add_test(
    NAME openlogfile_no_qt_widgets
    COMMAND ${CMAKE_COMMAND}
            -DSOURCES_DIR=${CMAKE_SOURCE_DIR}/src/openlogfile
            "-DWIDGETS_INCLUDE_DIRS=$<JOIN:$<TARGET_PROPERTY:Qt6::Widgets,INTERFACE_INCLUDE_DIRECTORIES>,|>"
            "-DLINK_LIBRARIES=$<JOIN:$<TARGET_PROPERTY:logsquirl_openlogfile,LINK_LIBRARIES>,|>"
            -P ${CMAKE_CURRENT_SOURCE_DIR}/openlogfile_no_qt_widgets.cmake
)

# The scrolling rules of a text view build without Qt Widgets: no file of them
# may include a Qt Widgets header, and the library may link neither Qt Widgets,
# the settings store nor the UI library (#246).
add_test(
    NAME textviewscrolling_no_qt_widgets
    COMMAND ${CMAKE_COMMAND}
            -DSOURCES_DIR=${CMAKE_SOURCE_DIR}/src/textviewscrolling
            "-DWIDGETS_INCLUDE_DIRS=$<JOIN:$<TARGET_PROPERTY:Qt6::Widgets,INTERFACE_INCLUDE_DIRECTORIES>,|>"
            "-DLINK_LIBRARIES=$<JOIN:$<TARGET_PROPERTY:logsquirl_textviewscrolling,LINK_LIBRARIES>,|>"
            -P ${CMAKE_CURRENT_SOURCE_DIR}/textviewscrolling_no_qt_widgets.cmake
)

# The UI half of the Settings Policy seam: the widget layer legitimately links
# logsquirl_settings, so an undeclared setting cannot be a link error there.
# Only the allowlisted files may name the settings store (#187).
add_test(
    NAME ui_settings_store_allowlist
    COMMAND ${CMAKE_COMMAND}
            -DSOURCES_DIR=${CMAKE_SOURCE_DIR}/src/ui
            -P ${CMAKE_CURRENT_SOURCE_DIR}/ui_settings_store_allowlist.cmake
)

# The compiler flags of an architecture follow the target, and the generic-CPU
# option gives every architecture a portable baseline (#285).
add_test(
    NAME architecture_flags
    COMMAND ${CMAKE_COMMAND}
            -DMODULE_DIR=${CMAKE_SOURCE_DIR}/cmake
            -P ${CMAKE_CURRENT_SOURCE_DIR}/architecture_flags.cmake
)

# The releases are optimized fully and keep their debug information (#280).
add_test(
    NAME full_optimization_flags
    COMMAND ${CMAKE_COMMAND}
            -DMODULE_DIR=${CMAKE_SOURCE_DIR}/cmake
            -P ${CMAKE_CURRENT_SOURCE_DIR}/full_optimization_flags.cmake
)

# Link time optimization covers every compiled target under src/ and tests/
# alike (#280).
function(logsquirl_compiled_targets dir out_var)
  get_property(_targets DIRECTORY "${dir}" PROPERTY BUILDSYSTEM_TARGETS)
  get_property(_subdirs DIRECTORY "${dir}" PROPERTY SUBDIRECTORIES)
  set(_compiled "")
  foreach(_target IN LISTS _targets)
    get_target_property(_type ${_target} TYPE)
    if(_type MATCHES "^(EXECUTABLE|STATIC_LIBRARY|SHARED_LIBRARY|MODULE_LIBRARY|OBJECT_LIBRARY)$")
      list(APPEND _compiled ${_target})
    endif()
  endforeach()
  foreach(_subdir IN LISTS _subdirs)
    logsquirl_compiled_targets("${_subdir}" _below)
    list(APPEND _compiled ${_below})
  endforeach()
  set(${out_var} "${_compiled}" PARENT_SCOPE)
endfunction()
logsquirl_compiled_targets("${CMAKE_SOURCE_DIR}/src" _lto_targets)
logsquirl_compiled_targets("${CMAKE_CURRENT_SOURCE_DIR}" _lto_tests)
set(_lto_entries "")
foreach(_target IN LISTS _lto_targets _lto_tests)
  list(APPEND _lto_entries "${_target}=$<TARGET_PROPERTY:${_target},INTERPROCEDURAL_OPTIMIZATION>")
endforeach()
list(JOIN _lto_entries "|" _lto_entries)
add_test(
    NAME lto_covers_project_targets
    COMMAND ${CMAKE_COMMAND}
            -DUSE_LTO=${LOGSQUIRL_USE_LTO}
            "-DTARGETS=${_lto_entries}"
            -P ${CMAKE_CURRENT_SOURCE_DIR}/lto_covers_project_targets.cmake
)







# The command line tool, run as a user runs it on a temporary Log File: what it
# prints and the code it exits with (#247).
add_test(
    NAME logsquirl_grep_cli
    COMMAND ${CMAKE_COMMAND}
            -DGREP=$<TARGET_FILE:logsquirl_grep>
            -DWORK_DIR=${CMAKE_CURRENT_BINARY_DIR}/logsquirl_grep_cli
            -P ${CMAKE_CURRENT_SOURCE_DIR}/logsquirl_grep_cli.cmake
)
set_tests_properties(logsquirl_grep_cli PROPERTIES TIMEOUT 300)

# The same on the Hyperscan build for CPUs without AVX2, where the Windows
# release chooses its Hyperscan DLL at run time (#281).
if(TARGET logsquirl_hyperscan_avx2)
  add_test(
      NAME logsquirl_grep_cli_hyperscan_without_avx2
      COMMAND ${CMAKE_COMMAND}
              -DGREP=$<TARGET_FILE:logsquirl_grep>
              -DWORK_DIR=${CMAKE_CURRENT_BINARY_DIR}/logsquirl_grep_cli_hyperscan_without_avx2
              -P ${CMAKE_CURRENT_SOURCE_DIR}/logsquirl_grep_cli.cmake
  )
  set_tests_properties(logsquirl_grep_cli_hyperscan_without_avx2 PROPERTIES
      ENVIRONMENT "LOGSQUIRL_HYPERSCAN_DISABLE_AVX2=1"
      TIMEOUT 300)
endif()
