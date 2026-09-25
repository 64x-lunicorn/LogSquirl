find_package(Qt6 COMPONENTS Test REQUIRED)

include(CatchTestDiscovery)

add_subdirectory(helpers)
add_subdirectory(unit)
add_subdirectory(openlogfile)
add_subdirectory(textviewscrolling)
add_subdirectory(searchline)
add_subdirectory(ui)
add_subdirectory(benchmarks)

add_dependencies(logsquirl_itests file_write_helper)
add_dependencies(ci_build logsquirl_grep logsquirl_tests logsquirl_compression_tests logsquirl_plugin_catalog_tests logsquirl_openlogfile_tests logsquirl_textviewscrolling_tests logsquirl_searchline_tests logsquirl_itests logsquirl_decoration_benchmark logsquirl_textviewscrolling_benchmark logsquirl_textview_scroll_benchmark logsquirl_logdata_benchmark logsquirl_logdata_read_benchmark logsquirl_session_restore_benchmark logsquirl_regex_matcher_benchmark logsquirl_tableview_paint_benchmark logsquirl_tableview_scroll_benchmark logsquirl_displayedlines_benchmark logsquirl_filteredview_read_benchmark logsquirl_chart_follow_benchmark logsquirl_chart_paint_benchmark logsquirl_overview_selection_benchmark logsquirl_timelookup_benchmark logsquirl_versioncheck_tests)

# TBB is a private dependency of the log data library: no public header of
# it may include a TBB header (#168).
add_test(
    NAME logdata_public_headers_no_tbb
    COMMAND ${CMAKE_COMMAND}
            -DHEADERS_DIR=${CMAKE_SOURCE_DIR}/src/logdata/include
            -P ${CMAKE_CURRENT_SOURCE_DIR}/logdata_public_headers_no_tbb.cmake
)

# The log data library reads text through Qt 6's converters: no public header
# may name QTextCodec and the library may not link Qt5Compat (#442).
add_test(
    NAME logdata_no_qt5compat
    COMMAND ${CMAKE_COMMAND}
            -DLOGDATA_DIR=${CMAKE_SOURCE_DIR}/src/logdata
            -P ${CMAKE_CURRENT_SOURCE_DIR}/logdata_no_qt5compat.cmake
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

# The Search Line builds without Qt Widgets: no file of it may include a Qt
# Widgets header, and the library may link neither Qt Widgets, the settings
# store nor the UI library (#399).
add_test(
    NAME searchline_no_qt_widgets
    COMMAND ${CMAKE_COMMAND}
            -DSOURCES_DIR=${CMAKE_SOURCE_DIR}/src/searchline
            "-DWIDGETS_INCLUDE_DIRS=$<JOIN:$<TARGET_PROPERTY:Qt6::Widgets,INTERFACE_INCLUDE_DIRECTORIES>,|>"
            "-DLINK_LIBRARIES=$<JOIN:$<TARGET_PROPERTY:logsquirl_searchline,LINK_LIBRARIES>,|>"
            -P ${CMAKE_CURRENT_SOURCE_DIR}/searchline_no_qt_widgets.cmake
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

# The ctest runner leaves out only the TSan reports of races made inside a
# library that is not built with TSan; the rest fails the case (#482).
add_test(
    NAME tsan_report_filter
    COMMAND ${CMAKE_COMMAND}
            -DMODULE_DIR=${CMAKE_SOURCE_DIR}/cmake
            -DWORK_DIR=${CMAKE_CURRENT_BINARY_DIR}/tsan_report_filter
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tsan_report_filter.cmake
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

# The project's warnings cover every compiled target under src/ and tests/
# alike (#451). They come from project_warnings, which a target either links
# itself or reaches through a library it links, so the answer is in the link
# graph: walk it here and hand the check one TRUE or FALSE per target. Only
# the project's own directories are walked; the 3rdparty libraries CPM brings
# in are not ours to keep warning-clean.
# The link libraries of one target along which the project's warnings can
# travel. own_private is TRUE for the target the question is about: its own
# PRIVATE dependencies compile its sources too. Further out, only what a
# library hands on counts, which is its INTERFACE_LINK_LIBRARIES without the
# entries wrapped in $<LINK_ONLY:...> -- those name a library to link and pass
# on none of its usage requirements, warnings included.
function(logsquirl_warning_edges target own_private out_var)
  set(_properties INTERFACE_LINK_LIBRARIES)
  # An INTERFACE library has no LINK_LIBRARIES, and CMake before 3.19 refuses
  # to be asked for one.
  get_target_property(_type ${target} TYPE)
  if(own_private AND NOT _type STREQUAL "INTERFACE_LIBRARY")
    list(APPEND _properties LINK_LIBRARIES)
  endif()
  set(_edges "")
  foreach(_property IN LISTS _properties)
    get_target_property(_libs ${target} ${_property})
    if(NOT _libs)
      continue()
    endif()
    foreach(_lib IN LISTS _libs)
      if(_lib MATCHES "^\\$<" OR NOT TARGET "${_lib}")
        continue()
      endif()
      get_target_property(_aliased ${_lib} ALIASED_TARGET)
      if(_aliased)
        set(_lib "${_aliased}")
      endif()
      list(APPEND _edges "${_lib}")
    endforeach()
  endforeach()
  set(${out_var} "${_edges}" PARENT_SCOPE)
endfunction()

function(logsquirl_reaches_project_warnings target out_var)
  set(_seen "${target}")
  logsquirl_warning_edges("${target}" TRUE _pending)
  while(_pending)
    list(POP_FRONT _pending _current)
    if(_current STREQUAL "project_warnings")
      set(${out_var} TRUE PARENT_SCOPE)
      return()
    endif()
    if(_current IN_LIST _seen)
      continue()
    endif()
    list(APPEND _seen "${_current}")
    logsquirl_warning_edges("${_current}" FALSE _edges)
    list(APPEND _pending ${_edges})
  endwhile()
  set(${out_var} FALSE PARENT_SCOPE)
endfunction()
logsquirl_compiled_targets("${CMAKE_SOURCE_DIR}/src" _warning_targets)
logsquirl_compiled_targets("${CMAKE_CURRENT_SOURCE_DIR}" _warning_tests)
set(_warning_entries "")
foreach(_target IN LISTS _warning_targets _warning_tests)
  logsquirl_reaches_project_warnings(${_target} _reaches_warnings)
  list(APPEND _warning_entries "${_target}=${_reaches_warnings}")
endforeach()
list(JOIN _warning_entries "|" _warning_entries)
add_test(
    NAME project_warnings_cover_project_targets
    COMMAND ${CMAKE_COMMAND}
            "-DTARGETS=${_warning_entries}"
            -P ${CMAKE_CURRENT_SOURCE_DIR}/project_warnings_cover_project_targets.cmake
)

# The other side of the same line: the third-party libraries CPM brings in are
# built without warnings, and the flag that does it reaches none of LogSquirl's
# own targets (#452). Read from the compile options here, where
# logsquirl_third_party_build_quietly() has already put them on -- 3rdparty/ is
# added long before this directory.
#
# What "quietly" means differs by compiler, because the two compilers answer a
# second warning level differently. On GCC and Clang -w has to be on the target
# itself, last, to inhibit whatever a package turned on for itself. On MSVC the
# level is the one /W0 in CMAKE_C(XX)_FLAGS and a target may carry none of its
# own: a second one on the same command line is a D9025 per file rather than a
# quieter build.
if(MSVC)
  set(_quiet_flag /W0)
else()
  set(_quiet_flag -w)
endif()
include(ThirdPartyWarnings)
function(logsquirl_built_quietly target out_var)
  get_target_property(_options ${target} COMPILE_OPTIONS)
  if(NOT _options)
    set(_options "")
  endif()
  if(MSVC)
    # Quiet is /W0 and no other level, whatever shape the other one came in --
    # a package's generator expression counts as one. cl says nothing about the
    # same level twice, so /W0 in the flags and /W0 here is fine; a second,
    # different level is a D9025 for every file of that target.
    set(_others "${_options}")
    list(REMOVE_ITEM _others "${_quiet_flag}")
    set(_stripped "${_others}")
    logsquirl_strip_msvc_warning_level(_stripped)
    if("${_quiet_flag}" IN_LIST _options AND "${_stripped}" STREQUAL "${_others}")
      set(${out_var} TRUE PARENT_SCOPE)
    else()
      set(${out_var} FALSE PARENT_SCOPE)
    endif()
  elseif("${_quiet_flag}" IN_LIST _options)
    set(${out_var} TRUE PARENT_SCOPE)
  else()
    set(${out_var} FALSE PARENT_SCOPE)
  endif()
endfunction()

# The project's own half asks the other question, on every compiler alike: the
# flag that silences third-party code has not reached this target.
function(logsquirl_carries_quiet_flag target out_var)
  get_target_property(_options ${target} COMPILE_OPTIONS)
  if(_options AND "${_quiet_flag}" IN_LIST _options)
    set(${out_var} TRUE PARENT_SCOPE)
  else()
    set(${out_var} FALSE PARENT_SCOPE)
  endif()
endfunction()
logsquirl_compiled_targets("${CMAKE_SOURCE_DIR}/3rdparty" _third_party_targets)
set(_third_party_entries "")
foreach(_target IN LISTS _third_party_targets)
  logsquirl_built_quietly(${_target} _quiet)
  # A target that is not quiet says what it is carrying. Without that the
  # report names a target and leaves whoever reads it to guess which of the
  # shapes a warning level arrives in this one used. The separators the entries
  # are joined with are spelled out of the way first.
  set(_name "${_target}")
  if(NOT _quiet)
    get_target_property(_target_options ${_target} COMPILE_OPTIONS)
    if(NOT _target_options)
      set(_target_options "(none)")
    endif()
    string(REPLACE ";" " " _target_options "${_target_options}")
    string(REPLACE "=" "~" _target_options "${_target_options}")
    string(REPLACE "|" "!" _target_options "${_target_options}")
    # And the $ of a generator expression becomes a # -- this text goes into an
    # add_test() command line, where CMake would evaluate it, and a package's
    # own $<$<NOT:$<CXX_COMPILER_ID:Intel>>:...> fails the generate step there
    # ("$<CXX_COMPILER_ID> may only be used with binary targets"). The point is
    # to read what the target carries, not to run it.
    string(REPLACE "$" "#" _target_options "${_target_options}")
    set(_name "${_target} [${_target_options}]")
  endif()
  list(APPEND _third_party_entries "${_name}=${_quiet}")
endforeach()
list(JOIN _third_party_entries "|" _third_party_entries)
set(_project_quiet_entries "")
foreach(_target IN LISTS _warning_targets _warning_tests)
  logsquirl_carries_quiet_flag(${_target} _quiet)
  list(APPEND _project_quiet_entries "${_target}=${_quiet}")
endforeach()
list(JOIN _project_quiet_entries "|" _project_quiet_entries)
add_test(
    NAME third_party_builds_quietly
    COMMAND ${CMAKE_COMMAND}
            -DQUIET_FLAG=${_quiet_flag}
            "-DTHIRD_PARTY=${_third_party_entries}"
            "-DPROJECT_TARGETS=${_project_quiet_entries}"
            # This directory's CMAKE_CXX_FLAGS is the one src/ has too: both
            # inherit it from the top-level CMakeLists.txt, which is where the
            # third-party flags would land if they were set one directory too
            # high.
            "-DPROJECT_FLAGS=${CMAKE_CXX_FLAGS}"
            -P ${CMAKE_CURRENT_SOURCE_DIR}/third_party_builds_quietly.cmake
)

# And the flags that do it are the project's warning level taken out, not one
# more level put on top (#452).
add_test(
    NAME third_party_quiet_flags
    COMMAND ${CMAKE_COMMAND}
            -DMODULE_DIR=${CMAKE_SOURCE_DIR}/cmake
            -P ${CMAKE_CURRENT_SOURCE_DIR}/third_party_quiet_flags.cmake
)

# The link step of a build with link time optimization generates code a second
# time, and what it reports has to fail the build the same way. The one warning
# accepted there is accepted for the GCC versions that report it and no longer
# (#454).
add_test(
    NAME project_warning_flags
    COMMAND ${CMAKE_COMMAND}
            -DMODULE_DIR=${CMAKE_SOURCE_DIR}/cmake
            -P ${CMAKE_CURRENT_SOURCE_DIR}/project_warning_flags.cmake
)

# A packaging recipe passes only build options the project declares, and only
# build types CMake knows: CMake takes an unknown -D or build type without a
# word, so nothing but a check tells the packager (#333).
add_test(
    NAME packaging_build_options
    COMMAND ${CMAKE_COMMAND}
            -DPROJECT_DIR=${CMAKE_SOURCE_DIR}
            -P ${CMAKE_CURRENT_SOURCE_DIR}/packaging_build_options.cmake
)







# The version the binaries report is the one the project declares, leading zero
# and all, whether the build was handed a version or left to CMakeLists.txt
# (#372).
add_test(
    NAME version_as_declared
    COMMAND ${CMAKE_COMMAND}
            -DGREP=$<TARGET_FILE:logsquirl_grep>
            -DPROJECT_DIR=${CMAKE_SOURCE_DIR}
            "-DVERSION_OVERRIDE=${BUILD_VERSION}"
            "-DVERSION_BUILD_NUMBER=${BUILD_NUMBER}"
            -DWORK_DIR=${CMAKE_CURRENT_BINARY_DIR}/version_as_declared
            -P ${CMAKE_CURRENT_SOURCE_DIR}/version_as_declared.cmake
)

# The translations that exist are the ones built into the application and
# offered in the Options Dialog, each of them is finished enough to be usable,
# and none is behind the strings the source holds today (#448).
find_package(Qt6 COMPONENTS LinguistTools REQUIRED)
add_test(
    NAME translations_shipped
    COMMAND ${CMAKE_COMMAND}
            -DI18N_DIR=${CMAKE_SOURCE_DIR}/src/app/i18n
            -DAPP_QRC=${CMAKE_BINARY_DIR}/src/app/app_translations.qrc
            -DLUPDATE=$<TARGET_FILE:Qt6::lupdate>
            -DSOURCES_DIR=${CMAKE_SOURCE_DIR}/src
            -DWORK_DIR=${CMAKE_CURRENT_BINARY_DIR}/translations_shipped
            -P ${CMAKE_CURRENT_SOURCE_DIR}/translations_shipped.cmake
)

# A test binary reads and writes its settings in a throwaway location of its
# own: the Options Dialog case saves everything it changed, and nothing of it
# may reach the settings file beside the built binaries, which the application,
# the command line tool and every other test binary read (#370). The binary is
# run here as a developer runs it, not through the isolation ctest gives a test
# case, so this is the check that a run by hand keeps to itself too.
add_test(
    NAME test_binary_own_settings
    COMMAND ${CMAKE_COMMAND}
            -DTEST_BINARY=$<TARGET_FILE:logsquirl_tests>
            "-DTEST_SPEC=[optionsdialog]"
            -P ${CMAKE_CURRENT_SOURCE_DIR}/test_binary_own_settings.cmake
)
set_tests_properties(test_binary_own_settings PROPERTIES TIMEOUT 300)

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
