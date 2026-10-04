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
; meantime keeps that application.
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

; Deletes a key under HKLM that holds nothing: no subkey and no value. Not
; DeleteRegKey /ifempty, which looks at the subkeys only and would take an
; extension's other values with it. EnumRegValue names the default value "",
; the same as the end of the list, so a key with a default value is kept, and
; a second value is looked for.
!macro LogSquirlDeleteKeyIfEmpty _KEY
    Push $R1
    EnumRegKey $R1 HKLM "${_KEY}" 0
    ${If} $R1 == ""
        ReadRegStr $R1 HKLM "${_KEY}" ""
        ${If} $R1 == ""
            EnumRegValue $R1 HKLM "${_KEY}" 0
            ${If} $R1 == ""
                EnumRegValue $R1 HKLM "${_KEY}" 1
                ${If} $R1 == ""
                    DeleteRegKey HKLM "${_KEY}"
                ${EndIf}
            ${EndIf}
        ${EndIf}
    ${EndIf}
    Pop $R1
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

; Removes what the installer kept about the extensions, after every extension
; was given back.
!macro LogSquirlRemoveFileTypesBackup
    DeleteRegKey HKLM "${LOGSQUIRL_FILE_TYPES_BACKUP}"
    !insertmacro LogSquirlDeleteKeyIfEmpty "Software\logsquirl"
!macroend

; Tells Explorer that file associations changed, so icons and "Open with"
; follow at once (SHCNE_ASSOCCHANGED).
!macro LogSquirlFileTypesChanged
    System::Call 'shell32::SHChangeNotify(i 0x08000000, i 0, p 0, p 0)'
!macroend

!endif ; LOGSQUIRL_FILE_TYPES_NSH
