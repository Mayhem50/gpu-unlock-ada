param([string]$Gcc="$env:USERPROFILE/scoop/apps/gcc/current/bin/gcc.exe",[switch]$ReuseCoreValidation)
$ErrorActionPreference='Stop'
$build=Join-Path $PSScriptRoot 'build/hybrid-unlock'
[void][IO.Directory]::CreateDirectory($build)
if($ReuseCoreValidation){
 $priorManifest=Join-Path $build ('prior-core-'+(Get-FileHash (Join-Path $build 'manifest.json')).Hash.Substring(0,12)+'.json')
 Copy-Item -LiteralPath (Join-Path $build 'manifest.json') -Destination $priorManifest
} elseif(Test-Path (Join-Path $build 'core-reuse.json')){Remove-Item -LiteralPath (Join-Path $build 'core-reuse.json')}
@{complete=$false;hardware_unlock_validated=$false;mode='hybrid-native-U6'} | ConvertTo-Json | Set-Content (Join-Path $build 'manifest.json')
& python (Join-Path $PSScriptRoot 'prepare_hybrid_unlock.py')
if($LASTEXITCODE -ne 0){throw 'Hybrid U source preparation failed'}
if($ReuseCoreValidation){
 & python (Join-Path $PSScriptRoot 'reuse_hybrid_core_validation.py') --prior $priorManifest
 if($LASTEXITCODE -ne 0){throw 'Prior core validation is not reusable; run full build without reuse'}
}
$savedPath=$env:Path
try {
 $env:Path=(Split-Path -Parent $Gcc)+';'+$env:Path
 $src=Join-Path $build 'source'
 $common=@('-std=c11','-O2','-Wall','-Wextra','-Werror','-Wno-unused-parameter','-Wno-misleading-indentation','-D__KERNEL__','-DGU_UEFI_PORT','-DGU_AD104_TRANSACTION','-DGU_ENTRY_LATCHED_SS1_FULL',"-I$src/compat","-I$src/vendor/edk2/Include","-I$src/vendor/edk2/Include/X64")
 $backendTest=Join-Path $build 'native-hybrid-tests.exe'
 & $Gcc @common -Wno-unused-function -Wno-unused-variable "$src/tests/test_hybrid_native.c" "$src/port.c" "$src/checks.c" "$src/hybrid_core.c" "$src/hybrid_policy.c" -o $backendTest
 if($LASTEXITCODE -ne 0){throw 'Hybrid native backend test compile failed'}
 & $backendTest *> (Join-Path $build 'backend-tests.log')
 if($LASTEXITCODE -ne 0){Get-Content (Join-Path $build 'backend-tests.log');throw 'Hybrid native backend tests failed'}
 Get-Content (Join-Path $build 'backend-tests.log')
 $appTest=Join-Path $build 'native-hybrid-app-tests.exe'
 & $Gcc @common "$src/tests/test_hybrid_app.c" -o $appTest
 if($LASTEXITCODE -ne 0){throw 'Hybrid U application test compile failed'}
 & $appTest *> (Join-Path $build 'app-tests.log')
 if($LASTEXITCODE -ne 0){Get-Content (Join-Path $build 'app-tests.log');throw 'Hybrid U application tests failed'}
 Get-Content (Join-Path $build 'app-tests.log')
 $profileTest=Join-Path $build 'native-hybrid-profile-tests.exe'
 & $Gcc @common "$src/tests/test_hybrid_profile.c" "$src/profile.c" "$src/checks.c" "$src/sha256.c" "$src/vendor/vbios_610.c" -o $profileTest
 if($LASTEXITCODE -ne 0){throw 'Hybrid exact-selector tests compile failed'}
 & $profileTest *> (Join-Path $build 'profile-tests.log')
 if($LASTEXITCODE -ne 0){Get-Content (Join-Path $build 'profile-tests.log');throw 'Hybrid exact-selector tests failed'}
 Get-Content (Join-Path $build 'profile-tests.log') -Tail 2
 $windowsProfile=Get-Content (Join-Path $PSScriptRoot 'build/windows-profile.json') -Raw | ConvertFrom-Json
 $bootTest=Join-Path $build 'native-hybrid-boot-tests.exe'
 & $Gcc @common "$src/tests/test_boot_io.c" "$src/sha256.c" "$src/format.c" -o $bootTest
 if($LASTEXITCODE -ne 0){throw 'Hybrid Windows staging tests compile failed'}
 & $bootTest $windowsProfile.local_copy (Join-Path $PSScriptRoot 'build/assets') *> (Join-Path $build 'boot-tests.log')
 if($LASTEXITCODE -ne 0){Get-Content (Join-Path $build 'boot-tests.log') -Tail 5;throw 'Hybrid Windows staging tests failed'}
 Get-Content (Join-Path $build 'boot-tests.log') -Tail 1
 $coreTest=Join-Path $build 'native-hybrid-core-tests.exe'
 $sources=@('tests/test_efi_transaction.c','port.c','compat.c','checks.c','profile.c','sha256.c','vendor/unlock_610.c','vendor/prep_610.c','vendor/layout_610.c','vendor/vbios_610.c','vendor/gfw_eval.c') | ForEach-Object {Join-Path $src $_}
 & $Gcc @common @sources -o $coreTest
 if($LASTEXITCODE -ne 0){throw 'Hybrid core test compile failed'}
 & (Join-Path $src 'Build-Experimental.ps1') -Gcc $Gcc
 if(-not $?){throw 'Hybrid native EFI build failed'}
 Copy-Item -LiteralPath (Join-Path $src 'build/AD104-EXPERIMENTAL.EFI') -Destination (Join-Path $build 'MSI2717-HYBRID-U6.EFI')
 if(-not $ReuseCoreValidation){
  Write-Output 'Running real native core/EFI transport regression: 240 cases / eight entry selectors including unseen values; full drift checks retained.'
  & $coreTest (Join-Path $PSScriptRoot 'build/capture-prom.bin') (Join-Path $PSScriptRoot 'build/assets') *> (Join-Path $build 'core-tests.log')
  if($LASTEXITCODE -ne 0){Get-Content (Join-Path $build 'core-tests.log') -Tail 15;throw 'Hybrid native core regression failed'}
 }
 Get-Content (Join-Path $build 'core-tests.log') -Tail 1
 & python (Join-Path $PSScriptRoot 'verify_hybrid_unlock.py')
 if($LASTEXITCODE -ne 0){throw 'Hybrid U final verification failed'}
} finally {$env:Path=$savedPath}
