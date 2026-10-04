; The registry entries of the file types LogSquirl opens (#719). The types
; themselves, their ProgIDs and the installer's sections are generated from
; cmake/FileTypes.cmake into logsquirl_file_types.nsh, which uses these macros.
;
; Every type gets a ProgID, LogSquirl.<id>, whose icon is the document icon
; (the second icon of logsquirl.exe), and LogSquirl is listed under each of its
; extensions' OpenWithProgids, so Explorer offers it under "Open with". For a
; type the user checks, the installer also makes the ProgID the extension's
; default, as far as Windows lets an installer: a choice the user made in
; Explorer (UserChoice) still wins. The application registers the same ProgIDs
; for the current user, so a type is never registered twice under two names.
;
; The uninstaller removes the ProgIDs and LogSquirl's OpenWithProgids entries,
; and gives an extension back to the ProgID it had before the installer made
; LogSquirl its default. An extension another application took in the
; meantime keeps that application. It removes the uninstalling user's own
; registration too, which the File Associations page wrote (#722), where it
; opens this installation's logsquirl.exe; a portable LogSquirl's stays.
; Other users' registrations are in their own hives, out of its reach.
;
; Everything goes into HKLM\Software\Classes: the installer runs as an
; administrator for all users of the machine.

!ifndef LOGSQUIRL_FILE_TYPES_NSH
!define LOGSQUIRL_FILE_TYPES_NSH

!include "LogicLib.nsh"

; Where the installer keeps the ProgID an extension had before LogSquirl was
; made its default.
!define LOGSQUIRL_FILE_TYPES_BACKUP "Software\logsquirl\FileTypes"

; Registers the ProgID of a type.
!macro LogSquirlRegisterProgId _PROGID _NAME
    WriteRegStr HKLM "Software\Classes\${_PROGID}" "" "${_NAME}"
    WriteRegStr HKLM "Software\Classes\${_PROGID}\DefaultIcon" "" "$INSTDIR\logsquirl.exe,1"
    WriteRegStr HKLM "Software\Classes\${_PROGID}\shell\open\command" "" '"$INSTDIR\logsquirl.exe" "%1"'
!macroend

; Offers LogSquirl under "Open with" for an extension.
!macro LogSquirlAddOpenWith _EXT _PROGID
    WriteRegStr HKLM "Software\Classes\${_EXT}\OpenWithProgids" "${_PROGID}" ""
!macroend

; Makes LogSquirl the default for an extension, keeping the ProgID it had.
!macro LogSquirlSetDefault _EXT _PROGID
    Push $R0
    ReadRegStr $R0 HKLM "Software\Classes\${_EXT}" ""
    ${If} $R0 != "${_PROGID}"
        ${If} $R0 != ""
            WriteRegStr HKLM "${LOGSQUIRL_FILE_TYPES_BACKUP}" "${_EXT}" "$R0"
        ${EndIf}
        WriteRegStr HKLM "Software\Classes\${_EXT}" "" "${_PROGID}"
    ${EndIf}
    Pop $R0
!macroend

; Deletes a key under a root key, HKLM or HKCU, that holds nothing: no subkey
; and no value. Not DeleteRegKey /ifempty, which looks at the subkeys only and
; would take an extension's other values with it. EnumRegValue names the
; default value "", the same as the end of the list, so a key with a default
; value is kept, and a second value is looked for.
!macro LogSquirlDeleteKeyIfEmptyIn _ROOT _KEY
    Push $R1
    EnumRegKey $R1 ${_ROOT} "${_KEY}" 0
    ${If} $R1 == ""
        ReadRegStr $R1 ${_ROOT} "${_KEY}" ""
        ${If} $R1 == ""
            EnumRegValue $R1 ${_ROOT} "${_KEY}" 0
            ${If} $R1 == ""
                EnumRegValue $R1 ${_ROOT} "${_KEY}" 1
                ${If} $R1 == ""
                    DeleteRegKey ${_ROOT} "${_KEY}"
                ${EndIf}
            ${EndIf}
        ${EndIf}
    ${EndIf}
    Pop $R1
!macroend

; The same under HKLM.
!macro LogSquirlDeleteKeyIfEmpty _KEY
    !insertmacro LogSquirlDeleteKeyIfEmptyIn HKLM "${_KEY}"
!macroend

; Removes LogSquirl's entries of an extension, and gives the extension back if
; LogSquirl is still its default.
!macro LogSquirlRemoveExtension _EXT _PROGID
    Push $R0
    DeleteRegValue HKLM "Software\Classes\${_EXT}\OpenWithProgids" "${_PROGID}"
    !insertmacro LogSquirlDeleteKeyIfEmpty "Software\Classes\${_EXT}\OpenWithProgids"
    ReadRegStr $R0 HKLM "Software\Classes\${_EXT}" ""
    ${If} $R0 == "${_PROGID}"
        ReadRegStr $R0 HKLM "${LOGSQUIRL_FILE_TYPES_BACKUP}" "${_EXT}"
        ${If} $R0 != ""
            WriteRegStr HKLM "Software\Classes\${_EXT}" "" "$R0"
        ${Else}
            DeleteRegValue HKLM "Software\Classes\${_EXT}" ""
        ${EndIf}
    ${EndIf}
    !insertmacro LogSquirlDeleteKeyIfEmpty "Software\Classes\${_EXT}"
    Pop $R0
!macroend

; Removes the ProgID of a type.
!macro LogSquirlRemoveProgId _PROGID
    DeleteRegKey HKLM "Software\Classes\${_PROGID}"
!macroend

; Where the File Associations page registers LogSquirl as an application for
; the current user (#722), which RegisteredApplications names.
!define LOGSQUIRL_USER_CAPABILITIES "Software\LogSquirl\Capabilities"

; Removes the uninstalling user's entries of an extension, as the File
; Associations page wrote them, when the user's ProgID opens this
; installation's logsquirl.exe. Before LogSquirlRemoveUserProgId, which takes
; the ProgID this looks at.
!macro LogSquirlRemoveUserExtension _EXT _PROGID
    Push $R0
    ReadRegStr $R0 HKCU "Software\Classes\${_PROGID}\shell\open\command" ""
    ${If} $R0 == '"$INSTDIR\logsquirl.exe" "%1"'
        DeleteRegValue HKCU "Software\Classes\${_EXT}\OpenWithProgids" "${_PROGID}"
        !insertmacro LogSquirlDeleteKeyIfEmptyIn HKCU "Software\Classes\${_EXT}\OpenWithProgids"
        !insertmacro LogSquirlDeleteKeyIfEmptyIn HKCU "Software\Classes\${_EXT}"
        DeleteRegValue HKCU "${LOGSQUIRL_USER_CAPABILITIES}\FileAssociations" "${_EXT}"
    ${EndIf}
    Pop $R0
!macroend

; Removes the uninstalling user's ProgID of a type when it opens this
; installation's logsquirl.exe.
!macro LogSquirlRemoveUserProgId _PROGID
    Push $R0
    ReadRegStr $R0 HKCU "Software\Classes\${_PROGID}\shell\open\command" ""
    ${If} $R0 == '"$INSTDIR\logsquirl.exe" "%1"'
        DeleteRegKey HKCU "Software\Classes\${_PROGID}"
    ${EndIf}
    Pop $R0
!macroend

; Removes the uninstalling user's registration of LogSquirl as an application
; once no type is left in it, after LogSquirlRemoveUserExtension: a portable
; LogSquirl's types keep it.
!macro LogSquirlRemoveUserCapabilities
    Push $R0
    EnumRegValue $R0 HKCU "${LOGSQUIRL_USER_CAPABILITIES}\FileAssociations" 0
    ${If} $R0 == ""
        DeleteRegKey HKCU "${LOGSQUIRL_USER_CAPABILITIES}"
        DeleteRegValue HKCU "Software\RegisteredApplications" "LogSquirl"
        !insertmacro LogSquirlDeleteKeyIfEmptyIn HKCU "Software\LogSquirl"
    ${EndIf}
    Pop $R0
!macroend

; Removes what the installer kept about the extensions, after every extension
; was given back.
!macro LogSquirlRemoveFileTypesBackup
    DeleteRegKey HKLM "${LOGSQUIRL_FILE_TYPES_BACKUP}"
    !insertmacro LogSquirlDeleteKeyIfEmpty "Software\logsquirl"
!macroend

; The entry "Open with LogSquirl" in the context menu of every file (#724),
; for files whose type LogSquirl does not claim, such as rotated logs like
; app.log.1. The application's File Associations page adds and removes the
; same entry, LogSquirl, for the current user; that one hides the machine's.
!define LOGSQUIRL_CONTEXT_MENU "Software\Classes\*\shell\LogSquirl"

; Adds the entry for every user. It opens a file the way a double-click on a
; .log file does.
!macro LogSquirlAddContextMenu
    WriteRegStr HKLM "${LOGSQUIRL_CONTEXT_MENU}" "MUIVerb" "Open with LogSquirl"
    WriteRegStr HKLM "${LOGSQUIRL_CONTEXT_MENU}" "Icon" "$INSTDIR\logsquirl.exe"
    WriteRegStr HKLM "${LOGSQUIRL_CONTEXT_MENU}\command" "" '"$INSTDIR\logsquirl.exe" "%1"'
!macroend

; Removes the entry for every user, and the uninstalling user's own entry when
; it opens this installation's logsquirl.exe, shown or hiding the machine's;
; an entry of a portable LogSquirl stays.
!macro LogSquirlRemoveContextMenu
    Push $R0
    DeleteRegKey HKLM "${LOGSQUIRL_CONTEXT_MENU}"
    ReadRegStr $R0 HKCU "${LOGSQUIRL_CONTEXT_MENU}\command" ""
    ${If} $R0 == '"$INSTDIR\logsquirl.exe" "%1"'
        DeleteRegKey HKCU "${LOGSQUIRL_CONTEXT_MENU}"
    ${EndIf}
    Pop $R0
!macroend

; Tells Explorer that file associations changed, so icons and "Open with"
; follow at once (SHCNE_ASSOCCHANGED).
!macro LogSquirlFileTypesChanged
    System::Call 'shell32::SHChangeNotify(i 0x08000000, i 0, p 0, p 0)'
!macroend

!endif ; LOGSQUIRL_FILE_TYPES_NSH
