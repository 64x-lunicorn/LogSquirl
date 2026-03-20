$ErrorActionPreference = 'Stop';

$packageArgs = @{
  packageName    = 'logsquirl'
  unzipLocation  = "$(Split-Path -parent $MyInvocation.MyCommand.Definition)"
  fileType       = 'exe'
  url64bit       = 'https://github.com/64x-lunicorn/LogSquirl/releases/download/v26.03.0/logsquirl-26.03.0-Win-x64-setup.exe'
  checksum64     = 'TODO'
  checksumType64 = 'sha256'
  softwareName   = 'logsquirl'
  silentArgs     = '/S'
  validExitCodes = @(0)
}

Install-ChocolateyPackage @packageArgs