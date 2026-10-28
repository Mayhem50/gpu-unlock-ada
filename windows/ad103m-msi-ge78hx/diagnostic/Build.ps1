param(
    [string]$VcVars = 'C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat',
    [string]$Python = 'python'
)
$ErrorActionPreference = 'Stop'
if (-not (Test-Path -LiteralPath $VcVars)) {
    $vswhere = [Environment]::ExpandEnvironmentVariables('%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe')
    if (Test-Path -LiteralPath $vswhere) {
        $install = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
        if ($install) { $VcVars = Join-Path $install 'VC\Auxiliary\Build\vcvars64.bat' }
    }
}
if (-not (Test-Path -LiteralPath $VcVars)) { throw "Missing MSVC setup: $VcVars" }
if ($VcVars -match '["&|<>%^!\r\n]' -or $PSScriptRoot -match '["&|<>%^!\r\n]') { throw 'Unsupported command-shell characters in build paths' }
$buildDir = Join-Path $PSScriptRoot 'build'
New-Item -ItemType Directory -Path $buildDir -Force | Out-Null
$previous=Join-Path $buildDir 'AD103M-PROBE.EFI'
if(Test-Path -LiteralPath $previous) {
    $archive=Join-Path $buildDir ('archive/'+[DateTime]::UtcNow.ToString('yyyyMMddTHHmmssfffffffZ'))
    [void][IO.Directory]::CreateDirectory($archive)
    foreach($name in @('AD103M-PROBE.EFI','manifest.json','qualification.json','build_id.h')) {
        $old=Join-Path $buildDir $name
        if(Test-Path -LiteralPath $old) {
            Copy-Item -LiteralPath $old -Destination (Join-Path $archive $name)
            if((Get-FileHash -LiteralPath $old).Hash -cne (Get-FileHash -LiteralPath (Join-Path $archive $name)).Hash){throw 'Archive verification failed'}
        }
    }
}
@{tests_passed=$false;firmware_executed=$false;deployed=$false} | ConvertTo-Json | Set-Content (Join-Path $buildDir 'manifest.json')
@{tests_passed=$false;firmware_executed=$false;deployed=$false} | ConvertTo-Json | Set-Content (Join-Path $buildDir 'qualification.json')
& $Python (Join-Path $PSScriptRoot 'tools/packet.py') preflight
if($LASTEXITCODE -ne 0){throw 'Pinned header or input preflight failed'}
$sourceHashes = [ordered]@{}
Get-ChildItem -LiteralPath (Join-Path $PSScriptRoot 'src') -File | Sort-Object Name | ForEach-Object { $sourceHashes[$_.Name] = (Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash.ToLowerInvariant() }
$identityText = (($sourceHashes.GetEnumerator() | ForEach-Object { $_.Name + '=' + $_.Value }) -join "`n")
$identityText += "`nheaders=" + (Get-FileHash -LiteralPath (Join-Path $PSScriptRoot '..\..\ad104-msi\vendor\edk2\headers.json') -Algorithm SHA256).Hash
$identityText += "`nbuild=" + (Get-FileHash -LiteralPath $PSCommandPath -Algorithm SHA256).Hash
$hasher = [Security.Cryptography.SHA256]::Create()
try { $buildId = [BitConverter]::ToString($hasher.ComputeHash([Text.Encoding]::UTF8.GetBytes($identityText))).Replace('-','').ToLowerInvariant() } finally { $hasher.Dispose() }
Set-Content -LiteralPath (Join-Path $buildDir 'build_id.h') -Value ('#define PROBE_BUILD_ID "' + $buildId + '"') -Encoding ascii
$script = @'
@echo off
call "@VCVARS@" >nul
if not "%errorlevel%"=="0" exit /b 1
cd /d "@ROOT@"
if not "%errorlevel%"=="0" exit /b 1
where cl >build\compiler-path.txt
if not "%errorlevel%"=="0" exit /b 1
where link >build\linker-path.txt
if not "%errorlevel%"=="0" exit /b 1
cl /nologo /W4 /WX /O2 /Oi /GS- /Gs65536 /Zl /c /TC /I..\..\ad104-msi\vendor\edk2\Include /I..\..\ad104-msi\vendor\edk2\Include\X64 /Fobuild\core.obj src\core.c
if not "%errorlevel%"=="0" exit /b 1
cl /nologo /W4 /WX /O2 /Oi /GS- /Gs65536 /Zl /c /TC /FIbuild_id.h /Ibuild /I..\..\ad104-msi\vendor\edk2\Include /I..\..\ad104-msi\vendor\edk2\Include\X64 /Fobuild\probe.obj src\probe.c
if not "%errorlevel%"=="0" exit /b 1
cl /nologo /W4 /WX /O2 /Oi /GS- /Gs65536 /Zl /c /TC /I..\..\ad104-msi\vendor\edk2\Include /I..\..\ad104-msi\vendor\edk2\Include\X64 /Fobuild\sample.obj src\sample.c
if not "%errorlevel%"=="0" exit /b 1
cl /nologo /W4 /WX /O2 /Oi /GS- /Gs65536 /Zl /c /TC /I..\..\ad104-msi\vendor\edk2\Include /I..\..\ad104-msi\vendor\edk2\Include\X64 /Fobuild\platform.obj src\platform.c
if not "%errorlevel%"=="0" exit /b 1
cl /nologo /W4 /WX /Od /Oi- /GS- /Zl /c /TC /I..\..\ad104-msi\vendor\edk2\Include /I..\..\ad104-msi\vendor\edk2\Include\X64 /Fobuild\runtime.obj src\runtime.c
if not "%errorlevel%"=="0" exit /b 1
cl /nologo /W4 /WX /O2 /Oi /GS- /Gs65536 /Zl /DCS_REAL_PROFILE /I..\..\ad104-msi\vendor\edk2\Include /I..\..\ad104-msi\vendor\edk2\Include\X64 /c src\console_state.c /Fobuild\console_state.obj
if not "%errorlevel%"=="0" exit /b 1
link /nologo /Brepro /subsystem:efi_application /entry:UefiMain /machine:x64 /nodefaultlib /incremental:no /fixed:no /dynamicbase:no /nxcompat:no /out:build\AD103M-PROBE.EFI build\core.obj build\probe.obj build\sample.obj build\platform.obj build\console_state.obj build\runtime.obj
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
$script = $script.Replace('@VCVARS@', $VcVars).Replace('@ROOT@', $PSScriptRoot)
$cmdFile = Join-Path $buildDir 'compile.cmd'
Set-Content -LiteralPath $cmdFile -Value $script -Encoding ascii
& $env:ComSpec /d /c $cmdFile
if ($LASTEXITCODE -ne 0) { throw 'Compiler or host test failure' }
& $Python (Join-Path $PSScriptRoot 'tools\verify.py')
if ($LASTEXITCODE -ne 0) { throw 'PE/source verification failure' }
& $Python -m unittest discover -s (Join-Path $PSScriptRoot 'tests') -p 'test_*.py' -v
if ($LASTEXITCODE -ne 0) { throw 'Python verification test failure' }
& (Join-Path $PSScriptRoot 'tests/test_identity.ps1')
$binary = Join-Path $buildDir 'AD103M-PROBE.EFI'
$toolchain=[ordered]@{}
foreach($name in @('compiler','linker')) {
    $path=Get-Content (Join-Path $buildDir ($name+'-path.txt')) -First 1
    $toolchain[$name]=[ordered]@{path=$path;sha256=(Get-FileHash -LiteralPath $path).Hash.ToLowerInvariant();version=(Get-Item -LiteralPath $path).VersionInfo.FileVersion}
}
$toolchain['vcvars']=[ordered]@{path=$VcVars;sha256=(Get-FileHash -LiteralPath $VcVars).Hash.ToLowerInvariant()}
$manifest = [ordered]@{ version='0.4.4'; build_id=$buildId; binary='AD103M-PROBE.EFI'; sha256=(Get-FileHash -LiteralPath $binary -Algorithm SHA256).Hash.ToLowerInvariant(); source_sha256=$sourceHashes;toolchain=$toolchain; tests_passed=$true;deployed=$false;firmware_executed=$false;hardware_tested=$false; gpu_write_capability=$false; mmio_read_capability=$true; validation='MSVC host mocks and PE checks'; utc=[DateTime]::UtcNow.ToString('o') }
$manifest | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $buildDir 'manifest.json') -Encoding utf8
$qualification = [ordered]@{
 target='10de:2717/1462:13cd/a1'; expected_chip='0x193000a1'; chip_observed_on_hardware=$false
 signed=$false; deployed=$false; firmware_executed=$false; gpu_access_during_build=$false
 header_pins_verified=$true; suites_passed=@('C application mocks','C platform mocks','Python analyzer and PE tests','PowerShell identity tests')
 note='Successful build/test only, not hardware qualification.'
}
$qualification | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $buildDir 'qualification.json') -Encoding utf8
Write-Output $binary
& $Python (Join-Path $PSScriptRoot 'tools/packet.py') seal
if($LASTEXITCODE -ne 0){throw 'Packet sealing failed; no launch packet is approved'}
