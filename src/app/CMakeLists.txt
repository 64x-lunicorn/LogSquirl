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
  ${CMAKE_CURRENT_SOURCE_DIR}/i18n/zh_CN.ts
)

find_package(Qt${QT_VERSION_MAJOR}LinguistTools REQUIRED)
if (${QT_VERSION_MAJOR} EQUAL 5)
  qt5_add_translation(QM_FILES ${TS_FILES})
else (${QT_VERSION_MAJOR} EQUAL 6)
  qt6_add_translation(QM_FILES ${TS_FILES})
endif()

if (NOT TARGET Qt${QT_VERSION_MAJOR}::lupdate)
    message(FATAL_ERROR "The package \"Qt${QT_VERSION_MAJOR}LinguistTools\" is required.")
endif()
set(Qt_LUPDATE_EXECUTABLE Qt${QT_VERSION_MAJOR}::lupdate)
set(Qt_LCONVERT_EXECUTABLE Qt${QT_VERSION_MAJOR}::lconvert)
add_custom_target(lupdate
    COMMAND ${Qt_LUPDATE_EXECUTABLE} -recursive -no-obsolete ${CMAKE_SOURCE_DIR}/src -ts ${TS_FILES}
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}/src
    COMMENT "Updating translations"
)

include(TranslationUtils)
add_app_translations_resource(LOGSQUIRL_I18N_RES ${QM_FILES})
add_qt_translations_resource(LOGSQUIRL_QT_TRANSLATION_RES zh_CN)

set(MAIN_SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/cli.h
                 ${CMAKE_CURRENT_SOURCE_DIR}/logsquirl.qrc 
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

if(WIN32)
  add_dependencies(ci_build logsquirl_portable)
endif()

set_target_properties(logsquirl PROPERTIES AUTORCC ON)
set_target_properties(logsquirl PROPERTIES AUTOMOC ON)
set_target_properties(logsquirl_portable PROPERTIES AUTORCC ON)
set_target_properties(logsquirl_portable PROPERTIES AUTOMOC ON)
set_target_properties(logsquirl_grep PROPERTIES AUTORCC ON)
set_target_properties(logsquirl_grep PROPERTIES AUTOMOC ON)

if(LOGSQUIRL_USE_LTO)
  set_property(TARGET logsquirl PROPERTY INTERPROCEDURAL_OPTIMIZATION TRUE)
  set_property(TARGET logsquirl_portable PROPERTY INTERPROCEDURAL_OPTIMIZATION TRUE)
  set_property(TARGET logsquirl_grep PROPERTY INTERPROCEDURAL_OPTIMIZATION TRUE)
endif()

target_link_libraries(logsquirl PUBLIC ${MAIN_LIBS} logsquirl_ui)
target_link_libraries(logsquirl_portable PUBLIC ${MAIN_LIBS} logsquirl_ui)
target_link_libraries(logsquirl_grep PUBLIC ${MAIN_LIBS})

target_compile_definitions(logsquirl_portable PUBLIC -DLOGSQUIRL_PORTABLE)

if(WIN32)

  target_sources(logsquirl PRIVATE ${ProductVersionResourceFiles})
  target_sources(logsquirl_portable PRIVATE ${ProductVersionResourceFiles})
  target_sources(logsquirl_grep PRIVATE ${ProductVersionResourceFiles})

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
  if(TARGET tbbmalloc_proxy)
    add_custom_command(
      TARGET logsquirl
      POST_BUILD
      COMMAND cmake -E copy $<TARGET_FILE:tbbmalloc> $<TARGET_FILE_DIR:logsquirl>
      WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
      COMMENT "Copy tbbmalloc"
    )
    add_custom_command(
      TARGET logsquirl
      POST_BUILD
      COMMAND cmake -E copy $<TARGET_FILE:tbbmalloc_proxy> $<TARGET_FILE_DIR:logsquirl>
      WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
      COMMENT "Copy tbbmalloc_proxy"
    )
  endif()
endif()

if(LOGSQUIRL_USE_SENTRY)
  if(NOT WIN32)
    if(APPLE)
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

    add_custom_command(
      TARGET logsquirl
      POST_BUILD
      COMMAND cmake -E copy ${SYMS_TOOLS}/minidump_dump $<TARGET_FILE_DIR:logsquirl>/logsquirl_minidump_dump
      WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
      COMMENT "Copy stackwalk"
    )
  else()
    add_custom_command(
      TARGET logsquirl
      POST_BUILD
      COMMAND cmake -E copy $<TARGET_FILE:crashpad_handler> $<TARGET_FILE_DIR:logsquirl>/logsquirl_crashpad_handler.exe
      WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
      COMMENT "Copy crash_handler"
    )

    if(CMAKE_SIZEOF_VOID_P EQUAL 8)
      add_custom_command(
        TARGET logsquirl
        POST_BUILD
        COMMAND cmake -E copy ${SYMS_TOOLS}/minidump_dump_x64.exe $<TARGET_FILE_DIR:logsquirl>/logsquirl_minidump_dump.exe
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
        COMMENT "Copy stackwalk"
      )
    else()
      add_custom_command(
        TARGET logsquirl
        POST_BUILD
        COMMAND cmake -E copy ${SYMS_TOOLS}/minidump_dump_x86.exe $<TARGET_FILE_DIR:logsquirl>/logsquirl_minidump_dump.exe
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
        COMMENT "Copy stackwalk"
      )
    endif()
  endif()

  if(APPLE)
    install(PROGRAMS $<TARGET_FILE_DIR:logsquirl>/logsquirl_minidump_dump $<TARGET_FILE_DIR:logsquirl>/logsquirl_crashpad_handler
            DESTINATION $<TARGET_BUNDLE_CONTENT_DIR:logsquirl>/MacOS
    )
  elseif(NOT WIN32)
    install(PROGRAMS $<TARGET_FILE_DIR:logsquirl>/logsquirl_minidump_dump $<TARGET_FILE_DIR:logsquirl>/logsquirl_crashpad_handler
            DESTINATION ${CMAKE_INSTALL_BINDIR}
    )
  endif()
endif(LOGSQUIRL_USE_SENTRY)
