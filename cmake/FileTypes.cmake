# The file types LogSquirl opens, declared once for every platform (#717,
# #718, #719). The packaging of each platform is generated from this list:
#
#   - Linux: the shared-mime-info package logsquirl.xml and the MimeType line
#     of the desktop entry, which the deb and the rpm install;
#   - macOS: the document types of the app bundle's Info.plist;
#   - Windows: the file type page of the installer and its ProgIDs.
#
# The File Associations page, the first-start dialog and the lost-association
# hint take the same list, so the ids, the groups and the ProgIDs here are the
# ones the application uses too: an installer and an application that register
# a type both use LogSquirl.<id>, and never register it twice.
#
# A file type is one choice for the user, with one or more extensions:
#
#   logsquirl_file_type(<id>
#     [CHECKED]                  offered checked; the optional ones are not
#     [OPEN_WITH_ONLY]           only ever offered under "Open with", never made
#                                the default: a type another application owns
#     GROUP <logs|optional>      "Log files" or "More (optional)"
#     LABEL <text>               what the choice says ("Android Logcat traces")
#     SHOWN_AS <text>            its extensions as the choice shows them
#     NAME <text>                what a file of the type is called (Explorer's
#                                type column, Finder's kind, the MIME comment)
#     EXTENSIONS <ext>...        without the dot
#     MIME <type>                its Linux MIME type
#     [MIME_DEFINED]             LogSquirl's MIME package defines the type,
#                                with a glob per extension
#     [MIME_GLOB_WEIGHT <n>]     the weight of those globs (default 50)
#     [DOCUMENT_ICON]            the type shows the document icon on Linux: on
#                                macOS and Windows a type has it once LogSquirl
#                                opens it, on Linux the icon belongs to the MIME
#                                type, so plain text keeps its own
#     UTI <identifier>           its macOS uniform type identifier
#     [UTI_DECLARATION <EXPORTED|IMPORTED>]
#                                the bundle declares the type: EXPORTED when it
#                                is LogSquirl's own, IMPORTED when it is only
#                                known nowhere else; none for a system type
#   )
#
# It sets LOGSQUIRL_FILE_TYPE_<id>_<FIELD> for each field above, plus _PROGID,
# and appends <id> to LOGSQUIRL_FILE_TYPES or LOGSQUIRL_OPEN_WITH_TYPES.

set(LOGSQUIRL_FILE_TYPES "")
set(LOGSQUIRL_OPEN_WITH_TYPES "")
set(LOGSQUIRL_FILE_TYPE_FIELDS
    CHECKED
    OPEN_WITH_ONLY
    GROUP
    LABEL
    SHOWN_AS
    NAME
    EXTENSIONS
    MIME
    MIME_DEFINED
    MIME_GLOB_WEIGHT
    DOCUMENT_ICON
    UTI
    UTI_DECLARATION
)
# The icon name of the document icon, in the hicolor theme on Linux, and the
# file name of the macOS one in the bundle's Resources.
set(LOGSQUIRL_DOCUMENT_ICON_NAME logsquirl-document)

function(logsquirl_file_type id)
  cmake_parse_arguments(
    FT
    "CHECKED;OPEN_WITH_ONLY;MIME_DEFINED;DOCUMENT_ICON"
    "GROUP;LABEL;SHOWN_AS;NAME;MIME;MIME_GLOB_WEIGHT;UTI;UTI_DECLARATION"
    "EXTENSIONS"
    ${ARGN}
  )
  foreach(_field IN LISTS LOGSQUIRL_FILE_TYPE_FIELDS)
    set(LOGSQUIRL_FILE_TYPE_${id}_${_field}
        "${FT_${_field}}"
        PARENT_SCOPE
    )
  endforeach()
  set(LOGSQUIRL_FILE_TYPE_${id}_PROGID
      "LogSquirl.${id}"
      PARENT_SCOPE
  )
  if(FT_OPEN_WITH_ONLY)
    list(APPEND LOGSQUIRL_OPEN_WITH_TYPES ${id})
    set(LOGSQUIRL_OPEN_WITH_TYPES
        "${LOGSQUIRL_OPEN_WITH_TYPES}"
        PARENT_SCOPE
    )
  else()
    list(APPEND LOGSQUIRL_FILE_TYPES ${id})
    set(LOGSQUIRL_FILE_TYPES
        "${LOGSQUIRL_FILE_TYPES}"
        PARENT_SCOPE
    )
  endif()
endfunction()

# --- the list ----------------------------------------------------------------

# Log files
logsquirl_file_type(
  log
  CHECKED
  GROUP logs
  LABEL "General log files"
  SHOWN_AS ".log"
  NAME "Log file"
  EXTENSIONS log
  MIME text/x-log
  DOCUMENT_ICON
  UTI com.apple.log
)
# freedesktop.org.xml gives *.adb to Ada source at the default weight 50; the
# higher weight makes an .adb file a Logcat trace once LogSquirl is installed.
# One glob per extension rather than *.adb[0-9]: it matches the same names and
# stays a literal suffix, which the MIME database looks up fastest. macOS keeps
# calling an .adb file Ada source (public.ada-source) whatever an application
# exports; .adb0 to .adb9 become the Logcat type.
logsquirl_file_type(
  logcat
  CHECKED
  GROUP logs
  LABEL "Android Logcat traces"
  SHOWN_AS ".adb, .adb0-.adb9"
  NAME "Android Logcat trace"
  EXTENSIONS adb adb0 adb1 adb2 adb3 adb4 adb5 adb6 adb7 adb8 adb9
  MIME application/x-logcat
  MIME_DEFINED
  MIME_GLOB_WEIGHT 60
  DOCUMENT_ICON
  UTI io.github.logsquirl.logcat
  UTI_DECLARATION EXPORTED
)

# More (optional)
logsquirl_file_type(
  output
  GROUP optional
  LABEL "Program output"
  SHOWN_AS ".out, .err"
  NAME "Program output"
  EXTENSIONS out err
  MIME text/x-program-output
  MIME_DEFINED
  DOCUMENT_ICON
  UTI io.github.logsquirl.program-output
  UTI_DECLARATION IMPORTED
)
logsquirl_file_type(
  trace
  GROUP optional
  LABEL "Trace files"
  SHOWN_AS ".trace"
  NAME "Trace file"
  EXTENSIONS trace
  MIME text/x-trace
  MIME_DEFINED
  DOCUMENT_ICON
  UTI io.github.logsquirl.trace
  UTI_DECLARATION IMPORTED
)
logsquirl_file_type(
  text
  GROUP optional
  LABEL "Text files"
  SHOWN_AS ".txt"
  NAME "Text file"
  EXTENSIONS txt
  MIME text/plain
  UTI public.plain-text
)

# The compressed files LogSquirl opens: offered under "Open with" only, the
# archive tool keeps them.
logsquirl_file_type(
  gz
  OPEN_WITH_ONLY
  NAME "Gzip-compressed file"
  EXTENSIONS gz
  MIME application/gzip
  UTI org.gnu.gnu-zip-archive
)
logsquirl_file_type(
  zip
  OPEN_WITH_ONLY
  NAME "Zip archive"
  EXTENSIONS zip
  MIME application/zip
  UTI public.zip-archive
)

# --- Linux -------------------------------------------------------------------

# The shared-mime-info package: the types LogSquirl defines, and the document
# icon for every type that shows it. The icon is the generic icon of the type,
# so the lookup is <type with dashes>, then logsquirl-document: an icon theme
# that draws the type itself still wins, and one that only has a generic text
# icon, as Adwaita does, no longer hides LogSquirl's.
function(logsquirl_file_types_mime_xml out_var)
  set(_xml "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n")
  string(APPEND _xml "<!-- Generated from cmake/FileTypes.cmake: the file types LogSquirl opens (#717). -->\n")
  string(APPEND _xml "<mime-info xmlns=\"http://www.freedesktop.org/standards/shared-mime-info\">\n")
  foreach(_id IN LISTS LOGSQUIRL_FILE_TYPES)
    if(NOT LOGSQUIRL_FILE_TYPE_${_id}_MIME_DEFINED AND NOT LOGSQUIRL_FILE_TYPE_${_id}_DOCUMENT_ICON)
      continue()
    endif()
    string(APPEND _xml "  <mime-type type=\"${LOGSQUIRL_FILE_TYPE_${_id}_MIME}\">\n")
    if(LOGSQUIRL_FILE_TYPE_${_id}_MIME_DEFINED)
      string(APPEND _xml "    <comment>${LOGSQUIRL_FILE_TYPE_${_id}_NAME}</comment>\n")
      string(APPEND _xml "    <sub-class-of type=\"text/plain\"/>\n")
    endif()
    if(LOGSQUIRL_FILE_TYPE_${_id}_DOCUMENT_ICON)
      string(APPEND _xml "    <generic-icon name=\"${LOGSQUIRL_DOCUMENT_ICON_NAME}\"/>\n")
    endif()
    if(LOGSQUIRL_FILE_TYPE_${_id}_MIME_DEFINED)
      set(_weight "")
      if(LOGSQUIRL_FILE_TYPE_${_id}_MIME_GLOB_WEIGHT)
        set(_weight " weight=\"${LOGSQUIRL_FILE_TYPE_${_id}_MIME_GLOB_WEIGHT}\"")
      endif()
      foreach(_extension IN LISTS LOGSQUIRL_FILE_TYPE_${_id}_EXTENSIONS)
        string(APPEND _xml "    <glob pattern=\"*.${_extension}\"${_weight}/>\n")
      endforeach()
    endif()
    string(APPEND _xml "  </mime-type>\n")
  endforeach()
  string(APPEND _xml "</mime-info>\n")
  set(${out_var}
      "${_xml}"
      PARENT_SCOPE
  )
endfunction()

# The MimeType value of the desktop entry: every type LogSquirl opens, so it is
# offered under "Open with" for each.
function(logsquirl_file_types_desktop_mime_types out_var)
  set(_types "")
  foreach(_id IN LISTS LOGSQUIRL_FILE_TYPES LOGSQUIRL_OPEN_WITH_TYPES)
    list(APPEND _types "${LOGSQUIRL_FILE_TYPE_${_id}_MIME}")
  endforeach()
  list(REMOVE_DUPLICATES _types)
  list(JOIN _types ";" _value)
  set(${out_var}
      "${_value};"
      PARENT_SCOPE
  )
endfunction()

# --- Windows -----------------------------------------------------------------

# The installer's file types (packaging/windows/logsquirl.nsi), as NSIS macros
# over packaging/windows/FileTypes.nsh:
#   LogSquirlRegisterFileTypes        every ProgID and every Open with entry
#   LogSquirlFileTypeSections         a section per type for the file type
#                                     page, checked as the list says; a checked
#                                     section makes LogSquirl the default
#   LogSquirlShowFileTypeSections     shows them (the file type page)
#   LogSquirlHideFileTypeSections     hides them (the components page)
#   LogSquirlFileTypeDescriptions     their descriptions on the page
#   LogSquirlUnregisterFileTypes      removes all of it again
# The compressed types only get the ProgID and the Open with entry.
function(logsquirl_file_types_nsis out_var)
  set(_register "")
  set(_sections "")
  set(_show "")
  set(_hide "")
  set(_descriptions "")
  set(_unregister "")
  foreach(_id IN LISTS LOGSQUIRL_FILE_TYPES LOGSQUIRL_OPEN_WITH_TYPES)
    set(_progid "${LOGSQUIRL_FILE_TYPE_${_id}_PROGID}")
    string(APPEND _register "    !insertmacro LogSquirlRegisterProgId \"${_progid}\" \"${LOGSQUIRL_FILE_TYPE_${_id}_NAME}\"\n")
    foreach(_extension IN LISTS LOGSQUIRL_FILE_TYPE_${_id}_EXTENSIONS)
      string(APPEND _register "    !insertmacro LogSquirlAddOpenWith \".${_extension}\" \"${_progid}\"\n")
      string(APPEND _unregister "    !insertmacro LogSquirlRemoveExtension \".${_extension}\" \"${_progid}\"\n")
    endforeach()
    string(APPEND _unregister "    !insertmacro LogSquirlRemoveProgId \"${_progid}\"\n")
    if(LOGSQUIRL_FILE_TYPE_${_id}_OPEN_WITH_ONLY)
      continue()
    endif()

    set(_text "${LOGSQUIRL_FILE_TYPE_${_id}_LABEL} (${LOGSQUIRL_FILE_TYPE_${_id}_SHOWN_AS})")
    set(_flags "")
    if(NOT LOGSQUIRL_FILE_TYPE_${_id}_CHECKED)
      set(_flags "/o ")
    endif()
    string(APPEND _sections "    Section ${_flags}\"${_text}\" FileType_${_id}\n")
    foreach(_extension IN LISTS LOGSQUIRL_FILE_TYPE_${_id}_EXTENSIONS)
      string(APPEND _sections "        !insertmacro LogSquirlSetDefault \".${_extension}\" \"${_progid}\"\n")
    endforeach()
    string(APPEND _sections "    SectionEnd\n")
    string(APPEND _show "    SectionSetText \${FileType_${_id}} \"${_text}\"\n")
    string(APPEND _hide "    SectionSetText \${FileType_${_id}} \"\"\n")
    string(APPEND _descriptions
           "    !insertmacro MUI_DESCRIPTION_TEXT \${FileType_${_id}} \"Double-clicking a ${LOGSQUIRL_FILE_TYPE_${_id}_SHOWN_AS} file opens it in LogSquirl, and the file shows the LogSquirl document icon.\"\n"
    )
  endforeach()

  set(_nsh "; Generated from cmake/FileTypes.cmake: the file types LogSquirl opens, for\n")
  string(APPEND _nsh "; packaging/windows/logsquirl.nsi, with the macros of FileTypes.nsh (#719).\n\n")
  string(APPEND _nsh "!macro LogSquirlRegisterFileTypes\n${_register}!macroend\n\n")
  string(APPEND _nsh "!macro LogSquirlFileTypeSections\n${_sections}!macroend\n\n")
  string(APPEND _nsh "!macro LogSquirlShowFileTypeSections\n${_show}!macroend\n\n")
  string(APPEND _nsh "!macro LogSquirlHideFileTypeSections\n${_hide}!macroend\n\n")
  string(APPEND _nsh "!macro LogSquirlFileTypeDescriptions\n${_descriptions}!macroend\n\n")
  string(APPEND _nsh "!macro LogSquirlUnregisterFileTypes\n${_unregister}")
  string(APPEND _nsh "    !insertmacro LogSquirlRemoveFileTypesBackup\n")
  string(APPEND _nsh "    !insertmacro LogSquirlFileTypesChanged\n!macroend\n")
  set(${out_var}
      "${_nsh}"
      PARENT_SCOPE
  )
endfunction()

# --- the application ---------------------------------------------------------

# The list as the application reads it (src/fileassociations, #720): one
# LOGSQUIRL_FILE_TYPE(...) line per type, the types the user chooses first, in
# the order above, then the ones only offered under "Open with".
function(logsquirl_file_types_cpp out_var)
  set(_cpp "// Generated from cmake/FileTypes.cmake: the file types LogSquirl opens (#720).\n")
  string(APPEND _cpp "// LOGSQUIRL_FILE_TYPE( id, group, checked, label, shownAs, name, extensions,\n")
  string(APPEND _cpp "//                      mimeType, uti, progId )\n")
  foreach(_id IN LISTS LOGSQUIRL_FILE_TYPES LOGSQUIRL_OPEN_WITH_TYPES)
    if(LOGSQUIRL_FILE_TYPE_${_id}_OPEN_WITH_ONLY)
      set(_group OpenWith)
    elseif(LOGSQUIRL_FILE_TYPE_${_id}_GROUP STREQUAL "logs")
      set(_group Logs)
    elseif(LOGSQUIRL_FILE_TYPE_${_id}_GROUP STREQUAL "optional")
      set(_group Optional)
    else()
      message(FATAL_ERROR "The file type ${_id} has GROUP '${LOGSQUIRL_FILE_TYPE_${_id}_GROUP}', not logs or optional")
    endif()
    if(LOGSQUIRL_FILE_TYPE_${_id}_CHECKED)
      set(_checked true)
    else()
      set(_checked false)
    endif()
    list(JOIN LOGSQUIRL_FILE_TYPE_${_id}_EXTENSIONS " " _extensions)
    string(APPEND _cpp "LOGSQUIRL_FILE_TYPE( \"${_id}\", ${_group}, ${_checked}, ")
    string(APPEND _cpp "\"${LOGSQUIRL_FILE_TYPE_${_id}_LABEL}\", \"${LOGSQUIRL_FILE_TYPE_${_id}_SHOWN_AS}\", ")
    string(APPEND _cpp "\"${LOGSQUIRL_FILE_TYPE_${_id}_NAME}\", \"${_extensions}\", ")
    string(APPEND _cpp "\"${LOGSQUIRL_FILE_TYPE_${_id}_MIME}\", \"${LOGSQUIRL_FILE_TYPE_${_id}_UTI}\", ")
    string(APPEND _cpp "\"${LOGSQUIRL_FILE_TYPE_${_id}_PROGID}\" )\n")
  endforeach()
  set(${out_var}
      "${_cpp}"
      PARENT_SCOPE
  )
endfunction()

# --- the generated files ----------------------------------------------------

# Writes <content> to <file> only when it changed, so nothing that depends on
# the file is rebuilt by a configure run that changed nothing.
function(logsquirl_write_if_changed file content)
  file(WRITE "${file}.new" "${content}")
  configure_file("${file}.new" "${file}" COPYONLY)
  file(REMOVE "${file}.new")
endfunction()

# Generates the packaging files of every platform into <dir>:
#   logsquirl.xml       the shared-mime-info package (Linux)
#   logsquirl.desktop   the desktop entry, from DESKTOP_TEMPLATE (Linux)
#   postinst, postrm    the deb's and the rpm's scripts that refresh the MIME
#                       database, the icon cache and the desktop database
#   logsquirl_file_types.nsh
#                       the installer's file types (Windows), which
#                       packaging/windows/prepare_release.cmd copies beside
#                       logsquirl.nsi
#   file_types.inc      the list the application reads (#720)
# The desktop entry lists the file types only with FILE_TYPES (the default);
# the AppImage, which has no install step to register types, is built without.
function(logsquirl_generate_file_types dir)
  cmake_parse_arguments(GEN "" "DESKTOP_TEMPLATE;FILE_TYPES" "" ${ARGN})
  if(NOT DEFINED GEN_FILE_TYPES)
    set(GEN_FILE_TYPES ON)
  endif()
  file(MAKE_DIRECTORY "${dir}")

  logsquirl_file_types_mime_xml(_mime_xml)
  logsquirl_write_if_changed("${dir}/logsquirl.xml" "${_mime_xml}")

  logsquirl_file_types_nsis(_nsh)
  logsquirl_write_if_changed("${dir}/logsquirl_file_types.nsh" "${_nsh}")

  logsquirl_file_types_cpp(_cpp)
  logsquirl_write_if_changed("${dir}/file_types.inc" "${_cpp}")

  if(GEN_FILE_TYPES)
    logsquirl_file_types_desktop_mime_types(LOGSQUIRL_DESKTOP_MIME_TYPES)
  else()
    set(LOGSQUIRL_DESKTOP_MIME_TYPES "text/plain;")
  endif()
  configure_file("${GEN_DESKTOP_TEMPLATE}" "${dir}/logsquirl.desktop" @ONLY)

  set(_script "#!/bin/sh\n")
  string(APPEND _script "# Generated from cmake/FileTypes.cmake: after LogSquirl's file types, its\n")
  string(APPEND _script "# document icon or its desktop entry came or went, the caches that hold them\n")
  string(APPEND _script "# are refreshed (#717). A distribution whose packages do this with triggers\n")
  string(APPEND _script "# refreshes them once more; one without the tools skips them.\n")
  string(APPEND _script "if command -v update-mime-database >/dev/null 2>&1; then\n")
  string(APPEND _script "    update-mime-database /usr/share/mime || true\n")
  string(APPEND _script "fi\n")
  string(APPEND _script "if command -v gtk-update-icon-cache >/dev/null 2>&1 && [ -f /usr/share/icons/hicolor/index.theme ]; then\n")
  string(APPEND _script "    gtk-update-icon-cache -q -t -f /usr/share/icons/hicolor || true\n")
  string(APPEND _script "fi\n")
  string(APPEND _script "if command -v update-desktop-database >/dev/null 2>&1; then\n")
  string(APPEND _script "    update-desktop-database -q /usr/share/applications || true\n")
  string(APPEND _script "fi\n")
  string(APPEND _script "exit 0\n")
  foreach(_name postinst postrm)
    logsquirl_write_if_changed("${dir}/package-scripts/${_name}" "${_script}")
    # dpkg runs the scripts it finds in the package; they must be executable.
    file(
      COPY "${dir}/package-scripts/${_name}"
      DESTINATION "${dir}"
      FILE_PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE GROUP_READ GROUP_EXECUTE WORLD_READ WORLD_EXECUTE
    )
  endforeach()
endfunction()

# --- macOS -------------------------------------------------------------------

# The UTExportedTypeDeclarations or UTImportedTypeDeclarations entry of <id>.
# Only LogSquirl's own type carries the document icon: the imported ones are
# no more LogSquirl's than TextEdit's.
function(logsquirl_file_type_bundle_declaration id out_var)
  set(_type "")
  string(APPEND _type "        <dict>\n")
  string(APPEND _type "            <key>UTTypeConformsTo</key>\n")
  string(APPEND _type "            <array>\n")
  string(APPEND _type "                <string>public.plain-text</string>\n")
  string(APPEND _type "            </array>\n")
  string(APPEND _type "            <key>UTTypeDescription</key>\n")
  string(APPEND _type "            <string>${LOGSQUIRL_FILE_TYPE_${id}_NAME}</string>\n")
  if(LOGSQUIRL_FILE_TYPE_${id}_UTI_DECLARATION STREQUAL "EXPORTED")
    string(APPEND _type "            <key>UTTypeIconFile</key>\n")
    string(APPEND _type "            <string>${LOGSQUIRL_DOCUMENT_ICON_NAME}.icns</string>\n")
  endif()
  string(APPEND _type "            <key>UTTypeIdentifier</key>\n")
  string(APPEND _type "            <string>${LOGSQUIRL_FILE_TYPE_${id}_UTI}</string>\n")
  string(APPEND _type "            <key>UTTypeTagSpecification</key>\n")
  string(APPEND _type "            <dict>\n")
  string(APPEND _type "                <key>public.filename-extension</key>\n")
  string(APPEND _type "                <array>\n")
  foreach(_extension IN LISTS LOGSQUIRL_FILE_TYPE_${id}_EXTENSIONS)
    string(APPEND _type "                    <string>${_extension}</string>\n")
  endforeach()
  string(APPEND _type "                </array>\n")
  string(APPEND _type "            </dict>\n")
  string(APPEND _type "        </dict>\n")
  set(${out_var}
      "${_type}"
      PARENT_SCOPE
  )
endfunction()

# The document types of the app bundle's Info.plist (cmake/MacOSXBundleInfo.
# plist.in), as three fragments:
#   LOGSQUIRL_BUNDLE_DOCUMENT_TYPES  an entry per system type LogSquirl views:
#                                    the log type and plain text with the
#                                    document icon, the compressed ones without
#   LOGSQUIRL_BUNDLE_EXPORTED_TYPES  the types LogSquirl owns (the Logcat trace),
#                                    with the document icon
#   LOGSQUIRL_BUNDLE_IMPORTED_TYPES  the types nobody declares (.out, .trace)
# Installing LogSquirl never makes it the default by itself (#718): every entry
# has the Alternate rank, which leaves a system type with the application that
# opens it now. LaunchServices prefers an application that names a type over
# one that opens it as plain text, whatever the rank, so the types LogSquirl
# declares itself get no entry: an entry would make LogSquirl the default for
# them. All of them conform to plain text, and the bundle's public.text entry,
# which has the document icon too, offers LogSquirl under "Open with" for them
# and shows the icon once the user chose it with "Change All". macOS has no
# wildcard extensions, so the Logcat type lists .adb0 to .adb9 one by one.
function(logsquirl_file_types_bundle_plist)
  set(_icon "${LOGSQUIRL_DOCUMENT_ICON_NAME}.icns")
  set(_documents "")
  set(_exported "")
  set(_imported "")
  foreach(_id IN LISTS LOGSQUIRL_FILE_TYPES LOGSQUIRL_OPEN_WITH_TYPES)
    set(_uti "${LOGSQUIRL_FILE_TYPE_${_id}_UTI}")
    set(_declaration "${LOGSQUIRL_FILE_TYPE_${_id}_UTI_DECLARATION}")
    if(_declaration)
      logsquirl_file_type_bundle_declaration(${_id} _type)
      if(_declaration STREQUAL "EXPORTED")
        string(APPEND _exported "${_type}")
      elseif(_declaration STREQUAL "IMPORTED")
        string(APPEND _imported "${_type}")
      else()
        message(FATAL_ERROR "The file type ${_id} has UTI_DECLARATION '${_declaration}', not EXPORTED or IMPORTED")
      endif()
      continue()
    endif()
    string(APPEND _documents "        <dict>\n")
    string(APPEND _documents "            <key>CFBundleTypeName</key>\n")
    string(APPEND _documents "            <string>${LOGSQUIRL_FILE_TYPE_${_id}_NAME}</string>\n")
    if(NOT LOGSQUIRL_FILE_TYPE_${_id}_OPEN_WITH_ONLY)
      string(APPEND _documents "            <key>CFBundleTypeIconFile</key>\n")
      string(APPEND _documents "            <string>${_icon}</string>\n")
    endif()
    string(APPEND _documents "            <key>CFBundleTypeRole</key>\n")
    string(APPEND _documents "            <string>Viewer</string>\n")
    string(APPEND _documents "            <key>LSHandlerRank</key>\n")
    string(APPEND _documents "            <string>Alternate</string>\n")
    string(APPEND _documents "            <key>LSItemContentTypes</key>\n")
    string(APPEND _documents "            <array>\n")
    string(APPEND _documents "                <string>${_uti}</string>\n")
    string(APPEND _documents "            </array>\n")
    string(APPEND _documents "        </dict>\n")
  endforeach()
  set(LOGSQUIRL_BUNDLE_DOCUMENT_TYPES
      "${_documents}"
      PARENT_SCOPE
  )
  set(LOGSQUIRL_BUNDLE_EXPORTED_TYPES
      "${_exported}"
      PARENT_SCOPE
  )
  set(LOGSQUIRL_BUNDLE_IMPORTED_TYPES
      "${_imported}"
      PARENT_SCOPE
  )
endfunction()
logsquirl_file_types_bundle_plist()
