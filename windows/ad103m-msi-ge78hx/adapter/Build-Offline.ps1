$ErrorActionPreference='Stop'
$build=Join-Path $PSScriptRoot 'build'
[void][IO.Directory]::CreateDirectory($build)
@{tests_passed=$false;firmware_executed=$false} | ConvertTo-Json | Set-Content (Join-Path $build 'manifest.json')
$finder=[Environment]::ExpandEnvironmentVariables('%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe')
$install=& $finder -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
$vcvars=Join-Path $install 'VC\Auxiliary\Build\vcvars64.bat'
if(-not(Test-Path -LiteralPath $vcvars)){throw 'MSVC x64 missing'}
if($vcvars -match '["&|<>%^!\r\n]' -or $PSScriptRoot -match '["&|<>%^!\r\n]'){throw 'Unsupported shell path'}
$cmd=@'
@echo off
call "@VCVARS@" >nul
if errorlevel 1 exit /b 1
cd /d "@ROOT@"
cl /nologo /std:c11 /W4 /WX /DMSI_ADAPTER_HOST_TEST /I..\..\ad104-msi\vendor\edk2\Include /I..\..\ad104-msi\vendor\edk2\Include\X64 admission.c tests.c /Fobuild\ /Febuild\admission-tests.exe
if errorlevel 1 exit /b 1
build\admission-tests.exe >build\validation.log 2>&1
if errorlevel 1 exit /b 1
cl /nologo /std:c11 /c /I..\..\ad104-msi\vendor\edk2\Include /I..\..\ad104-msi\vendor\edk2\Include\X64 admission.c /Fobuild\forbidden.obj >build\guard-native.log 2>&1
if not errorlevel 1 exit /b 1
cl /nologo /std:c11 /c /DMSI_ADAPTER_HOST_TEST /DGU_UEFI_PORT /I..\..\ad104-msi\vendor\edk2\Include /I..\..\ad104-msi\vendor\edk2\Include\X64 admission.c /Fobuild\forbidden.obj >build\guard-unlock.log 2>&1
if not errorlevel 1 exit /b 1
where cl >build\compiler.txt
exit /b 0
'@
$cmd=$cmd.Replace('@VCVARS@',$vcvars).Replace('@ROOT@',$PSScriptRoot)
Set-Content (Join-Path $build 'compile.cmd') $cmd -Encoding ascii
& $env:ComSpec /d /c (Join-Path $build 'compile.cmd')
Get-Content (Join-Path $build 'validation.log') -ErrorAction SilentlyContinue
if($LASTEXITCODE -ne 0){throw 'Adapter host validation failed'}
$hashes=[ordered]@{}
foreach($file in @('admission.c','admission.h','tests.c','Build-Offline.ps1')){
 $hashes[$file]=(Get-FileHash -LiteralPath (Join-Path $PSScriptRoot $file)).Hash.ToLowerInvariant()
}
$headers=[ordered]@{}
Get-ChildItem -LiteralPath (Join-Path $PSScriptRoot '../../ad104-msi/vendor/edk2/Include') -File -Recurse | ForEach-Object {
 $headers[$_.FullName]=(Get-FileHash -LiteralPath $_.FullName).Hash.ToLowerInvariant()
}
$compiler=Get-Content (Join-Path $build 'compiler.txt') -First 1
[ordered]@{tests_passed=$true;firmware_executed=$false;gpu_access=$false;efi_built=$false;source_sha256=$hashes;headers_sha256=$headers;compiler_sha256=(Get-FileHash -LiteralPath $compiler).Hash;compiler_version=(Get-Item -LiteralPath $compiler).VersionInfo.FileVersion;utc=[DateTime]::UtcNow.ToString('o')} | ConvertTo-Json -Depth 5 | Set-Content (Join-Path $build 'manifest.json')
