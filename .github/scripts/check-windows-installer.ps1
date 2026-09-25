<#
.SYNOPSIS
  Installs, starts, upgrades and uninstalls the Windows installer silently, the
  way an administrator deploys it (#506).

.DESCRIPTION
  Runs on a GitHub-hosted Windows runner, which is a throwaway machine whose
  account is an administrator. Every check prints one "ok:" line, so the job
  log shows what was verified; the first failed check stops the script.

  What is checked, against packaging/windows/logsquirl.nsi:

  1. `setup.exe /S`, started by an administrator, installs without a dialog
     and exits with 0: the files under Program Files, the Qt and MSVC runtime
     DLLs, the Start menu shortcut for all users, the version under the
     machine's Uninstall key, no .log association (that section is /o). The
     installed application starts -- a standard (non-administrator) user
     included -- and `Uninstall.exe /S` removes it all again. Started by a
     standard user, the installer is refused before it runs.
  2. `setup.exe /S /D=<dir>`, run as SYSTEM the way Intune runs it, installs
     into that directory.
  3. `setup.exe /S` over the latest release upgrades it in place.

  The installed application is only ever started with `--version`, with its
  settings, data and temporary directories redirected into a scratch
  directory (as tests/e2e/isolated_instance.py does), and with a PATH that
  holds nothing but Windows itself, so it runs on the DLLs it was installed
  with.
#>
param(
    # The installer this run built.
    [Parameter(Mandatory)] [string] $Installer,
    # The version it was built as (LOGSQUIRL_VERSION, e.g. 26.10.0.1234).
    [Parameter(Mandatory)] [string] $Version,
    # The installer of the latest release, to upgrade from.
    [Parameter(Mandatory)] [string] $PreviousInstaller,
    [Parameter(Mandatory)] [string] $PreviousVersion,
    # A scratch directory.
    [Parameter(Mandatory)] [string] $WorkDir
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$UninstallKeyPath = 'SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall\logsquirl'
$DefaultInstallDir = Join-Path $env:ProgramFiles 'logsquirl'
$CommonStartMenuLink = Join-Path $env:ProgramData 'Microsoft\Windows\Start Menu\Programs\logsquirl.lnk'
$UserStartMenuLink = Join-Path ([Environment]::GetFolderPath('Programs')) 'logsquirl.lnk'
# SYSTEM's profile, as a 64-bit and as a 32-bit program sees it: the installer
# is a 32-bit program, for which System32 is redirected to SysWOW64.
$SystemSendToLinks = @('System32', 'SysWOW64') | ForEach-Object {
    Join-Path $env:SystemRoot "$_\config\systemprofile\AppData\Roaming\Microsoft\Windows\SendTo\logsquirl.lnk"
}
$UserSendToLink = Join-Path ([Environment]::GetFolderPath('SendTo')) 'logsquirl.lnk'

# What the default components put under the installation directory, per
# logsquirl.nsi for QT_MAJOR=Qt6 and PLATFORM=x64.
$ApplicationFiles = @(
    'logsquirl.exe', 'logsquirl_grep.exe', 'logsquirl_crashpad_handler.exe',
    'logsquirl_minidump_dump.exe', 'tbb12.dll', 'hs.dll', 'hs_avx2.dll',
    'COPYING', 'NOTICE', 'README.md', 'DOCUMENTATION.md', 'documentation.html',
    'Uninstall.exe'
)
$QtFiles = @(
    'Qt6Core.dll', 'Qt6Gui.dll', 'Qt6Network.dll', 'Qt6Widgets.dll',
    'Qt6Concurrent.dll', 'Qt6Xml.dll', 'Qt6Svg.dll',
    'platforms\qwindows.dll', 'imageformats\qsvg.dll', 'iconengines\qsvgicon.dll',
    'styles\qmodernwindowsstyle.dll', 'tls\qopensslbackend.dll', 'tls\qschannelbackend.dll'
)
$MsvcFiles = @(
    'msvcp140.dll', 'msvcp140_1.dll', 'vcruntime140.dll', 'vcruntime140_1.dll',
    'libcrypto-3-x64.dll', 'libssl-3-x64.dll'
)

function Pass([string] $What) { Write-Host "ok: $What" }

function Fail([string] $What) {
    Write-Host "::error::$What"
    throw $What
}

function Check([bool] $Condition, [string] $What) {
    if ($Condition) { Pass $What } else { Fail "not so: $What" }
}

function Step([string] $Title) { Write-Host "`n=== $Title ===" }

# The Uninstall key in one registry view. The installer is a 32-bit program,
# and a 32-bit program's HKLM\SOFTWARE is redirected to WOW6432Node on 64-bit
# Windows unless it asks for the 64-bit view.
function Get-UninstallKey([Microsoft.Win32.RegistryView] $View) {
    $base = [Microsoft.Win32.RegistryKey]::OpenBaseKey([Microsoft.Win32.RegistryHive]::LocalMachine, $View)
    return $base.OpenSubKey($UninstallKeyPath)
}

function Get-DotLogAssociation {
    $key = [Microsoft.Win32.Registry]::ClassesRoot.OpenSubKey('.log')
    if ($null -eq $key) { return '<no .log key>' }
    $value = $key.GetValue('')
    $backup = $key.GetValue('backup_val')
    return "default='$value' backup_val='$backup'"
}

function Get-LinkTarget([string] $Link) {
    return (New-Object -ComObject WScript.Shell).CreateShortcut($Link).TargetPath
}

# Starts an installer or uninstaller and waits for it, with a time limit: a
# dialog in a silent run would otherwise wait for a click forever.
function Invoke-Setup([string] $Exe, [string] $Arguments, [int] $TimeoutSeconds = 300) {
    Write-Host "> `"$Exe`" $Arguments"
    $watch = [Diagnostics.Stopwatch]::StartNew()
    $process = Start-Process -FilePath $Exe -ArgumentList $Arguments -PassThru
    if (-not $process.WaitForExit($TimeoutSeconds * 1000)) {
        $process.Kill($true)
        Fail "$Exe $Arguments still ran after $TimeoutSeconds s (a dialog in a silent run?)"
    }
    Write-Host "  exited with $($process.ExitCode) after $([int]$watch.Elapsed.TotalSeconds) s"
    return $process.ExitCode
}

# Waits until a condition holds: the NSIS uninstaller copies itself to TEMP,
# starts the copy and exits at once, so the files and keys go a moment later.
function Wait-Until([scriptblock] $Condition, [string] $What, [int] $TimeoutSeconds = 120) {
    $watch = [Diagnostics.Stopwatch]::StartNew()
    while (-not (& $Condition)) {
        if ($watch.Elapsed.TotalSeconds -gt $TimeoutSeconds) {
            Fail "not so after $TimeoutSeconds s: $What"
        }
        Start-Sleep -Milliseconds 500
    }
    Pass "$What (after $([int]$watch.Elapsed.TotalSeconds) s)"
}

# Runs an installed executable with --version in an isolated environment and
# returns what it printed.
function Invoke-Isolated([string] $Exe) {
    $root = Join-Path $WorkDir ('run-' + [guid]::NewGuid().ToString('N').Substring(0, 8))
    $home_ = Join-Path $root 'home'
    $appData = Join-Path $home_ 'AppData\Roaming'
    $localAppData = Join-Path $home_ 'AppData\Local'
    $temp = Join-Path $root 't'
    New-Item -ItemType Directory -Force -Path $appData, $localAppData, $temp, (Join-Path $appData 'logsquirl') | Out-Null
    Set-Content -Path (Join-Path $appData 'logsquirl\logsquirl.ini') -Value @(
        '[General]', 'versionchecker.enabled=false', 'session.loadLast=false', 'view.showSplashScreen=false'
    )

    $info = [Diagnostics.ProcessStartInfo]::new($Exe)
    $info.ArgumentList.Add('--version')
    $info.UseShellExecute = $false
    $info.RedirectStandardOutput = $true
    $info.RedirectStandardError = $true
    $info.WorkingDirectory = $root
    $info.Environment['USERPROFILE'] = $home_
    $info.Environment['HOME'] = $home_
    $info.Environment['APPDATA'] = $appData
    $info.Environment['LOCALAPPDATA'] = $localAppData
    $info.Environment['TEMP'] = $temp
    $info.Environment['TMP'] = $temp
    $info.Environment['LOGSQUIRL_TEST_MODE'] = '1'
    $info.Environment['LOGSQUIRL_INSTANCE_ID'] = Split-Path -Leaf $root
    $info.Environment['PATH'] = "$env:SystemRoot\System32;$env:SystemRoot"

    $process = [Diagnostics.Process]::Start($info)
    $stdout = $process.StandardOutput.ReadToEndAsync()
    $stderr = $process.StandardError.ReadToEndAsync()
    if (-not $process.WaitForExit(60000)) {
        $process.Kill($true)
        Fail "$Exe --version still ran after 60 s"
    }
    $process.WaitForExit()
    Write-Host "  $Exe --version exited with $($process.ExitCode):"
    $stdout.Result -split "`r?`n" | Where-Object { $_ } | ForEach-Object { Write-Host "    $_" }
    if ($stderr.Result) { Write-Host "  stderr: $($stderr.Result)" }
    return [pscustomobject]@{ ExitCode = $process.ExitCode; Output = $stdout.Result }
}

function Test-Starts([string] $InstallDir, [string] $ExpectedVersion) {
    foreach ($name in 'logsquirl', 'logsquirl_grep') {
        $run = Invoke-Isolated (Join-Path $InstallDir "$name.exe")
        Check ($run.ExitCode -eq 0) "$name.exe --version exits with 0, with only Windows on PATH"
        Check ($run.Output -match "(?m)^$name $([regex]::Escape($ExpectedVersion))\s*$") "$name.exe reports version $ExpectedVersion"
    }
}

function Test-Installed([string] $InstallDir, [string] $ExpectedVersion) {
    foreach ($group in @(
            @{ Name = 'application'; Files = $ApplicationFiles },
            @{ Name = 'Qt runtime'; Files = $QtFiles },
            @{ Name = 'MSVC and OpenSSL runtime'; Files = $MsvcFiles })) {
        $missing = @($group.Files | Where-Object { -not (Test-Path -PathType Leaf (Join-Path $InstallDir $_)) })
        Check ($missing.Count -eq 0) "$($group.Files.Count) $($group.Name) files under $InstallDir$(if ($missing) { " -- missing: $($missing -join ', ')" })"
    }

    # The installer is a 32-bit program and does not ask for the 64-bit view,
    # so its HKLM\SOFTWARE writes land in WOW6432Node: a detection rule has to
    # look there (Intune: "Associated with a 32-bit app on 64-bit clients").
    $key = Get-UninstallKey Registry32
    Check ($null -ne $key) "the Uninstall key is HKLM\SOFTWARE\WOW6432Node\Microsoft\Windows\CurrentVersion\Uninstall\logsquirl (the 32-bit view)"
    Check ($null -eq (Get-UninstallKey Registry64)) "and not in the 64-bit view, HKLM\$UninstallKeyPath"
    Check ($key.GetValue('DisplayVersion') -eq $ExpectedVersion) "DisplayVersion is $ExpectedVersion (is '$($key.GetValue('DisplayVersion'))')"
    Check ($key.GetValue('DisplayName') -eq 'LogSquirl') "DisplayName is LogSquirl"
    Check ($key.GetValue('InstallLocation') -eq $InstallDir) "InstallLocation is $InstallDir (is '$($key.GetValue('InstallLocation'))')"
    Check ($key.GetValue('UninstallString') -eq "`"$InstallDir\Uninstall.exe`"") "UninstallString is `"$InstallDir\Uninstall.exe`""

    Check (Test-Path $CommonStartMenuLink) "Start menu shortcut for all users: $CommonStartMenuLink"
    Check ((Get-LinkTarget $CommonStartMenuLink) -eq (Join-Path $InstallDir 'logsquirl.exe')) "it points to $InstallDir\logsquirl.exe"
    Check (-not (Test-Path $UserStartMenuLink)) "no Start menu shortcut in the installing user's own profile"

    $association = Get-DotLogAssociation
    Check ($association -eq $script:DotLogBefore) ".log association unchanged: $association"
}

function Test-Removed([string] $InstallDir) {
    try {
        Wait-Until { -not (Test-Path $InstallDir) } "$InstallDir is gone"
    } catch {
        Write-Host "  left under ${InstallDir}:"
        Get-ChildItem -Recurse -Force $InstallDir | ForEach-Object { Write-Host "    $($_.FullName)" }
        throw
    }
    Wait-Until { ($null -eq (Get-UninstallKey Registry32)) -and ($null -eq (Get-UninstallKey Registry64)) } "the Uninstall key is gone"
    Check (-not (Test-Path $CommonStartMenuLink)) "the Start menu shortcut is gone"
    Check ($null -eq [Microsoft.Win32.Registry]::ClassesRoot.OpenSubKey('Applications\logsquirl.exe')) "HKCR\Applications\logsquirl.exe is gone"
    $association = Get-DotLogAssociation
    Check ($association -eq $script:DotLogBefore) ".log association unchanged: $association"
}

function Uninstall([string] $InstallDir) {
    $code = Invoke-Setup (Join-Path $InstallDir 'Uninstall.exe') '/S'
    Check ($code -eq 0) "Uninstall.exe /S exits with 0"
    Test-Removed $InstallDir
}

# ---------------------------------------------------------------------------
Step 'The machine'
$principal = [Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()
$elevated = $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
$uac = (Get-ItemProperty 'HKLM:\SOFTWARE\Microsoft\Windows\CurrentVersion\Policies\System').EnableLUA
Write-Host "  user $([Environment]::UserName), elevated administrator: $elevated, UAC (EnableLUA): $uac"
Write-Host "  $((Get-CimInstance Win32_OperatingSystem).Caption) $([Environment]::OSVersion.Version), 64-bit: $([Environment]::Is64BitOperatingSystem)"
Check $elevated 'the job runs as an elevated administrator'
Check (-not (Test-Path $DefaultInstallDir)) "nothing is installed at $DefaultInstallDir yet"
Check ($null -eq (Get-UninstallKey Registry32) -and $null -eq (Get-UninstallKey Registry64)) 'no Uninstall key yet'
$script:DotLogBefore = Get-DotLogAssociation
Write-Host "  .log association before: $script:DotLogBefore"
$realAppData = Join-Path $env:APPDATA 'logsquirl'
$realAppDataBefore = Test-Path $realAppData

# ---------------------------------------------------------------------------
Step '1. setup.exe /S as an administrator'
$code = Invoke-Setup $Installer '/S'
Check ($code -eq 0) 'setup.exe /S exits with 0'
Test-Installed $DefaultInstallDir $Version
Write-Host "  'Send to' shortcut in the installing user's profile ($UserSendToLink): $(Test-Path $UserSendToLink)"
Test-Starts $DefaultInstallDir $Version
Check ((Test-Path $realAppData) -eq $realAppDataBefore) "the runner's own $realAppData is untouched by the isolated starts"

Step '1b. The installed application starts for a standard user'
$standardUser = 'lsqstandard'
$password = ConvertTo-SecureString ('Aa1!' + [guid]::NewGuid().ToString('N')) -AsPlainText -Force
New-LocalUser -Name $standardUser -Password $password -AccountNeverExpires -PasswordNeverExpires | Out-Null
Add-LocalGroupMember -Group 'Users' -Member $standardUser
$credential = [pscredential]::new("$env:COMPUTERNAME\$standardUser", $password)
$shared = Join-Path $env:SystemDrive 'lsq-standard'
New-Item -ItemType Directory -Force -Path $shared | Out-Null
& icacls $shared /grant 'Users:(OI)(CI)M' | Out-Null
function Invoke-AsStandardUser([string] $Exe, [string] $Arguments, [string] $Name) {
    $out = Join-Path $shared "$Name.out"
    $err = Join-Path $shared "$Name.err"
    $process = Start-Process -FilePath $Exe -ArgumentList $Arguments -Credential $credential `
        -WorkingDirectory $shared -RedirectStandardOutput $out -RedirectStandardError $err -PassThru
    if (-not $process.WaitForExit(120000)) {
        $process.Kill($true)
        Fail "$Exe $Arguments as $standardUser still ran after 120 s"
    }
    $process.WaitForExit()
    return [pscustomobject]@{ ExitCode = $process.ExitCode; Output = (Get-Content -Raw $out) + (Get-Content -Raw $err) }
}
$groups = Invoke-AsStandardUser "$env:SystemRoot\System32\whoami.exe" '/groups' 'whoami'
Check ($groups.ExitCode -eq 0 -and $groups.Output -notmatch 'S-1-5-32-544') "$standardUser is not in Administrators"
$run = Invoke-AsStandardUser (Join-Path $DefaultInstallDir 'logsquirl.exe') '--version' 'version'
Write-Host "  $($run.Output)"
Check ($run.ExitCode -eq 0 -and $run.Output -match "logsquirl $([regex]::Escape($Version))") "logsquirl.exe --version runs for $standardUser and reports $Version"

Step '1c. Uninstall.exe /S'
Uninstall $DefaultInstallDir
Check (-not (Test-Path $UserSendToLink)) "the 'Send to' shortcut is gone from the installing user's profile"

Step '1d. setup.exe started without elevation'
# A program whose manifest requires an administrator is not started by
# CreateProcess without one: Windows refuses with ERROR_ELEVATION_REQUIRED
# (740), so the installer never runs half-privileged. (Started from Explorer
# or a non-elevated prompt, ShellExecute shows the UAC prompt instead; that
# prompt is not something a CI runner can show.)
$refusal = $null
try {
    $process = Start-Process -FilePath $Installer -ArgumentList '/S' -Credential $credential -WorkingDirectory $shared -PassThru
    $process.WaitForExit(120000) | Out-Null
} catch {
    $refusal = $_.Exception
}
$codes = @()
for ($e = $refusal; $null -ne $e; $e = $e.InnerException) {
    if ($e -is [ComponentModel.Win32Exception]) { $codes += $e.NativeErrorCode }
}
if ($refusal) { Write-Host "  refused: $($refusal.Message)" }
Check ($null -ne $refusal -and ($codes -contains 740 -or $refusal.Message -match 'requires elevation')) "setup.exe /S as $standardUser is refused: the operation requires elevation"
Check (-not (Test-Path $DefaultInstallDir)) "nothing was installed at $DefaultInstallDir"
Check ($null -eq (Get-UninstallKey Registry32)) 'no Uninstall key was written'

# ---------------------------------------------------------------------------
Step '2. setup.exe /S /D=<dir> as SYSTEM (as Intune runs it)'
$customDir = Join-Path $env:SystemDrive 'Tools\Log Squirl'
$task = 'logsquirl-silent-install'
$action = New-ScheduledTaskAction -Execute $Installer -Argument "/S /D=$customDir"
$systemPrincipal = New-ScheduledTaskPrincipal -UserId 'SYSTEM' -LogonType ServiceAccount -RunLevel Highest
Register-ScheduledTask -TaskName $task -Action $action -Principal $systemPrincipal | Out-Null
Write-Host "> as SYSTEM: `"$Installer`" /S /D=$customDir"
Start-ScheduledTask -TaskName $task
Wait-Until { (Get-ScheduledTask -TaskName $task).State -eq 'Ready' -and (Get-ScheduledTaskInfo -TaskName $task).LastTaskResult -notin 267009, 267011 } 'the SYSTEM task has finished' 300
$result = (Get-ScheduledTaskInfo -TaskName $task).LastTaskResult
Unregister-ScheduledTask -TaskName $task -Confirm:$false
Check ($result -eq 0) "setup.exe /S /D=... as SYSTEM exits with 0 (is $result)"
Check (-not (Test-Path $DefaultInstallDir)) "nothing under $DefaultInstallDir"
Test-Installed $customDir $Version
# Not a check, a note for administrators: $SENDTO is the installing user's,
# so an install as SYSTEM gives the users no 'Send to' entry.
foreach ($link in $SystemSendToLinks) { Write-Host "  'Send to' shortcut in SYSTEM's profile ($link): $(Test-Path $link)" }
Write-Host "  'Send to' shortcut in the administrator's profile: $(Test-Path $UserSendToLink)"
Test-Starts $customDir $Version
Uninstall $customDir

# ---------------------------------------------------------------------------
Step "3. setup.exe /S over the latest release ($PreviousVersion)"
$code = Invoke-Setup $PreviousInstaller '/S'
Check ($code -eq 0) "the $PreviousVersion setup.exe /S exits with 0"
$key = Get-UninstallKey Registry32
Check ($null -ne $key -and $key.GetValue('DisplayVersion') -eq $PreviousVersion) "DisplayVersion is $PreviousVersion"
$code = Invoke-Setup $Installer '/S'
Check ($code -eq 0) 'setup.exe /S over it exits with 0'
Test-Installed $DefaultInstallDir $Version
Test-Starts $DefaultInstallDir $Version
Uninstall $DefaultInstallDir

Write-Host "`nAll installer checks passed."
