param([Parameter(Mandatory=$true)][ValidatePattern('^[0-9a-fA-F]{64}$')][string]$ExpectedPreviousEfiSha256)
$ErrorActionPreference='Stop'
$build=Join-Path $PSScriptRoot 'build/hybrid-unlock'
$manifest=Get-Content (Join-Path $build 'manifest.json') -Raw | ConvertFrom-Json
if(-not $manifest.complete -or $manifest.mode -ne 'hybrid-native-U6' -or $manifest.native_core_scenarios -ne 240 -or $manifest.native_backend_scenarios -ne 30 -or -not $manifest.nvidia_memory_decode_enabled_before_windows -or -not $manifest.nvidia_bus_master_disabled_before_windows -or $manifest.original_decode_restored_before_windows -or -not $manifest.ss0_full_transaction_drift_checks -or -not $manifest.unlock_capability -or $manifest.automatic_unlock -or $manifest.intel_driver_detach){throw 'Not a complete manual Hybrid U6 build'}
$packet=Join-Path $build ('package-'+$manifest.sha256.Substring(0,12))
$files=(Get-Content (Join-Path $packet 'package-manifest.json') -Raw | ConvertFrom-Json).files
function Assert-HybridUnlockUsb {
 $partition=Get-Partition -DriveLetter E;$disk=$partition | Get-Disk;$volume=Get-Volume -DriveLetter E
 if($disk.BusType -ne 'USB' -or $disk.SerialNumber.Trim() -ne '6E0EA84049C9' -or $disk.Size -ne 15614803968 -or $partition.Guid -ne '{30d8b4d6-35b6-45bc-85f9-d1fcb463d3c1}' -or $volume.FileSystem -ne 'FAT32' -or $volume.FileSystemLabel -ne 'GPU_DIAG'){throw 'Kingston USB identity mismatch'}
}
foreach($entry in $files.PSObject.Properties){
 $source=[IO.Path]::GetFullPath((Join-Path $packet $entry.Name))
 if(-not $source.StartsWith($packet+[IO.Path]::DirectorySeparatorChar,[StringComparison]::OrdinalIgnoreCase)){throw 'Invalid packet path'}
 if((Get-Item -LiteralPath $source).Length -ne $entry.Value.bytes -or (Get-FileHash -LiteralPath $source).Hash.ToLowerInvariant() -ne $entry.Value.sha256){throw "Packet mismatch: $($entry.Name)"}
}
if($files.'EFI/BOOT/BOOTX64.EFI'.sha256 -ne $manifest.sha256){throw 'EFI manifest mismatch'}
Assert-HybridUnlockUsb
$old=(Get-FileHash -LiteralPath E:/EFI/BOOT/BOOTX64.EFI).Hash.ToLowerInvariant()
if($old -ne $ExpectedPreviousEfiSha256.ToLowerInvariant()){throw 'Unexpected previous EFI; no overwrite'}
$backup=Join-Path $build ('usb-backup-'+[DateTime]::UtcNow.ToString('yyyyMMdd-HHmmss'))
[void][IO.Directory]::CreateDirectory($backup)
Copy-Item -LiteralPath E:/EFI/BOOT/BOOTX64.EFI -Destination (Join-Path $backup BOOTX64.EFI)
if((Get-FileHash (Join-Path $backup BOOTX64.EFI)).Hash.ToLowerInvariant() -ne $old){throw 'EFI backup verification failed'}
foreach($folder in @('AD103M-PROBE','MSI2717-UNLOCK','MSI2717-HYBRID','MSI2717-HYBRID-U')){
 $source=Join-Path E:/ $folder
 if(Test-Path -LiteralPath $source){
  $saved=Join-Path $backup $folder
  Copy-Item -LiteralPath $source -Destination $saved -Recurse
  foreach($file in Get-ChildItem -LiteralPath $source -File -Recurse){
   $relative=[IO.Path]::GetRelativePath($source,$file.FullName)
   if((Get-FileHash -LiteralPath $file.FullName).Hash -ne (Get-FileHash -LiteralPath (Join-Path $saved $relative)).Hash){throw 'USB log backup verification failed'}
  }
 }
}
foreach($entry in $files.PSObject.Properties){
 if($entry.Name -eq 'EFI/BOOT/BOOTX64.EFI'){continue}
 $dest=[IO.Path]::GetFullPath((Join-Path 'E:/' $entry.Name))
 if(-not $dest.StartsWith('E:\',[StringComparison]::OrdinalIgnoreCase)){throw 'Invalid USB path'}
 if(Test-Path -LiteralPath $dest){
  if((Get-FileHash -LiteralPath $dest).Hash.ToLowerInvariant() -eq $entry.Value.sha256){continue}
  $save=Join-Path $backup $entry.Name
  [void][IO.Directory]::CreateDirectory((Split-Path -Parent $save))
  Copy-Item -LiteralPath $dest -Destination $save
  if((Get-FileHash -LiteralPath $save).Hash -ne (Get-FileHash -LiteralPath $dest).Hash){throw 'Replaced file backup mismatch'}
 }
 [void][IO.Directory]::CreateDirectory((Split-Path -Parent $dest))
 Copy-Item -LiteralPath (Join-Path $packet $entry.Name) -Destination $dest
 if((Get-FileHash -LiteralPath $dest).Hash.ToLowerInvariant() -ne $entry.Value.sha256){throw 'USB dependency readback failed'}
}
Assert-HybridUnlockUsb
if((Get-FileHash E:/EFI/BOOT/BOOTX64.EFI).Hash.ToLowerInvariant() -ne $old){throw 'EFI changed during staging'}
Copy-Item -LiteralPath (Join-Path $packet 'EFI/BOOT/BOOTX64.EFI') -Destination E:/EFI/BOOT/BOOTX64.EFI
Assert-HybridUnlockUsb
$actual=(Get-FileHash -LiteralPath E:/EFI/BOOT/BOOTX64.EFI).Hash.ToLowerInvariant()
if($actual -ne $manifest.sha256){throw 'EFI USB readback failed'}
foreach($entry in $files.PSObject.Properties){
 $dest=Join-Path E:/ $entry.Name
 if((Get-Item -LiteralPath $dest).Length -ne $entry.Value.bytes -or (Get-FileHash -LiteralPath $dest).Hash.ToLowerInvariant() -ne $entry.Value.sha256){throw 'Final USB packet verification failed'}
}
$result=[ordered]@{status='verified';mode='hybrid-native-U6';previous_efi=$old;installed_efi=$actual;backup=$backup;firmware_files=5;automatic_unlock=$false;requires_key='U';hardware_executed=$false;utc=[DateTime]::UtcNow.ToString('o')}
$result | ConvertTo-Json | Set-Content (Join-Path $backup 'deployment.json') -Encoding utf8
$result | ConvertTo-Json
