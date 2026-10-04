# The file types LogSquirl opens are declared once, in cmake/FileTypes.cmake,
# and each platform's packaging is generated from that list: the shared-mime-
# info package and the desktop entry of the deb and rpm (#717), the document
# types of the macOS app bundle (#718) and the file type page of the Windows
# installer (#719). This check generates all of them into a scratch directory
# and reads them back the way the platform will:
#
#   - Linux: application/x-logcat matches trace.adb and trace.adb3 but not
#     trace.adb12, wins .adb over the Ada source type of freedesktop.org.xml,
#     and every type LogSquirl opens is in the desktop entry's MimeType, the
#     compressed ones too. With update-mime-database and desktop-file-validate
#     installed, the files are also run through them.
#   - macOS: the Info.plist views the log type and plain text with the
#     document icon and .gz and .zip without, all with the Alternate role only,
#     exports the Logcat type with .adb and .adb0 to .adb9 one by one, and
#     names none of its own types in a document type, which would make it
#     their default; plutil checks it where it exists.
#   - Windows: one ProgID per type, the same names the application will use,
#     .log and the Logcat traces checked, the optional types not, and .gz and
#     .zip only as an Open with entry; makensis compiles it where it exists.
#
# Usage: cmake -DPROJECT_DIR=<source directory> -DWORK_DIR=<scratch directory>
#              -P file_types_packaging.cmake

cmake_minimum_required(VERSION 3.16)

if(NOT PROJECT_DIR OR NOT EXISTS "${PROJECT_DIR}/cmake/FileTypes.cmake")
  message(FATAL_ERROR "PROJECT_DIR has no cmake/FileTypes.cmake: '${PROJECT_DIR}'")
endif()
if(NOT WORK_DIR)
  message(FATAL_ERROR "WORK_DIR is not set")
endif()

file(REMOVE_RECURSE "${WORK_DIR}")
file(MAKE_DIRECTORY "${WORK_DIR}")

set(_failures "")
macro(fail _description)
  list(APPEND _failures "${_description}")
endmacro()

# Functions, not macros: a macro would read the backslashes of the text it is
# handed as escapes.
function(expect_contains _text _needle _description)
  string(FIND "${_text}" "${_needle}" _at)
  if(_at EQUAL -1)
    list(APPEND _failures "${_description}: '${_needle}' is missing")
    set(_failures
        "${_failures}"
        PARENT_SCOPE
    )
  endif()
endfunction()

function(expect_not_contains _text _needle _description)
  string(FIND "${_text}" "${_needle}" _at)
  if(NOT _at EQUAL -1)
    list(APPEND _failures "${_description}: '${_needle}' is there")
    set(_failures
        "${_failures}"
        PARENT_SCOPE
    )
  endif()
endfunction()

include("${PROJECT_DIR}/cmake/FileTypes.cmake")
logsquirl_generate_file_types("${WORK_DIR}" DESKTOP_TEMPLATE "${PROJECT_DIR}/packaging/linux/logsquirl.desktop.in")

# --- the list ------------------------------------------------------------

foreach(_id log logcat output trace text)
  if(NOT _id IN_LIST LOGSQUIRL_FILE_TYPES)
    fail("the file type '${_id}' is not declared")
  endif()
endforeach()
foreach(_id gz zip)
  if(NOT _id IN_LIST LOGSQUIRL_OPEN_WITH_TYPES)
    fail("the compressed type '${_id}' is not offered for Open with")
  endif()
  if(_id IN_LIST LOGSQUIRL_FILE_TYPES)
    fail("the compressed type '${_id}' is a file type LogSquirl could be made the default for")
  endif()
endforeach()
set(_expected_logcat adb adb0 adb1 adb2 adb3 adb4 adb5 adb6 adb7 adb8 adb9)
if(NOT LOGSQUIRL_FILE_TYPE_logcat_EXTENSIONS STREQUAL "${_expected_logcat}")
  fail("the Logcat traces are '${LOGSQUIRL_FILE_TYPE_logcat_EXTENSIONS}', not .adb and .adb0 to .adb9")
endif()
foreach(_id log logcat)
  if(NOT LOGSQUIRL_FILE_TYPE_${_id}_CHECKED)
    fail("'${_id}' is not checked by default")
  endif()
  if(NOT LOGSQUIRL_FILE_TYPE_${_id}_GROUP STREQUAL "logs")
    fail("'${_id}' is not in the group Log files")
  endif()
endforeach()
foreach(_id output trace text)
  if(LOGSQUIRL_FILE_TYPE_${_id}_CHECKED)
    fail("the optional type '${_id}' is checked by default")
  endif()
  if(NOT LOGSQUIRL_FILE_TYPE_${_id}_GROUP STREQUAL "optional")
    fail("'${_id}' is not in the group More (optional)")
  endif()
endforeach()
foreach(_id IN LISTS LOGSQUIRL_FILE_TYPES LOGSQUIRL_OPEN_WITH_TYPES)
  if(NOT LOGSQUIRL_FILE_TYPE_${_id}_PROGID STREQUAL "LogSquirl.${_id}")
    fail("the ProgID of '${_id}' is '${LOGSQUIRL_FILE_TYPE_${_id}_PROGID}', not LogSquirl.${_id}")
  endif()
endforeach()

# --- Linux (#717) ----------------------------------------------------------

file(READ "${WORK_DIR}/logsquirl.xml" _mime_xml)
expect_contains("${_mime_xml}" "<mime-type type=\"application/x-logcat\">" "the MIME package")
expect_contains("${_mime_xml}" "<sub-class-of type=\"text/plain\"/>" "the Logcat type")
expect_contains("${_mime_xml}" "<glob pattern=\"*.adb\" weight=\"60\"/>" "the Logcat type")
expect_contains("${_mime_xml}" "<glob pattern=\"*.adb9\" weight=\"60\"/>" "the Logcat type")
expect_contains("${_mime_xml}" "<mime-type type=\"text/x-log\">" "the MIME package gives the log type the document icon")
expect_contains("${_mime_xml}" "<generic-icon name=\"logsquirl-document\"/>" "the MIME package")
# text/plain is every text file there is: LogSquirl offers to open it, but does
# not put its icon on it.
expect_not_contains("${_mime_xml}" "type=\"text/plain\">" "the MIME package redefines plain text")
expect_not_contains("${_mime_xml}" "application/gzip" "the MIME package claims a compressed type")
expect_not_contains("${_mime_xml}" "application/zip" "the MIME package claims a compressed type")

file(READ "${WORK_DIR}/logsquirl.desktop" _desktop)
if(NOT _desktop MATCHES "\nMimeType=([^\n]*)\n")
  fail("the desktop entry has no MimeType line")
endif()
set(_desktop_types "${CMAKE_MATCH_1}")
foreach(_type text/plain text/x-log application/x-logcat text/x-program-output text/x-trace application/gzip application/zip)
  expect_contains(";${_desktop_types}" ";${_type};" "the desktop entry's MimeType")
endforeach()

# The packages refresh the MIME database, the icon cache and the desktop
# database after an install and after an uninstall.
file(READ "${WORK_DIR}/postinst" _postinst)
expect_contains("${_postinst}" "update-mime-database" "the package scripts")
file(READ "${WORK_DIR}/postrm" _postrm)
expect_contains("${_postrm}" "update-mime-database" "the package scripts")

# How the MIME database resolves a file name: the globs2 file
# update-mime-database writes, read the way xdg-mime reads it -- the highest
# weight wins, then the longest pattern. Only the plain `*.ext` patterns that
# decide these names are looked at.
function(mime_type_of _globs2 _name _out)
  set(_best_weight -1)
  set(_best_length -1)
  set(_best "")
  foreach(_line IN LISTS _globs2)
    if(NOT _line MATCHES "^([0-9]+):([^:]+):\\*(\\.[^*?[]+)$")
      continue()
    endif()
    set(_weight "${CMAKE_MATCH_1}")
    set(_type "${CMAKE_MATCH_2}")
    set(_suffix "${CMAKE_MATCH_3}")
    string(LENGTH "${_suffix}" _suffix_length)
    string(LENGTH "${_name}" _name_length)
    if(_suffix_length GREATER_EQUAL _name_length)
      continue()
    endif()
    math(EXPR _start "${_name_length} - ${_suffix_length}")
    string(SUBSTRING "${_name}" ${_start} -1 _tail)
    if(NOT _tail STREQUAL _suffix)
      continue()
    endif()
    if(_weight GREATER _best_weight OR (_weight EQUAL _best_weight AND _suffix_length GREATER _best_length))
      set(_best_weight ${_weight})
      set(_best_length ${_suffix_length})
      set(_best "${_type}")
    elseif(_weight EQUAL _best_weight AND _suffix_length EQUAL _best_length AND NOT _type STREQUAL _best)
      # Two types at the same weight and length: xdg-mime falls back to the
      # content, and a text file can come out as either.
      set(_best "ambiguous (${_best} or ${_type})")
    endif()
  endforeach()
  set(${_out} "${_best}" PARENT_SCOPE)
endfunction()

find_program(UPDATE_MIME_DATABASE update-mime-database)
if(UPDATE_MIME_DATABASE)
  set(_mime_dir "${WORK_DIR}/mime")
  file(MAKE_DIRECTORY "${_mime_dir}/packages")
  configure_file("${WORK_DIR}/logsquirl.xml" "${_mime_dir}/packages/logsquirl.xml" COPYONLY)
  # The distribution's own definitions, where there are any, so the Ada source
  # type that also claims *.adb is in the database.
  foreach(_fdo /usr/share/mime/packages/freedesktop.org.xml /opt/homebrew/share/mime/packages/freedesktop.org.xml
               /usr/local/share/mime/packages/freedesktop.org.xml
  )
    if(EXISTS "${_fdo}")
      configure_file("${_fdo}" "${_mime_dir}/packages/freedesktop.org.xml" COPYONLY)
      break()
    endif()
  endforeach()
  execute_process(
    COMMAND "${UPDATE_MIME_DATABASE}" "${_mime_dir}"
    RESULT_VARIABLE _result
    OUTPUT_VARIABLE _output
    ERROR_VARIABLE _output
  )
  if(NOT _result EQUAL 0 OR _output MATCHES "[Ww]arning|[Ee]rror")
    fail("update-mime-database rejects the MIME package (${_result}): ${_output}")
  else()
    file(STRINGS "${_mime_dir}/globs2" _globs2)
    foreach(_name trace.adb trace.adb0 trace.adb3 trace.adb9)
      mime_type_of("${_globs2}" "${_name}" _type)
      if(NOT _type STREQUAL "application/x-logcat")
        fail("the MIME database calls ${_name} '${_type}', not application/x-logcat")
      endif()
    endforeach()
    foreach(_name trace.adb12 trace.adbx)
      mime_type_of("${_globs2}" "${_name}" _type)
      if(_type STREQUAL "application/x-logcat")
        fail("the MIME database calls ${_name} a Logcat trace")
      endif()
    endforeach()
    foreach(_pair "run.out:text/x-program-output" "run.err:text/x-program-output" "x.trace:text/x-trace")
      string(REPLACE ":" ";" _pair "${_pair}")
      list(GET _pair 0 _name)
      list(GET _pair 1 _expected)
      mime_type_of("${_globs2}" "${_name}" _type)
      if(NOT _type STREQUAL _expected)
        fail("the MIME database calls ${_name} '${_type}', not ${_expected}")
      endif()
    endforeach()
    file(STRINGS "${_mime_dir}/generic-icons" _generic_icons)
    foreach(_type text/x-log application/x-logcat text/x-program-output text/x-trace)
      if(NOT "${_type}:logsquirl-document" IN_LIST _generic_icons)
        fail("the MIME database gives ${_type} not the document icon")
      endif()
    endforeach()
  endif()
else()
  message(STATUS "update-mime-database not found: the MIME package is not run through it")
endif()

find_program(DESKTOP_FILE_VALIDATE desktop-file-validate)
if(DESKTOP_FILE_VALIDATE)
  execute_process(
    COMMAND "${DESKTOP_FILE_VALIDATE}" "${WORK_DIR}/logsquirl.desktop"
    RESULT_VARIABLE _result
    OUTPUT_VARIABLE _output
    ERROR_VARIABLE _output
  )
  if(NOT _result EQUAL 0 OR _output MATCHES "error")
    fail("desktop-file-validate rejects the desktop entry: ${_output}")
  endif()
else()
  message(STATUS "desktop-file-validate not found: the desktop entry is not run through it")
endif()

# --- macOS (#718) ----------------------------------------------------------

set(MACOSX_BUNDLE_BUNDLE_DISPLAY_NAME "LogSquirl")
set(MACOSX_BUNDLE_EXECUTABLE_NAME "logsquirl")
set(MACOSX_BUNDLE_GUI_IDENTIFIER "io.github.logsquirl")
set(MACOSX_BUNDLE_ICON_FILE "logsquirl.icns")
set(CMAKE_OSX_DEPLOYMENT_TARGET "15.0")
configure_file("${PROJECT_DIR}/cmake/MacOSXBundleInfo.plist.in" "${WORK_DIR}/Info.plist")
file(READ "${WORK_DIR}/Info.plist" _plist)
string(REGEX REPLACE "[ \t\r\n]+" "" _plist_compact "${_plist}")
# The plain text Viewer entry stays, now with the document icon: it is the
# entry through which LogSquirl opens the types it declares itself.
expect_contains("${_plist_compact}" "<key>CFBundleTypeIconFile</key><string>logsquirl-document.icns</string><key>CFBundleTypeRole</key><string>Viewer</string><key>LSItemContentTypes</key><array><string>public.text</string></array>"
                "the bundle's plain text Viewer entry with the document icon"
)
foreach(_uti com.apple.log public.plain-text)
  expect_contains(
    "${_plist_compact}"
    "<key>CFBundleTypeIconFile</key><string>logsquirl-document.icns</string><key>CFBundleTypeRole</key><string>Viewer</string><key>LSHandlerRank</key><string>Alternate</string><key>LSItemContentTypes</key><array><string>${_uti}</string></array>"
    "the bundle views ${_uti} with the document icon, never the default by itself"
  )
endforeach()
foreach(_uti org.gnu.gnu-zip-archive public.zip-archive)
  expect_contains(
    "${_plist_compact}"
    "<key>CFBundleTypeRole</key><string>Viewer</string><key>LSHandlerRank</key><string>Alternate</string><key>LSItemContentTypes</key><array><string>${_uti}</string></array>"
    "the bundle offers ${_uti} with the Alternate role"
  )
endforeach()
# LaunchServices makes an application that names a type its default, whatever
# the rank, when no other one names it: the types LogSquirl declares itself
# have no document type entry of their own.
foreach(_uti io.github.logsquirl.logcat io.github.logsquirl.program-output io.github.logsquirl.trace)
  expect_not_contains("${_plist_compact}" "<array><string>${_uti}</string></array>" "the bundle names its own type in a document type")
endforeach()
expect_contains("${_plist_compact}" "<key>UTExportedTypeDeclarations</key><array><dict><key>UTTypeConformsTo</key><array><string>public.plain-text</string></array><key>UTTypeDescription</key><string>AndroidLogcattrace</string><key>UTTypeIconFile</key><string>logsquirl-document.icns</string><key>UTTypeIdentifier</key><string>io.github.logsquirl.logcat</string>"
                "the bundle exports the Logcat type"
)
expect_contains("${_plist_compact}" "<key>UTImportedTypeDeclarations</key><array><dict><key>UTTypeConformsTo</key><array><string>public.plain-text</string></array><key>UTTypeDescription</key><string>Programoutput</string><key>UTTypeIdentifier</key><string>io.github.logsquirl.program-output</string>"
                "the bundle imports the program output type, without LogSquirl's icon"
)
set(_tags "")
foreach(_extension IN LISTS _expected_logcat)
  string(APPEND _tags "<string>${_extension}</string>")
endforeach()
expect_contains("${_plist_compact}" "<key>public.filename-extension</key><array>${_tags}</array>" "the Logcat type's extensions, one by one")
string(FIND "${_plist_compact}" "$" _at)
if(NOT _at EQUAL -1)
  fail("the Info.plist has an unexpanded variable")
endif()

find_program(PLUTIL plutil)
if(PLUTIL)
  execute_process(
    COMMAND "${PLUTIL}" -lint "${WORK_DIR}/Info.plist"
    RESULT_VARIABLE _result
    OUTPUT_VARIABLE _output
    ERROR_VARIABLE _output
  )
  if(NOT _result EQUAL 0)
    fail("plutil rejects the Info.plist: ${_output}")
  endif()
else()
  message(STATUS "plutil not found: the Info.plist is not linted")
endif()

# --- Windows (#719) --------------------------------------------------------

file(READ "${WORK_DIR}/logsquirl_file_types.nsh" _nsh)
expect_contains("${_nsh}" "Section \"General log files (.log)\" FileType_log" "the installer offers .log, checked")
expect_contains("${_nsh}" "Section \"Android Logcat traces (.adb, .adb0-.adb9)\" FileType_logcat" "the installer offers the Logcat traces, checked")
expect_contains("${_nsh}" "Section /o \"Program output (.out, .err)\" FileType_output" "the installer offers .out and .err, unchecked")
expect_contains("${_nsh}" "Section /o \"Trace files (.trace)\" FileType_trace" "the installer offers .trace, unchecked")
expect_contains("${_nsh}" "Section /o \"Text files (.txt)\" FileType_text" "the installer offers .txt, unchecked")
expect_contains("${_nsh}" "!insertmacro LogSquirlSetDefault \".adb7\" \"LogSquirl.logcat\"" "the Logcat section sets .adb7")
foreach(_extension .log .adb .adb0 .adb9 .out .err .trace .txt .gz .zip)
  expect_contains("${_nsh}" "!insertmacro LogSquirlAddOpenWith \"${_extension}\"" "the installer offers LogSquirl under Open with")
  expect_contains("${_nsh}" "!insertmacro LogSquirlRemoveExtension \"${_extension}\"" "the uninstaller removes LogSquirl's entries")
  expect_contains("${_nsh}" "!insertmacro LogSquirlRemoveUserExtension \"${_extension}\"" "the uninstaller removes the uninstalling user's entries")
endforeach()
expect_contains("${_nsh}" "!insertmacro LogSquirlRemoveUserCapabilities" "the uninstaller removes the uninstalling user's registration as an application")
foreach(_extension .gz .zip)
  expect_not_contains("${_nsh}" "LogSquirlSetDefault \"${_extension}\"" "the installer makes LogSquirl the default for a compressed type")
endforeach()
foreach(_id IN LISTS LOGSQUIRL_FILE_TYPES LOGSQUIRL_OPEN_WITH_TYPES)
  expect_contains("${_nsh}" "!insertmacro LogSquirlRegisterProgId \"LogSquirl.${_id}\"" "the installer registers the ProgID")
  expect_contains("${_nsh}" "!insertmacro LogSquirlRemoveProgId \"LogSquirl.${_id}\"" "the uninstaller removes the ProgID")
  expect_contains("${_nsh}" "!insertmacro LogSquirlRemoveUserProgId \"LogSquirl.${_id}\"" "the uninstaller removes the uninstalling user's ProgID")
endforeach()

file(READ "${PROJECT_DIR}/packaging/windows/logsquirl.nsi" _nsi)
expect_contains("${_nsi}" "!include \"logsquirl_file_types.nsh\"" "the installer script")
expect_contains("${_nsi}" "!insertmacro LogSquirlFileTypeSections" "the installer script")
expect_contains("${_nsi}" "!insertmacro LogSquirlUnregisterFileTypes" "the installer script")
expect_not_contains("${_nsi}" "Associate with .log files" "the installer script still has the single .log box")
# Open with LogSquirl for every file (#724), added by a section, removed on
# uninstall.
expect_contains("${_nsi}" "!insertmacro LogSquirlAddContextMenu" "the installer adds the context menu entry")
expect_contains("${_nsi}" "!insertmacro LogSquirlRemoveContextMenu" "the uninstaller removes the context menu entry")
file(READ "${PROJECT_DIR}/packaging/windows/prepare_release.cmd" _prepare)
expect_contains("${_prepare}" "logsquirl_file_types.nsh" "prepare_release.cmd copies the generated file types")
expect_contains("${_prepare}" "FileTypes.nsh" "prepare_release.cmd copies the file type macros")

find_program(MAKENSIS makensis)
if(MAKENSIS)
  # The generated sections and the macros behind them, compiled into an
  # installer of their own: makensis is the only compiler they have.
  file(TO_NATIVE_PATH "${PROJECT_DIR}/packaging/windows/FileTypes.nsh" _macros)
  file(WRITE "${WORK_DIR}/file_types_test.nsi"
       "Unicode true\n"
       "OutFile \"file_types_test.exe\"\n"
       "InstallDir \"$PROGRAMFILES64\\logsquirl\"\n"
       "RequestExecutionLevel admin\n"
       "!include \"LogicLib.nsh\"\n"
       "!include \"${_macros}\"\n"
       "!include \"logsquirl_file_types.nsh\"\n"
       "Section \"-core\"\n"
       "  !insertmacro LogSquirlRegisterFileTypes\n"
       "  WriteUninstaller \"$INSTDIR\\Uninstall.exe\"\n"
       "SectionEnd\n"
       "!insertmacro LogSquirlFileTypeSections\n"
       "Section \"Open with LogSquirl\" contextmenu\n"
       "  !insertmacro LogSquirlAddContextMenu\n"
       "SectionEnd\n"
       "Section \"-after\"\n"
       "  !insertmacro LogSquirlFileTypesChanged\n"
       "SectionEnd\n"
       "Section \"Uninstall\"\n"
       "  !insertmacro LogSquirlUnregisterFileTypes\n"
       "  !insertmacro LogSquirlRemoveContextMenu\n"
       "SectionEnd\n"
  )
  execute_process(
    COMMAND "${MAKENSIS}" -V2 -WX file_types_test.nsi
    WORKING_DIRECTORY "${WORK_DIR}"
    RESULT_VARIABLE _result
    OUTPUT_VARIABLE _output
    ERROR_VARIABLE _output
  )
  if(NOT _result EQUAL 0)
    fail("makensis does not compile the file types: ${_output}")
  endif()
else()
  message(STATUS "makensis not found: the installer's file types are not compiled")
endif()

if(_failures)
  list(JOIN _failures "\n  " _report)
  message(FATAL_ERROR "The file types are not declared as each platform needs:\n  ${_report}")
endif()
message(STATUS "The file types are declared as each platform needs.")
