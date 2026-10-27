param([string]$Python='python',[string]$VcVars='')
$ErrorActionPreference='Stop'
$buildDirectory=Join-Path $PSScriptRoot 'build'
[void][IO.Directory]::CreateDirectory($buildDirectory)
[ordered]@{host_only=$true;bootable=$false;tests_passed=$false} | ConvertTo-Json | Set-Content (Join-Path $buildDirectory 'manifest.json') -Encoding utf8
if (-not $VcVars) {
    $finder=[Environment]::ExpandEnvironmentVariables('%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe')
    $install=& $finder -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
    $VcVars=Join-Path $install 'VC\Auxiliary\Build\vcvars64.bat'
}
if(-not(Test-Path -LiteralPath $VcVars)){throw 'MSVC x64 toolchain missing'}
if($VcVars -match '["&|<>%^!\r\n]' -or $PSScriptRoot -match '["&|<>%^!\r\n]'){throw 'Unsupported command path'}
& $Python (Join-Path $PSScriptRoot 'tools/prepare_fixtures.py')
if($LASTEXITCODE -ne 0){throw 'Private capture validation failed'}
$cmd=@'
@echo off
call "@VCVARS@" >nul
if errorlevel 1 exit /b 1
cd /d "@ROOT@"
cl /nologo /std:c11 /c src\session.c /Fobuild\forbidden-native.obj >build\guard-no-host.log 2>&1
if not errorlevel 1 exit /b 1
cl /nologo /std:c11 /DMSI_OFFLINE_HOST /DGU_UEFI_PORT /c src\session.c /Fobuild\forbidden-uefi.obj >build\guard-uefi.log 2>&1
if not errorlevel 1 exit /b 1
rem Upstream parser uses checked u64-to-u32 narrowing; suppress only its C4244.
cl /nologo /std:c11 /W4 /WX /DMSI_OFFLINE_HOST /c tests\forbidden_full_core.c /Fobuild\forbidden-full-core.obj >build\guard-full-core.log 2>&1
if not errorlevel 1 exit /b 1
cl /nologo /std:c11 /W4 /WX /wd4244 /O2 /D_CRT_SECURE_NO_WARNINGS /c ..\..\ad104-msi\vendor\vbios_610.c /Fobuild\vbios_610.obj
if errorlevel 1 exit /b 1
cl /nologo /std:c11 /W4 /WX /O2 /DMSI_OFFLINE_HOST /D_CRT_SECURE_NO_WARNINGS /Ibuild /I..\..\ad104-msi\vendor /I..\..\ad104-msi\vendor\edk2\Include /I..\..\ad104-msi\vendor\edk2\Include\X64 tests\host_tests.c src\profile.c src\ownership.c src\readbacks.c src\reset.c src\session.c ..\diagnostic\src\core.c build\vbios_610.obj /Fobuild\ /Febuild\msi-offline-tests.exe
if errorlevel 1 exit /b 1
build\msi-offline-tests.exe >build\validation.log 2>&1
exit /b %errorlevel%
'@
$cmd=$cmd.Replace('@VCVARS@',$VcVars).Replace('@ROOT@',$PSScriptRoot)
$path=Join-Path $PSScriptRoot 'build/compile.cmd'
Set-Content -LiteralPath $path -Value $cmd -Encoding ascii
& $env:ComSpec /d /c $path
if($LASTEXITCODE -ne 0){Get-Content (Join-Path $PSScriptRoot 'build/validation.log') -Tail 20 -ErrorAction SilentlyContinue;throw 'Offline port tests failed'}
Get-Content (Join-Path $PSScriptRoot 'build/validation.log') -Tail 4
$hashes=[ordered]@{}
Get-ChildItem (Join-Path $PSScriptRoot 'src') -File | Sort-Object Name | ForEach-Object {$hashes['src/'+$_.Name]=(Get-FileHash -LiteralPath $_.FullName).Hash}
Get-ChildItem (Join-Path $PSScriptRoot 'tests') -File | Sort-Object Name | ForEach-Object {$hashes['tests/'+$_.Name]=(Get-FileHash -LiteralPath $_.FullName).Hash}
foreach($file in @('tools/prepare_fixtures.py','Build-Offline.ps1','build/captured_fixture.h','build/prom.bin')){$hashes[$file]=(Get-FileHash -LiteralPath (Join-Path $PSScriptRoot $file)).Hash}
$hashes['shared/vbios_610.c']=(Get-FileHash -LiteralPath (Join-Path $PSScriptRoot '../../ad104-msi/vendor/vbios_610.c')).Hash
$hashes['shared/sha256-core.c']=(Get-FileHash -LiteralPath (Join-Path $PSScriptRoot '../diagnostic/src/core.c')).Hash
[ordered]@{host_only=$true;bootable=$false;tests_passed=$true;firmware_build_guards_tested=$true;hardware_access=$false;native_backend=$false;payload_linked=$false;hardware_qualified=$false;source_sha256=$hashes;test_executable_sha256=(Get-FileHash (Join-Path $PSScriptRoot 'build/msi-offline-tests.exe')).Hash;utc=[DateTime]::UtcNow.ToString('o')} | ConvertTo-Json -Depth 4 | Set-Content (Join-Path $PSScriptRoot 'build/manifest.json') -Encoding utf8
