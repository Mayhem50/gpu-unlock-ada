param([string]$Python='python',[string]$VcVars='',[string]$EspConfirmation='')
$ErrorActionPreference='Stop'
$build=Join-Path $PSScriptRoot 'build'
[void][IO.Directory]::CreateDirectory($build)
[ordered]@{tests_passed=$false;hardware_tested=$false;deployed=$false} | ConvertTo-Json | Set-Content (Join-Path $build 'manifest.json')
& (Join-Path $PSScriptRoot 'Prepare-Profile.ps1') -EspConfirmation $EspConfirmation
if(-not $VcVars){
 $finder=[Environment]::ExpandEnvironmentVariables('%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe')
 $install=& $finder -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
 $VcVars=Join-Path $install 'VC\Auxiliary\Build\vcvars64.bat'
}
if(-not(Test-Path -LiteralPath $VcVars)){throw 'MSVC x64 toolchain missing'}
if($VcVars -match '["&|<>%^!\r\n]' -or $PSScriptRoot -match '["&|<>%^!\r\n]'){throw 'Unsupported shell path'}
$cmd=@'
@echo off
call "@VCVARS@" >nul
if errorlevel 1 exit /b 1
cd /d "@ROOT@"
cl /nologo /std:c11 /c /I..\..\ad104-msi /I..\..\ad104-msi\vendor\edk2\Include /I..\..\ad104-msi\vendor\edk2\Include\X64 src\app.c /Fobuild\forbidden.obj >build\guard-unselected.log 2>&1
if not errorlevel 1 exit /b 1
cl /nologo /std:c11 /c /DSTOCK_HOST_TEST /DSTOCK_EFI_BUILD /I..\..\ad104-msi /I..\..\ad104-msi\vendor\edk2\Include /I..\..\ad104-msi\vendor\edk2\Include\X64 src\app.c /Fobuild\forbidden.obj >build\guard-mixed.log 2>&1
if not errorlevel 1 exit /b 1
cl /nologo /std:c11 /c /DSTOCK_EFI_BUILD /DGU_UEFI_PORT /I..\..\ad104-msi /I..\..\ad104-msi\vendor\edk2\Include /I..\..\ad104-msi\vendor\edk2\Include\X64 src\app.c /Fobuild\forbidden.obj >build\guard-unlock.log 2>&1
if not errorlevel 1 exit /b 1
where cl >build\compiler-path.txt
where link >build\linker-path.txt
cl /nologo /std:c11 /W4 /WX /Od /DSTOCK_HOST_TEST /D_CRT_SECURE_NO_WARNINGS /I..\..\ad104-msi /I..\..\ad104-msi\vendor\edk2\Include /I..\..\ad104-msi\vendor\edk2\Include\X64 tests\host_tests.c ..\..\ad104-msi\sha256.c /Fobuild\ /Febuild\stock-host-tests.exe
if errorlevel 1 exit /b 1
build\stock-host-tests.exe >build\host-validation.log 2>&1
if errorlevel 1 exit /b 1
cl /nologo /std:c11 /W4 /WX /O2 /Oi /GS- /Gs65536 /Zl /DSTOCK_EFI_BUILD /Ibuild /I..\..\ad104-msi /I..\..\ad104-msi\vendor\edk2\Include /I..\..\ad104-msi\vendor\edk2\Include\X64 /c src\app.c /Fobuild\app.obj
if errorlevel 1 exit /b 1
cl /nologo /std:c11 /W4 /WX /O2 /Oi /GS- /Gs65536 /Zl /DSTOCK_EFI_BUILD /Ibuild /I..\..\ad104-msi\vendor\edk2\Include /I..\..\ad104-msi\vendor\edk2\Include\X64 /c src\entry.c /Fobuild\entry.obj
if errorlevel 1 exit /b 1
cl /nologo /W4 /WX /O2 /Oi /GS- /Gs65536 /Zl /I..\..\ad104-msi\vendor\edk2\Include /I..\..\ad104-msi\vendor\edk2\Include\X64 /c ..\..\ad104-msi\sha256.c /Fobuild\sha256.obj
if errorlevel 1 exit /b 1
cl /nologo /W4 /WX /Od /Oi- /GS- /Zl /I..\..\ad104-msi\vendor\edk2\Include /I..\..\ad104-msi\vendor\edk2\Include\X64 /c ..\diagnostic\src\runtime.c /Fobuild\runtime.obj
if errorlevel 1 exit /b 1
link /nologo /Brepro /subsystem:efi_application /entry:UefiMain /machine:x64 /nodefaultlib /incremental:no /fixed:no /dynamicbase:no /nxcompat:no /out:build\MSI-STOCK-A.EFI build\app.obj build\entry.obj build\sha256.obj build\runtime.obj
exit /b %errorlevel%
'@
$cmd=$cmd.Replace('@VCVARS@',$VcVars).Replace('@ROOT@',$PSScriptRoot)
Set-Content (Join-Path $build 'compile.cmd') -Value $cmd -Encoding ascii
& $env:ComSpec /d /c (Join-Path $build 'compile.cmd')
if($LASTEXITCODE -ne 0){Get-Content (Join-Path $build 'host-validation.log') -Tail 10 -ErrorAction SilentlyContinue;throw 'Stock-only compilation/tests failed'}
Get-Content (Join-Path $build 'host-validation.log')
& $Python (Join-Path $PSScriptRoot 'tools/verify.py')
if($LASTEXITCODE -ne 0){throw 'Stock-only source/PE verification failed'}
$hashes=[ordered]@{}
foreach($file in @('src/app.c','src/entry.c','src/stock.h','tests/host_tests.c','tools/verify.py','Prepare-Profile.ps1','Stage-Usb.ps1','Build.ps1','build/profile.h','build/profile.json','../../ad104-msi/sha256.c','../../ad104-msi/sha256.h','../diagnostic/src/runtime.c','../diagnostic/tools/verify.py','../../ad104-msi/vendor/edk2/headers.json')){
 $hashes[$file]=(Get-FileHash -LiteralPath (Join-Path $PSScriptRoot $file)).Hash.ToLowerInvariant()
}
$binary=Join-Path $build 'MSI-STOCK-A.EFI'
$toolchain=[ordered]@{}
foreach($name in @('compiler','linker')){
 $path=(Get-Content (Join-Path $build ($name+'-path.txt')) -First 1)
 $toolchain[$name]=[ordered]@{path=$path;sha256=(Get-FileHash -LiteralPath $path).Hash.ToLowerInvariant();version=(Get-Item -LiteralPath $path).VersionInfo.FileVersion}
}
$profile=Get-Content (Join-Path $build 'profile.json') -Raw | ConvertFrom-Json
[ordered]@{purpose='Stage A Windows Boot Manager only';tests_passed=$true;sha256=(Get-FileHash $binary).Hash.ToLowerInvariant();source_sha256=$hashes;toolchain=$toolchain;esp_confirmation_pending=(-not($profile.esp_bytes_read_and_matched -or $profile.operator_esp_confirmed));hardware_tested=$false;deployed=$false;firmware_executed=$false;gpu_access_during_build=$false;gpu_protocols=$false;gpu_release=$false;gpu_dma=$false;gpu_reset=$false;unlock_payload=$false;signed=$false;utc=[DateTime]::UtcNow.ToString('o')} | ConvertTo-Json -Depth 5 | Set-Content (Join-Path $build 'manifest.json') -Encoding utf8
Write-Output $binary
