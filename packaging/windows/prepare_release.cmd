echo %LOGSQUIRL_QT%
echo %LOGSQUIRL_QT_DIR%

md %LOGSQUIRL_WORKSPACE%\release

echo "Copying logsquirl binaries..."
xcopy %LOGSQUIRL_WORKSPACE%\%LOGSQUIRL_BUILD_ROOT%\output\logsquirl_portable.exe %LOGSQUIRL_WORKSPACE%\release\ /y
xcopy %LOGSQUIRL_WORKSPACE%\%LOGSQUIRL_BUILD_ROOT%\output\logsquirl_portable.pdb %LOGSQUIRL_WORKSPACE%\release\ /y
xcopy %LOGSQUIRL_WORKSPACE%\%LOGSQUIRL_BUILD_ROOT%\output\logsquirl.exe %LOGSQUIRL_WORKSPACE%\release\ /y
xcopy %LOGSQUIRL_WORKSPACE%\%LOGSQUIRL_BUILD_ROOT%\output\logsquirl.pdb %LOGSQUIRL_WORKSPACE%\release\ /y

xcopy %LOGSQUIRL_WORKSPACE%\%LOGSQUIRL_BUILD_ROOT%\output\logsquirl_crashpad_handler.exe %LOGSQUIRL_WORKSPACE%\release\ /y
xcopy %LOGSQUIRL_WORKSPACE%\%LOGSQUIRL_BUILD_ROOT%\output\logsquirl_minidump_dump.exe %LOGSQUIRL_WORKSPACE%\release\ /y

REM Copy TBB DLL from build output (placed there by cmake post-build step)
xcopy %LOGSQUIRL_WORKSPACE%\%LOGSQUIRL_BUILD_ROOT%\output\tbb12.dll %LOGSQUIRL_WORKSPACE%\release\ /y
xcopy %LOGSQUIRL_WORKSPACE%\%LOGSQUIRL_BUILD_ROOT%\output\tbb12.pdb %LOGSQUIRL_WORKSPACE%\release\ /y

xcopy %LOGSQUIRL_WORKSPACE%\%LOGSQUIRL_BUILD_ROOT%\generated\documentation.html %LOGSQUIRL_WORKSPACE%\release\ /y
xcopy %LOGSQUIRL_WORKSPACE%\COPYING %LOGSQUIRL_WORKSPACE%\release\ /y
xcopy %LOGSQUIRL_WORKSPACE%\NOTICE %LOGSQUIRL_WORKSPACE%\release\ /y
xcopy %LOGSQUIRL_WORKSPACE%\README.md %LOGSQUIRL_WORKSPACE%\release\ /y
xcopy %LOGSQUIRL_WORKSPACE%\DOCUMENTATION.md %LOGSQUIRL_WORKSPACE%\release\ /y

echo "Copying vc runtime..."
xcopy "%VCToolsRedistDir%%platform%\Microsoft.VC143.CRT\msvcp140.dll" %LOGSQUIRL_WORKSPACE%\release\ /y
xcopy "%VCToolsRedistDir%%platform%\Microsoft.VC143.CRT\msvcp140_1.dll" %LOGSQUIRL_WORKSPACE%\release\ /y
xcopy "%VCToolsRedistDir%%platform%\Microsoft.VC143.CRT\msvcp140_2.dll" %LOGSQUIRL_WORKSPACE%\release\ /y
xcopy "%VCToolsRedistDir%%platform%\Microsoft.VC143.CRT\vcruntime140.dll" %LOGSQUIRL_WORKSPACE%\release\ /y
xcopy "%VCToolsRedistDir%%platform%\Microsoft.VC143.CRT\vcruntime140_1.dll" %LOGSQUIRL_WORKSPACE%\release\ /y

echo "Copying ssl..."
xcopy %SSL_DIR%\libcrypto-3%SSL_ARCH%.dll %LOGSQUIRL_WORKSPACE%\release\ /y
xcopy %SSL_DIR%\libssl-3%SSL_ARCH%.dll %LOGSQUIRL_WORKSPACE%\release\ /y

echo "Copying Qt..."
set "QTDIR=%LOGSQUIRL_QT_DIR:/=\%"
echo %QTDIR%
xcopy %QTDIR%\bin\%LOGSQUIRL_QT%Core.dll %LOGSQUIRL_WORKSPACE%\release\ /y
xcopy %QTDIR%\bin\%LOGSQUIRL_QT%Gui.dll %LOGSQUIRL_WORKSPACE%\release\ /y
xcopy %QTDIR%\bin\%LOGSQUIRL_QT%Network.dll %LOGSQUIRL_WORKSPACE%\release\ /y
xcopy %QTDIR%\bin\%LOGSQUIRL_QT%Widgets.dll %LOGSQUIRL_WORKSPACE%\release\ /y
xcopy %QTDIR%\bin\%LOGSQUIRL_QT%Concurrent.dll %LOGSQUIRL_WORKSPACE%\release\ /y
xcopy %QTDIR%\bin\%LOGSQUIRL_QT%Xml.dll %LOGSQUIRL_WORKSPACE%\release\ /y
xcopy %QTDIR%\bin\%LOGSQUIRL_QT%Core5Compat.dll %LOGSQUIRL_WORKSPACE%\release\ /y

md %LOGSQUIRL_WORKSPACE%\release\platforms
xcopy %QTDIR%\plugins\platforms\qwindows.dll %LOGSQUIRL_WORKSPACE%\release\platforms\ /y

md %LOGSQUIRL_WORKSPACE%\release\styles
xcopy %QTDIR%\plugins\styles\qwindowsvistastyle.dll %LOGSQUIRL_WORKSPACE%\release\styles /y
xcopy %QTDIR%\plugins\styles\qmodernwindowsstyle.dll %LOGSQUIRL_WORKSPACE%\release\styles /y

echo "Copying packaging files..."
md %LOGSQUIRL_WORKSPACE%\chocolatey
xcopy %LOGSQUIRL_WORKSPACE%\packaging\windows\chocolatey\logsquirl.nuspec chocolatey\ /y

md %LOGSQUIRL_WORKSPACE%\chocolatey\tools
xcopy %LOGSQUIRL_WORKSPACE%\packaging\windows\chocolatey\tools\chocolateyInstall.ps1 chocolatey\tools\ /y

xcopy %LOGSQUIRL_WORKSPACE%\packaging\windows\logsquirl.nsi  /y
xcopy %LOGSQUIRL_WORKSPACE%\packaging\windows\FileAssociation.nsh  /y

echo "Making portable archive..."
7z a -r %LOGSQUIRL_WORKSPACE%\logsquirl-%LOGSQUIRL_VERSION%-%LOGSQUIRL_ARCH%-%LOGSQUIRL_QT%-portable.zip @%LOGSQUIRL_WORKSPACE%\packaging\windows\7z_logsquirl_listfile.txt
7z a %LOGSQUIRL_WORKSPACE%\logsquirl-%LOGSQUIRL_VERSION%-%LOGSQUIRL_ARCH%-%LOGSQUIRL_QT%-pdb.zip @%LOGSQUIRL_WORKSPACE%\packaging\windows\7z_pdb_listfile.txt

echo "Done!"
