if(APPLE)
  set(OS_BUNDLE MACOSX_BUNDLE)
elseif(WIN32)
  set(OS_BUNDLE WIN32)
endif()

configure_file(${CMAKE_CURRENT_SOURCE_DIR}/documentation.qrc.in ${DOCUMENTATION_RESOURCE} @ONLY)

add_custom_command(
  OUTPUT ${DOCUMENTATION_HTML}
  COMMAND $<TARGET_FILE:maddy> ${DOCUMENTATION_FILE} ${DOCUMENTATION_HTML}
  DEPENDS maddy ${DOCUMENTATION_FILE}
)

set(TS_FILES
  ${CMAKE_CURRENT_SOURCE_DIR}/i18n/en.ts
  ${CMAKE_CURRENT_SOURCE_DIR}/i18n/de.ts
  ${CMAKE_CURRENT_SOURCE_DIR}/i18n/es.ts
  ${CMAKE_CURRENT_SOURCE_DIR}/i18n/fr.ts
  ${CMAKE_CURRENT_SOURCE_DIR}/i18n/pt_BR.ts
  ${CMAKE_CURRENT_SOURCE_DIR}/i18n/pt_PT.ts
  ${CMAKE_CURRENT_SOURCE_DIR}/i18n/uk.ts
  ${CMAKE_CURRENT_SOURCE_DIR}/i18n/zh_CN.ts
)

find_package(Qt6LinguistTools REQUIRED)
qt6_add_translation(QM_FILES ${TS_FILES})

if (NOT TARGET Qt6::lupdate)
    message(FATAL_ERROR "The package \"Qt6LinguistTools\" is required.")
endif()
set(Qt_LUPDATE_EXECUTABLE Qt6::lupdate)
set(Qt_LCONVERT_EXECUTABLE Qt6::lconvert)
add_custom_target(lupdate
    COMMAND ${Qt_LUPDATE_EXECUTABLE} -recursive -no-obsolete ${CMAKE_SOURCE_DIR}/src -ts ${TS_FILES}
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}/src
    COMMENT "Updating translations"
)

include(TranslationUtils)
add_app_translations_resource(LOGSQUIRL_I18N_RES ${QM_FILES})
add_qt_translations_resource(LOGSQUIRL_QT_TRANSLATION_RES de es fr pt_BR pt_PT uk zh_CN)

set(MAIN_SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/cli.h
                 ${CMAKE_CURRENT_SOURCE_DIR}/logsquirl.qrc 
                 ${CMAKE_SOURCE_DIR}/Resources/themes.qrc
                 ${LOGSQUIRL_I18N_RES}
                 ${LOGSQUIRL_QT_TRANSLATION_RES})

set(LOGSQUIRL_UI_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/messagereceiver.h
    ${CMAKE_CURRENT_SOURCE_DIR}/logsquirlapp.h
    ${CMAKE_CURRENT_SOURCE_DIR}/main.cpp
    ${DOCUMENTATION_RESOURCE}
    ${ICON_FILE}
)

set(LOGSQUIRL_GREP_SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/logsquirl_grep.cpp)

set(MAIN_LIBS
    logsquirl_logdata
    logsquirl_crash_handler
    roaring
    kdsingleapp
)

add_executable(logsquirl ${OS_BUNDLE} ${MAIN_SOURCES} ${LOGSQUIRL_UI_SOURCES})
add_executable(logsquirl_portable ${OS_BUNDLE} ${MAIN_SOURCES} ${LOGSQUIRL_UI_SOURCES})
add_executable(logsquirl_grep ${MAIN_SOURCES} ${LOGSQUIRL_GREP_SOURCES})

add_dependencies(ci_build logsquirl logsquirl_grep)

# The command line tool ships with the application (install() below), so
# whatever builds the application builds it too: a packaging recipe that names
# the `logsquirl` target -- packaging/linux/arch/PKGBUILD does -- would
# otherwise install a file the build never produced (#430).
add_dependencies(logsquirl logsquirl_grep)

if(WIN32)
  add_dependencies(ci_build logsquirl_portable)
endif()

set_target_properties(logsquirl PROPERTIES AUTORCC ON)
set_target_properties(logsquirl PROPERTIES AUTOMOC ON)
set_target_properties(logsquirl_portable PROPERTIES AUTORCC ON)
set_target_properties(logsquirl_portable PROPERTIES AUTOMOC ON)
set_target_properties(logsquirl_grep PROPERTIES AUTORCC ON)
set_target_properties(logsquirl_grep PROPERTIES AUTOMOC ON)

# The application looks the file watcher up and hands it to the Session; the
# engine is handed its port and does not link it (#249).
target_link_libraries(logsquirl PUBLIC ${MAIN_LIBS} logsquirl_ui logsquirl_filewatch)
target_link_libraries(logsquirl_portable PUBLIC ${MAIN_LIBS} logsquirl_ui logsquirl_filewatch)
# main.cpp caps TBB's concurrency itself (#168).
target_link_libraries(logsquirl PRIVATE tbb)
target_link_libraries(logsquirl_portable PRIVATE tbb)
# logsquirl_grep derives the Policies itself (there is no LogSquirlApp
# here), so unlike the log data library it does link the settings library.
# It reports a failure the engine reports by printing it, and so does not
# link the crash handler (#240). It follows its Log File through the Open Log
# File (#247), handing it no File Watch Port: it links no file watcher.
target_link_libraries(logsquirl_grep PUBLIC logsquirl_openlogfile kdsingleapp logsquirl_settings)

target_compile_definitions(logsquirl_portable PUBLIC -DLOGSQUIRL_PORTABLE)

if(WIN32)

  target_sources(logsquirl PRIVATE ${ProductVersionResourceFiles})
  target_sources(logsquirl_portable PRIVATE ${ProductVersionResourceFiles})
  target_sources(logsquirl_grep PRIVATE ${ProductVersionResourceFiles})

  # Embed Windows application manifest (asInvoker, DPI, long-path, SegmentHeap)
  set(_LOGSQUIRL_MANIFEST "${CMAKE_CURRENT_SOURCE_DIR}/logsquirl.manifest")
  foreach(_target logsquirl logsquirl_portable logsquirl_grep)
    target_sources(${_target} PRIVATE ${_LOGSQUIRL_MANIFEST})
  endforeach()

elseif(APPLE)
  set_source_files_properties(${ICON_FILE} PROPERTIES MACOSX_PACKAGE_LOCATION Resources)
  set_source_files_properties(${NOTICE_FILE} PROPERTIES MACOSX_PACKAGE_LOCATION SharedSupport)
  set_source_files_properties(${COPYING_FILE} PROPERTIES MACOSX_PACKAGE_LOCATION SharedSupport)

  set_source_files_properties(${README_FILE} PROPERTIES MACOSX_PACKAGE_LOCATION SharedSupport)
  set_source_files_properties(${DOCUMENTATION_FILE} PROPERTIES MACOSX_PACKAGE_LOCATION SharedSupport)
  set_source_files_properties(${DOCUMENTATION_HTML} PROPERTIES MACOSX_PACKAGE_LOCATION SharedSupport)

  set(MACOSX_BUNDLE_BUNDLE_DISPLAY_NAME ${PROJECT_DESCRIPTION})
  set(MACOSX_BUNDLE_BUNDLE_NAME ${PROJECT_NAME})
  set(MACOSX_BUNDLE_BUNDLE_VERSION ${PROJECT_VERSION})
  set(MACOSX_BUNDLE_LONG_VERSION_STRING ${PROJECT_VERSION})
  set(MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}")
  set(MACOSX_BUNDLE_COPYRIGHT ${COPYRIGHT})
  set(MACOSX_BUNDLE_GUI_IDENTIFIER ${IDENTIFIER})
  set(MACOSX_BUNDLE_ICON_FILE ${ICON_NAME})

endif()

install(
  TARGETS logsquirl
  DESTINATION ${CMAKE_INSTALL_BINDIR}
  BUNDLE DESTINATION .
)

# The command line tool is installed beside the application, so it reaches the
# deb, the rpm and the AppImage. On macOS there is no bin directory to be
# beside: it goes into the app bundle, where a bundled helper belongs and where
# the crashpad handler and the minidump tool go too, and is then called as
# /Applications/LogSquirl.app/Contents/MacOS/logsquirl_grep (#430).
if(APPLE)
  install(TARGETS logsquirl_grep DESTINATION $<TARGET_BUNDLE_CONTENT_DIR:logsquirl>/MacOS)

  # The DMG is packed from the app bundle in the build tree and not from an
  # install tree (.github/actions/agent-package-mac), so the tool is copied
  # into the bundle at build time as well -- the way the crashpad handler and
  # the minidump tool get there. macdeployqt is told about it so it uses the
  # bundled Qt frameworks (#430).
  add_custom_command(
    TARGET logsquirl_grep
    POST_BUILD
    COMMAND cmake -E make_directory $<TARGET_FILE_DIR:logsquirl>
    COMMAND cmake -E copy $<TARGET_FILE:logsquirl_grep> $<TARGET_FILE_DIR:logsquirl>
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    COMMENT "Copy logsquirl_grep into the app bundle"
  )
else()
  install(TARGETS logsquirl_grep DESTINATION ${CMAKE_INSTALL_BINDIR})
endif()

if(WIN32)
  if(TARGET tbb)
    add_custom_command(
      TARGET logsquirl
      POST_BUILD
      COMMAND cmake -E copy $<TARGET_FILE:tbb> $<TARGET_FILE_DIR:logsquirl>
      WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
      COMMENT "Copy tbb"
    )
  endif()
endif()

if(LOGSQUIRL_USE_SENTRY)
  if(NOT WIN32)
    if(APPLE)
      # With LTO on, ld64 merges every translation unit's debug info into one
      # temporary object during the link and deletes it once the binary is
      # written; dsymutil can no longer find where the line tables live, and
      # the dSYM it produces resolves a frame to a function but not a file or
      # line (#340). -object_path_lto tells ld64 to keep that object instead,
      # one per executable so parallel links do not collide. It has to land
      # outside the .app bundle: kept beside the binary, it used to end up
      # under Contents/MacOS, and codesign then tried to sign it as part of
      # the bundle and failed ("bundle format unrecognized") because it is a
      # raw LTO object, not a Mach-O executable or library.
      if(LOGSQUIRL_USE_LTO)
        file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/lto-objects")
        target_link_options(logsquirl PRIVATE
                             "LINKER:-object_path_lto,${CMAKE_CURRENT_BINARY_DIR}/lto-objects/logsquirl-lto.o"
        )
        target_link_options(logsquirl_portable PRIVATE
                             "LINKER:-object_path_lto,${CMAKE_CURRENT_BINARY_DIR}/lto-objects/logsquirl_portable-lto.o"
        )
      endif()

      find_program(DSYMUTIL dsymutil)
      add_custom_command(
        TARGET logsquirl
        POST_BUILD
        COMMAND ${DSYMUTIL} --out=${CMAKE_LIBRARY_OUTPUT_DIRECTORY}/logsquirl.dSym $<TARGET_FILE:logsquirl>
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
        COMMENT "Generating symbols for $<TARGET_FILE:logsquirl>"
      )

      add_custom_command(
        TARGET logsquirl_portable
        POST_BUILD
        COMMAND ${DSYMUTIL} --out=${CMAKE_LIBRARY_OUTPUT_DIRECTORY}/logsquirl_portable.dSym $<TARGET_FILE:logsquirl_portable>
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
        COMMENT "Generating symbols for $<TARGET_FILE:logsquirl_portable>"
      )
    else()
      find_program(OBJCOPY objcopy)
      add_custom_command(
        TARGET logsquirl
        POST_BUILD
        COMMAND ${OBJCOPY} --only-keep-debug --compress-debug-sections=zlib $<TARGET_FILE:logsquirl>
                ${CMAKE_LIBRARY_OUTPUT_DIRECTORY}/logsquirl.debug
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
        COMMENT "Generating symbols for $<TARGET_FILE:logsquirl>"
      )
      add_custom_command(
        TARGET logsquirl
        POST_BUILD
        COMMAND ${OBJCOPY} --strip-debug --strip-unneeded $<TARGET_FILE:logsquirl>
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
        COMMENT "Stripping $<TARGET_FILE:logsquirl>"
      )
      add_custom_command(
        TARGET logsquirl
        POST_BUILD
        COMMAND ${OBJCOPY} --add-gnu-debuglink=${CMAKE_LIBRARY_OUTPUT_DIRECTORY}/logsquirl.debug $<TARGET_FILE:logsquirl>
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
        COMMENT "Linking symbols for $<TARGET_FILE:logsquirl>"
      )

      add_custom_command(
        TARGET logsquirl_portable
        POST_BUILD
        COMMAND ${OBJCOPY} --only-keep-debug $<TARGET_FILE:logsquirl_portable>
                ${CMAKE_LIBRARY_OUTPUT_DIRECTORY}/logsquirl_portable.debug
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
        COMMENT "Generating symbols for $<TARGET_FILE:logsquirl>"
      )
      add_custom_command(
        TARGET logsquirl_portable
        POST_BUILD
        COMMAND ${OBJCOPY} --strip-debug --strip-unneeded $<TARGET_FILE:logsquirl_portable>
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
        COMMENT "Stripping $<TARGET_FILE:logsquirl_portable>"
      )
      add_custom_command(
        TARGET logsquirl_portable
        POST_BUILD
        COMMAND ${OBJCOPY} --add-gnu-debuglink=${CMAKE_LIBRARY_OUTPUT_DIRECTORY}/logsquirl_portable.debug
                $<TARGET_FILE:logsquirl_portable>
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
        COMMENT "Linking symbols for $<TARGET_FILE:logsquirl_portable>"
      )
    endif()

    add_custom_command(
      TARGET logsquirl
      POST_BUILD
      COMMAND cmake -E copy $<TARGET_FILE:crashpad_handler> $<TARGET_FILE_DIR:logsquirl>/logsquirl_crashpad_handler
      WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
      COMMENT "Copy crash_handler"
    )
  else()
    add_custom_command(
      TARGET logsquirl
      POST_BUILD
      COMMAND cmake -E copy $<TARGET_FILE:crashpad_handler> $<TARGET_FILE_DIR:logsquirl>/logsquirl_crashpad_handler.exe
      WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
      COMMENT "Copy crash_handler"
    )
  endif()

  # minidump-stackwalk (cmake/MinidumpStackwalk.cmake) keeps the name of the
  # Breakpad minidump_dump it replaced, which the installers and the crash
  # handler use (#318).
  if(WIN32)
    set(_minidump_tool_name logsquirl_minidump_dump.exe)
  else()
    set(_minidump_tool_name logsquirl_minidump_dump)
  endif()
  if(LOGSQUIRL_MINIDUMP_TOOL)
    add_custom_command(
      TARGET logsquirl
      POST_BUILD
      COMMAND cmake -E copy ${LOGSQUIRL_MINIDUMP_TOOL} $<TARGET_FILE_DIR:logsquirl>/${_minidump_tool_name}
      WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
      COMMENT "Copy minidump-stackwalk"
    )
    set(_minidump_tool_install $<TARGET_FILE_DIR:logsquirl>/${_minidump_tool_name})
  endif()

  if(APPLE)
    install(PROGRAMS ${_minidump_tool_install} $<TARGET_FILE_DIR:logsquirl>/logsquirl_crashpad_handler
            DESTINATION $<TARGET_BUNDLE_CONTENT_DIR:logsquirl>/MacOS
    )
  elseif(NOT WIN32)
    install(PROGRAMS ${_minidump_tool_install} $<TARGET_FILE_DIR:logsquirl>/logsquirl_crashpad_handler
            DESTINATION ${CMAKE_INSTALL_BINDIR}
    )
  endif()
endif(LOGSQUIRL_USE_SENTRY)
