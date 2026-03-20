# NSIS script creating the Windows installer for logsquirl

# Is passed to the script using -DVERSION=$(git describe) on the command line
!ifndef VERSION
    !define VERSION 'dev-build'
!endif

!ifndef PLATFORM
    !define PLATFORM 'unknown'
!endif

!ifndef QT_MAJOR
    !define QT_MAJOR 'Qt5'
!endif

# Headers
!include "MUI2.nsh"
!include "FileAssociation.nsh"

# General
OutFile "logsquirl-${VERSION}-${PLATFORM}-${QT_MAJOR}-setup.exe"

XpStyle on

SetCompressor /SOLID lzma

; Registry key to keep track of the directory we are installed in
!ifdef ARCH32
  InstallDir "$PROGRAMFILES\logsquirl"
!else
  InstallDir "$PROGRAMFILES64\logsquirl"
!endif
InstallDirRegKey HKLM Software\logsquirl ""

; logsquirl icon
; !define MUI_ICON logsquirl.ico

RequestExecutionLevel admin

Name "LogSquirl"
Caption "LogSquirl ${VERSION} Setup"

# Pages
!define MUI_WELCOMEPAGE_TITLE "Welcome to the LogSquirl ${VERSION} Setup Wizard"
!define MUI_WELCOMEPAGE_TEXT "This wizard will guide you through the installation of LogSquirl\
, a fast, advanced log explorer.$\r$\n$\r$\n\
logsquirl and the Qt libraries are released under the GPL, see \
the COPYING and NOTICE files.$\r$\n$\r$\n$_CLICK"
;MUI_FINISHPAGE_LINK_LOCATION "https://klogg.filimonov.dev/"

!insertmacro MUI_PAGE_WELCOME
;!insertmacro MUI_PAGE_LICENSE "COPYING"
# !ifdef VER_MAJOR & VER_MINOR & VER_REVISION & VER_BUILD...
!insertmacro MUI_PAGE_COMPONENTS
!insertmacro MUI_PAGE_DIRECTORY
!insertmacro MUI_PAGE_INSTFILES
!insertmacro MUI_PAGE_FINISH

!insertmacro MUI_UNPAGE_WELCOME
!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_INSTFILES
!insertmacro MUI_UNPAGE_FINISH

# Languages
!insertmacro MUI_LANGUAGE "English"

# Installer sections
Section "logsquirl" logsquirl
    ; Prevent this section from being unselected
    SectionIn RO

    SetOutPath $INSTDIR
    File release\logsquirl.exe
    File release\logsquirl_crashpad_handler.exe
    File release\logsquirl_minidump_dump.exe
    File release\tbb12.dll

    File COPYING
    File NOTICE
    File README.md
    File DOCUMENTATION.md
    File release\documentation.html

    ; Create the 'sendto' link
    CreateShortCut "$SENDTO\logsquirl.lnk" "$INSTDIR\logsquirl.exe" "" "$INSTDIR\logsquirl.exe" 0

    ; Register as an otion (but not main handler) for some files (.txt, .Log, .cap)
    WriteRegStr HKCR "Applications\logsquirl.exe" "" ""
    WriteRegStr HKCR "Applications\logsquirl.exe\shell" "" "open"
    WriteRegStr HKCR "Applications\logsquirl.exe\shell\open" "LogSquirl log viewer" "logsquirl"
    WriteRegStr HKCR "Applications\logsquirl.exe\shell\open\command" "" '"$INSTDIR\logsquirl.exe" "%1"'
    WriteRegStr HKCR "*\OpenWithList\logsquirl.exe" "" ""
    WriteRegStr HKCR ".txt\OpenWithList\logsquirl.exe" "" ""
    WriteRegStr HKCR ".Log\OpenWithList\logsquirl.exe" "" ""
    WriteRegStr HKCR ".cap\OpenWithList\logsquirl.exe" "" ""

    ; Register uninstaller
    WriteRegExpandStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\logsquirl"\
"UninstallString" '"$INSTDIR\Uninstall.exe"'
    WriteRegExpandStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\logsquirl"\
"InstallLocation" "$INSTDIR"
    WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\logsquirl" "DisplayName" "LogSquirl"
    WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\logsquirl" "DisplayVersion" "${VERSION}"
    WriteRegDWORD HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\logsquirl" "NoModify" "1"
    WriteRegDWORD HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\logsquirl" "NoRepair" "1"

    ; Create uninstaller
    WriteUninstaller "$INSTDIR\Uninstall.exe"
SectionEnd

Section "Qt Runtime libraries" qtlibs
    SetOutPath $INSTDIR
    File release\${QT_MAJOR}Core.dll
    File release\${QT_MAJOR}Gui.dll
    File release\${QT_MAJOR}Network.dll
    File release\${QT_MAJOR}Widgets.dll
    File release\${QT_MAJOR}Concurrent.dll
    File release\${QT_MAJOR}Xml.dll
!if ${QT_MAJOR} == "Qt6"
    File release\${QT_MAJOR}Core5Compat.dll
!endif

    SetOutPath $INSTDIR\platforms
    File release\platforms\qwindows.dll
    SetOutPath $INSTDIR\styles
!if ${QT_MAJOR} == "Qt6"
    File release\styles\qmodernwindowsstyle.dll
!else
    File release\styles\qwindowsvistastyle.dll
!endif

SectionEnd

Section "MSVC Runtime libraries" vcruntime
    SetOutPath $INSTDIR
    File release\msvcp140.dll
    File release\msvcp140_1.dll
    File release\vcruntime140.dll
    
!if ${PLATFORM} == "x64"
    File release\vcruntime140_1.dll

    File release\libcrypto-3-x64.dll
    File release\libssl-3-x64.dll
!else
    File release\libcrypto-3.dll
    File release\libssl-3.dll
!endif

SectionEnd

Section "Create Start menu shortcut" shortcut
    SetShellVarContext all
    CreateShortCut "$SMPROGRAMS\logsquirl.lnk" "$INSTDIR\logsquirl.exe" "" "$INSTDIR\logsquirl.exe" 0
SectionEnd

Section /o "Associate with .log files" associate
    ${registerExtension} "$INSTDIR\logsquirl.exe" ".log" "Log file"
SectionEnd

# Descriptions
!insertmacro MUI_FUNCTION_DESCRIPTION_BEGIN
    !insertmacro MUI_DESCRIPTION_TEXT ${logsquirl} "The core files required to use logsquirl."
    !insertmacro MUI_DESCRIPTION_TEXT ${qtlibs} "Needed by logsquirl, you have to install these unless \
you already have the Qt development kit installed."
    !insertmacro MUI_DESCRIPTION_TEXT ${vcruntime} "Needed by logsquirl, you have to install these unless \
you already have the Microsoft Visual C++ 2017 Redistributable installed."
    !insertmacro MUI_DESCRIPTION_TEXT ${shortcut} "Create a shortcut in the Start menu for logsquirl."
    !insertmacro MUI_DESCRIPTION_TEXT ${associate} "Make logsquirl the default viewer for .log files."
!insertmacro MUI_FUNCTION_DESCRIPTION_END

# Uninstaller
Section "Uninstall"
    Delete "$INSTDIR\Uninstall.exe"

    Delete "$INSTDIR\logsquirl.exe"
    Delete "$INSTDIR\logsquirl_crashpad_handler.exe"
    Delete "$INSTDIR\logsquirl_minidump_dump.exe"
    Delete "$INSTDIR\README.md"
    Delete "$INSTDIR\COPYING"
    Delete "$INSTDIR\NOTICE"
    Delete "$INSTDIR\readme.html"
    Delete "$INSTDIR\documentation.md"
    Delete "$INSTDIR\documentation.html"
    Delete "$INSTDIR\libstdc++-6.dll"
    Delete "$INSTDIR\libgcc_s_seh-1.dll"
    Delete "$INSTDIR\libgcc_s_dw2-1.dll"
    Delete "$INSTDIR\Qt5Widgets.dll"
    Delete "$INSTDIR\Qt5Core.dll"
    Delete "$INSTDIR\Qt5Gui.dll"
    Delete "$INSTDIR\Qt5Network.dll"
    Delete "$INSTDIR\Qt5Concurrent.dll"
    Delete "$INSTDIR\Qt5Xml.dll"
    Delete "$INSTDIR\Qt6Widgets.dll"
    Delete "$INSTDIR\Qt6Core.dll"
    Delete "$INSTDIR\Qt6Gui.dll"
    Delete "$INSTDIR\Qt6Network.dll"
    Delete "$INSTDIR\Qt6Concurrent.dll"
    Delete "$INSTDIR\Qt6Xml.dll"
    Delete "$INSTDIR\Qt6Core5Compat.dll"
    Delete "$INSTDIR\platforms\qwindows.dll"
    Delete "$INSTDIR\platforms\qminimal.dll"
    Delete "$INSTDIR\styles\qwindowsvistastyle.dll"
    Delete "$INSTDIR\msvcp140.dll"
    Delete "$INSTDIR\msvcp140_1.dll"
    Delete "$INSTDIR\vcruntime140.dll"
    Delete "$INSTDIR\vcruntime140_1.dll"
    Delete "$INSTDIR\tbb12.dll"
    Delete "$INSTDIR\tbbmalloc.dll"
    Delete "$INSTDIR\tbbmalloc_proxy.dll"
    Delete "$INSTDIR\logsquirl_tbbmalloc.dll"
    Delete "$INSTDIR\logsquirl_tbbmalloc_proxy.dll"
    Delete "$INSTDIR\libcrypto-3-x64.dll"
    Delete "$INSTDIR\libssl-3-x64.dll"
    Delete "$INSTDIR\libcrypto-3.dll"
    Delete "$INSTDIR\libssl-3.dll"
    ; Clean up legacy OpenSSL 1.1 files from previous installations
    Delete "$INSTDIR\libcrypto-1_1-x64.dll"
    Delete "$INSTDIR\libssl-1_1-x64.dll"
    Delete "$INSTDIR\libcrypto-1_1.dll"
    Delete "$INSTDIR\libssl-1_1.dll"
    Delete "$INSTDIR\mimalloc.dll"
    Delete "$INSTDIR\mimalloc_override.dll"
    Delete "$INSTDIR\mimalloc_redirect.dll"
    Delete "$INSTDIR\mimalloc_redirect32.dll"
    Delete "$INSTDIR\mimalloc-redirect.dll"
    Delete "$INSTDIR\mimalloc-redirect32.dll"
    RMDir "$INSTDIR"

    ; Remove settings in %appdata%
    Delete "$APPDATA\logsquirl\logsquirl.ini"
    RMDir "$APPDATA\logsquirl"

    DeleteRegKey HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\logsquirl"

    ; Remove the file associations
    ${unregisterExtension} ".log" "Log file"

    DeleteRegKey HKCR "*\OpenWithList\logsquirl.exe"
    DeleteRegKey HKCR ".txt\OpenWithList\logsquirl.exe"
    DeleteRegKey HKCR ".Log\OpenWithList\logsquirl.exe"
    DeleteRegKey HKCR ".cap\OpenWithList\logsquirl.exe"
    DeleteRegKey HKCR "Applications\logsquirl.exe\shell\open\command"
    DeleteRegKey HKCR "Applications\logsquirl.exe\shell\open"
    DeleteRegKey HKCR "Applications\logsquirl.exe\shell"
    DeleteRegKey HKCR "Applications\logsquirl.exe"

    ; Remove the shortcut, if any
    SetShellVarContext all
    Delete "$SMPROGRAMS\logsquirl.lnk"
SectionEnd

;!uninstfinalize 'packaging\windows\codesign_client.exe --debug "%1"'
