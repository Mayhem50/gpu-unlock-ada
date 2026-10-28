param([string]$Python='python')
$ErrorActionPreference='Stop'
$build=Join-Path $PSScriptRoot 'build'
if(-not(Test-Path -LiteralPath (Join-Path $build 'build_id.h'))){throw 'Existing diagnostic build_id.h required; this CPU runner never rebuilds an EFI'}
@{tests_passed=$false;firmware_executed=$false;efi_built=$false} | ConvertTo-Json | Set-Content (Join-Path $build 'host-only-manifest.json')
$finder=[Environment]::ExpandEnvironmentVariables('%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe')
$install=& $finder -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
$vcvars=Join-Path $install 'VC\Auxiliary\Build\vcvars64.bat'
if(-not(Test-Path -LiteralPath $vcvars)){throw 'MSVC x64 missing'}
if($vcvars -match '["&|<>%^!\r\n]' -or $PSScriptRoot -match '["&|<>%^!\r\n]'){throw 'Unsupported shell path'}
$cmd=@'
@echo off
call "@VCVARS@" >nul
if not "%errorlevel%"=="0" exit /b 1
cd /d "@ROOT@"
if not "%errorlevel%"=="0" exit /b 1
cl /nologo /W4 /WX /Od /D_CRT_SECURE_NO_WARNINGS /FIbuild_id.h /Ibuild /I..\..\ad104-msi\vendor\edk2\Include /I..\..\ad104-msi\vendor\edk2\Include\X64 /DCS_REAL_PROFILE tests\host_tests.c src\core.c src\sample.c src\platform.c src\console_state.c /Febuild\host-tests.exe /Fobuild\
if not "%errorlevel%"=="0" exit /b 1
build\host-tests.exe
if not "%errorlevel%"=="0" exit /b 1
cl /nologo /W4 /WX /Od /D_CRT_SECURE_NO_WARNINGS /I..\..\ad104-msi\vendor\edk2\Include /I..\..\ad104-msi\vendor\edk2\Include\X64 /DCS_REAL_PROFILE tests\platform_tests.c src\core.c src\platform.c src\console_state.c /Febuild\platform-tests.exe /Fobuild\
if not "%errorlevel%"=="0" exit /b 1
build\platform-tests.exe
if not "%errorlevel%"=="0" exit /b 1
cl /nologo /W4 /WX /Od /D_CRT_SECURE_NO_WARNINGS /DCS_SYNTHETIC_PROFILE /I..\..\ad104-msi\vendor\edk2\Include /I..\..\ad104-msi\vendor\edk2\Include\X64 tests\console_state_tests.c src\core.c src\platform.c src\console_state.c /Febuild\console-state-tests.exe /Fobuild\
if not "%errorlevel%"=="0" exit /b 1
build\console-state-tests.exe
if not "%errorlevel%"=="0" exit /b 1
cl /nologo /W4 /WX /c /I..\..\ad104-msi\vendor\edk2\Include /I..\..\ad104-msi\vendor\edk2\Include\X64 src\console_state.c /Fobuild\forbidden-console.obj >build\console-no-profile.log 2>&1
if "%errorlevel%"=="0" exit /b 1
cl /nologo /W4 /WX /c /DCS_REAL_PROFILE /DCS_SYNTHETIC_PROFILE /I..\..\ad104-msi\vendor\edk2\Include /I..\..\ad104-msi\vendor\edk2\Include\X64 src\console_state.c /Fobuild\forbidden-console.obj >build\console-mixed-profile.log 2>&1
if "%errorlevel%"=="0" exit /b 1
cl /nologo /W4 /WX /c /DCS_SYNTHETIC_PROFILE /DGU_UEFI_PORT /I..\..\ad104-msi\vendor\edk2\Include /I..\..\ad104-msi\vendor\edk2\Include\X64 src\console_state.c /Fobuild\forbidden-console.obj >build\console-unlock-profile.log 2>&1
if "%errorlevel%"=="0" exit /b 1
exit /b 0
'@
$cmd=$cmd.Replace('@VCVARS@',$vcvars).Replace('@ROOT@',$PSScriptRoot)
Set-Content (Join-Path $build 'host-only-validation.cmd') $cmd -Encoding ascii
& $env:ComSpec /d /c (Join-Path $build 'host-only-validation.cmd')
if($LASTEXITCODE -ne 0){throw 'CPU compilation or C tests failed'}
& $Python -m unittest discover -s (Join-Path $PSScriptRoot 'tests') -p 'test_*.py'
if($LASTEXITCODE -ne 0){throw 'Python tests failed'}
& $Python (Join-Path $PSScriptRoot 'tools/verify.py')
if($LASTEXITCODE -ne 0){throw 'Source or previous EFI sanity check failed'}
& (Join-Path $PSScriptRoot 'tests/test_identity.ps1')
$hashes=[ordered]@{}
foreach($folder in @('src','tests','tools')) {
 Get-ChildItem -LiteralPath (Join-Path $PSScriptRoot $folder) -File | Sort-Object Name | ForEach-Object {
  $hashes[$folder+'/'+$_.Name]=(Get-FileHash -LiteralPath $_.FullName).Hash.ToLowerInvariant()
 }
}
$hashes['Test-Host.ps1']=(Get-FileHash -LiteralPath $PSCommandPath).Hash.ToLowerInvariant()
[ordered]@{tests_passed=$true;firmware_executed=$false;gpu_access=$false;efi_built=$false;source_sha256=$hashes;checked_efi_is_previous_binary=$true;utc=[DateTime]::UtcNow.ToString('o')} | ConvertTo-Json -Depth 4 | Set-Content (Join-Path $build 'host-only-manifest.json')
