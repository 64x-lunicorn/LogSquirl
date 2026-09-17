REM cmd does not stop on a failing command and the script used to end in a
REM successful echo, so a missing file still produced a green package step.
REM Every command that can fail ends in `|| exit /b 1` (#218).

echo %LOGSQUIRL_QT%
echo %LOGSQUIRL_QT_DIR%

md %LOGSQUIRL_WORKSPACE%\release || exit /b 1

echo "Copying logsquirl binaries..."
xcopy %LOGSQUIRL_WORKSPACE%\%LOGSQUIRL_BUILD_ROOT%\output\logsquirl_portable.exe %LOGSQUIRL_WORKSPACE%\release\ /y || exit /b 1
xcopy %LOGSQUIRL_WORKSPACE%\%LOGSQUIRL_BUILD_ROOT%\output\logsquirl_portable.pdb %LOGSQUIRL_WORKSPACE%\release\ /y || exit /b 1
xcopy %LOGSQUIRL_WORKSPACE%\%LOGSQUIRL_BUILD_ROOT%\output\logsquirl.exe %LOGSQUIRL_WORKSPACE%\release\ /y || exit /b 1
xcopy %LOGSQUIRL_WORKSPACE%\%LOGSQUIRL_BUILD_ROOT%\output\logsquirl.pdb %LOGSQUIRL_WORKSPACE%\release\ /y || exit /b 1

xcopy %LOGSQUIRL_WORKSPACE%\%LOGSQUIRL_BUILD_ROOT%\output\logsquirl_crashpad_handler.exe %LOGSQUIRL_WORKSPACE%\release\ /y || exit /b 1
xcopy %LOGSQUIRL_WORKSPACE%\%LOGSQUIRL_BUILD_ROOT%\output\logsquirl_minidump_dump.exe %LOGSQUIRL_WORKSPACE%\release\ /y || exit /b 1

REM Copy TBB DLL from build output (placed there by cmake post-build step)
xcopy %LOGSQUIRL_WORKSPACE%\%LOGSQUIRL_BUILD_ROOT%\output\tbb12.dll %LOGSQUIRL_WORKSPACE%\release\ /y || exit /b 1

REM Hyperscan for CPUs without and with AVX2, chosen at run time (#281)
xcopy %LOGSQUIRL_WORKSPACE%\%LOGSQUIRL_BUILD_ROOT%\output\hs.dll %LOGSQUIRL_WORKSPACE%\release\ /y || exit /b 1
xcopy %LOGSQUIRL_WORKSPACE%\%LOGSQUIRL_BUILD_ROOT%\output\hs_avx2.dll %LOGSQUIRL_WORKSPACE%\release\ /y || exit /b 1

xcopy %LOGSQUIRL_WORKSPACE%\%LOGSQUIRL_BUILD_ROOT%\generated\documentation.html %LOGSQUIRL_WORKSPACE%\release\ /y || exit /b 1
xcopy %LOGSQUIRL_WORKSPACE%\COPYING %LOGSQUIRL_WORKSPACE%\release\ /y || exit /b 1
xcopy %LOGSQUIRL_WORKSPACE%\NOTICE %LOGSQUIRL_WORKSPACE%\release\ /y || exit /b 1
xcopy %LOGSQUIRL_WORKSPACE%\README.md %LOGSQUIRL_WORKSPACE%\release\ /y || exit /b 1
xcopy %LOGSQUIRL_WORKSPACE%\DOCUMENTATION.md %LOGSQUIRL_WORKSPACE%\release\ /y || exit /b 1

echo "Copying vc runtime..."
set "CRT_ARCH=%VSCMD_ARG_TGT_ARCH%"
if "%CRT_ARCH%"=="" set "CRT_ARCH=%platform%"
if "%CRT_ARCH%"=="" set "CRT_ARCH=%LOGSQUIRL_ARCH%"

set "CRT_DIR="
for %%D in (
	"%VCToolsRedistDir%%CRT_ARCH%\Microsoft.VC143.CRT"
	"%VCToolsRedistDir%\Microsoft.VC143.CRT"
	"%SystemRoot%\System32"
) do (
	if exist "%%~D\msvcp140.dll" (
		set "CRT_DIR=%%~D"
		goto :crt_found
	)
)

echo ERROR: Could not locate VC runtime directory (msvcp140.dll not found).
echo VCToolsRedistDir=%VCToolsRedistDir%
echo CRT_ARCH=%CRT_ARCH%
exit /b 1

:crt_found
echo Using VC runtime from: %CRT_DIR%
xcopy "%CRT_DIR%\msvcp140.dll" "%LOGSQUIRL_WORKSPACE%\release\" /y || exit /b 1
xcopy "%CRT_DIR%\msvcp140_1.dll" "%LOGSQUIRL_WORKSPACE%\release\" /y || exit /b 1
xcopy "%CRT_DIR%\vcruntime140.dll" "%LOGSQUIRL_WORKSPACE%\release\" /y || exit /b 1
xcopy "%CRT_DIR%\vcruntime140_1.dll" "%LOGSQUIRL_WORKSPACE%\release\" /y || exit /b 1

if not exist "%LOGSQUIRL_WORKSPACE%\release\msvcp140.dll" (
	echo ERROR: Failed to copy msvcp140.dll to release directory.
	exit /b 1
)
if not exist "%LOGSQUIRL_WORKSPACE%\release\msvcp140_1.dll" (
	echo ERROR: Failed to copy msvcp140_1.dll to release directory.
	exit /b 1
)
if not exist "%LOGSQUIRL_WORKSPACE%\release\vcruntime140.dll" (
	echo ERROR: Failed to copy vcruntime140.dll to release directory.
	exit /b 1
)
if not exist "%LOGSQUIRL_WORKSPACE%\release\vcruntime140_1.dll" (
	echo ERROR: Failed to copy vcruntime140_1.dll to release directory.
	exit /b 1
)

echo "Copying ssl..."
xcopy %SSL_DIR%\libcrypto-3%SSL_ARCH%.dll %LOGSQUIRL_WORKSPACE%\release\ /y || exit /b 1
xcopy %SSL_DIR%\libssl-3%SSL_ARCH%.dll %LOGSQUIRL_WORKSPACE%\release\ /y || exit /b 1

echo "Copying Qt..."
set "QTDIR=%LOGSQUIRL_QT_DIR:/=\%"
echo %QTDIR%
xcopy %QTDIR%\bin\%LOGSQUIRL_QT%Core.dll %LOGSQUIRL_WORKSPACE%\release\ /y || exit /b 1
xcopy %QTDIR%\bin\%LOGSQUIRL_QT%Gui.dll %LOGSQUIRL_WORKSPACE%\release\ /y || exit /b 1
xcopy %QTDIR%\bin\%LOGSQUIRL_QT%Network.dll %LOGSQUIRL_WORKSPACE%\release\ /y || exit /b 1
xcopy %QTDIR%\bin\%LOGSQUIRL_QT%Widgets.dll %LOGSQUIRL_WORKSPACE%\release\ /y || exit /b 1
xcopy %QTDIR%\bin\%LOGSQUIRL_QT%Concurrent.dll %LOGSQUIRL_WORKSPACE%\release\ /y || exit /b 1
xcopy %QTDIR%\bin\%LOGSQUIRL_QT%Xml.dll %LOGSQUIRL_WORKSPACE%\release\ /y || exit /b 1
xcopy %QTDIR%\bin\%LOGSQUIRL_QT%Core5Compat.dll %LOGSQUIRL_WORKSPACE%\release\ /y || exit /b 1

md %LOGSQUIRL_WORKSPACE%\release\platforms || exit /b 1
xcopy %QTDIR%\plugins\platforms\qwindows.dll %LOGSQUIRL_WORKSPACE%\release\platforms\ /y || exit /b 1

md %LOGSQUIRL_WORKSPACE%\release\styles || exit /b 1
xcopy %QTDIR%\plugins\styles\qmodernwindowsstyle.dll %LOGSQUIRL_WORKSPACE%\release\styles /y || exit /b 1

echo "Copying Qt TLS plugins..."
md %LOGSQUIRL_WORKSPACE%\release\tls || exit /b 1
xcopy %QTDIR%\plugins\tls\qopensslbackend.dll %LOGSQUIRL_WORKSPACE%\release\tls\ /y || exit /b 1
xcopy %QTDIR%\plugins\tls\qschannelbackend.dll %LOGSQUIRL_WORKSPACE%\release\tls\ /y || exit /b 1

echo "Copying packaging files..."
xcopy %LOGSQUIRL_WORKSPACE%\packaging\windows\logsquirl.nsi  /y || exit /b 1
xcopy %LOGSQUIRL_WORKSPACE%\packaging\windows\FileAssociation.nsh  /y || exit /b 1

echo "Making portable archive..."
7z a -r %LOGSQUIRL_WORKSPACE%\logsquirl-win-%LOGSQUIRL_ARCH%-portable.zip @%LOGSQUIRL_WORKSPACE%\packaging\windows\7z_logsquirl_listfile.txt || exit /b 1
7z a %LOGSQUIRL_WORKSPACE%\logsquirl-win-%LOGSQUIRL_ARCH%-pdb.zip @%LOGSQUIRL_WORKSPACE%\packaging\windows\7z_pdb_listfile.txt || exit /b 1

echo "Done!"
