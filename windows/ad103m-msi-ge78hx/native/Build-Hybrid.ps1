param()
$ErrorActionPreference='Stop'
$build=Join-Path $PSScriptRoot 'build/hybrid'
& python (Join-Path $PSScriptRoot 'prepare_hybrid.py')
if($LASTEXITCODE -ne 0){throw 'Hybrid source preparation failed'}
@{complete=$false;mode='hybrid-explicit-decode-test';unlock_capability=$false} | ConvertTo-Json | Set-Content (Join-Path $build 'manifest.json')
$headers=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../../ad104-msi/vendor/edk2'))
$vswhere=Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
$vcvars=@(& $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -find 'VC/Auxiliary/Build/vcvars64.bat') | Select-Object -First 1
if(!$vcvars -or !(Test-Path -LiteralPath $vcvars)){throw 'MSVC x64 tools missing'}
foreach($path in @($build,$headers,$vcvars)){if($path -match '["&|<>%^!\r\n]'){throw 'Unsupported shell path characters'}}
$lines=@('@echo off',('call "'+$vcvars+'" >nul'),'if errorlevel 1 exit /b 1',('cd /d "'+$build+'"'))
$flags='/nologo /std:c11 /W4 /WX /O2 /Oi /GS- /Gs65536 /Zl /c /TC /DCS_REAL_PROFILE /FIbuild_id.h /Isrc /I"'+$headers+'/Include" /I"'+$headers+'/Include/X64"'
$objects=@('core','probe','sample','platform','console_state','hybrid_policy','hybrid_entry')
foreach($name in $objects){
 $lines+=('cl '+$flags+' src/'+$name+'.c /Fobuild/'+$name+'.obj')
 $lines+='if errorlevel 1 exit /b 1'
}
$lines+=('cl '+$flags.Replace('/O2 /Oi','/Od /Oi-')+' src/runtime.c /Fobuild/runtime.obj')
$lines+='if errorlevel 1 exit /b 1'
$lines+=('link /nologo /Brepro /subsystem:efi_application /entry:UefiMain /machine:x64 /nodefaultlib /incremental:no /fixed:no /dynamicbase:no /nxcompat:no /out:MSI2717-HYBRID-D2.EFI '+(($objects+@('runtime') | ForEach-Object {'build/'+$_+'.obj'}) -join ' '))
$lines+='if errorlevel 1 exit /b 1'
$hostFlags='/nologo /std:c11 /W4 /WX /Od /D_CRT_SECURE_NO_WARNINGS /DCS_REAL_PROFILE /FIbuild_id.h /Isrc /I"'+$headers+'/Include" /I"'+$headers+'/Include/X64"'
$lines+=('cl '+$hostFlags+' tests/host_tests.c src/core.c src/sample.c src/platform.c src/console_state.c src/hybrid_policy.c /Febuild/host-tests.exe /Fobuild/')
$lines+='if errorlevel 1 exit /b 1'
$lines+='build\host-tests.exe >build\host-tests.log 2>&1'
$lines+='if errorlevel 1 exit /b 1'
$lines+=('cl '+$hostFlags+' tests/hybrid_entry_tests.c src/hybrid_entry.c /Febuild/entry-tests.exe /Fobuild/')
$lines+='if errorlevel 1 exit /b 1'
$lines+='build\entry-tests.exe >build\entry-tests.log 2>&1'
$lines+='if errorlevel 1 exit /b 1'
$lines+='exit /b 0'
$cmdFile=Join-Path $build 'compile.cmd'
Set-Content -LiteralPath $cmdFile -Value $lines -Encoding ascii
& $env:ComSpec /d /c $cmdFile
if($LASTEXITCODE -ne 0){throw 'Hybrid EFI compile or application tests failed; see build/hybrid/build logs'}
& python (Join-Path $PSScriptRoot 'verify_hybrid.py')
if($LASTEXITCODE -ne 0){throw 'Hybrid EFI verification failed'}
Get-Content (Join-Path $build 'build/host-tests.log') -Tail 1
Get-Content (Join-Path $build 'build/entry-tests.log') -Tail 1
Get-Content (Join-Path $build 'manifest.json')
