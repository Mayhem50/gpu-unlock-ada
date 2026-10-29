param()
$ErrorActionPreference='Stop'
$build=Join-Path $PSScriptRoot 'build/hybrid'
$manifest=Get-Content (Join-Path $build 'manifest.json') -Raw | ConvertFrom-Json
if(-not $manifest.complete -or $manifest.mode -ne 'hybrid-explicit-decode-test' -or -not $manifest.pci_config_write_capability -or $manifest.gpu_mmio_write_capability -or $manifest.unlock_capability -or $manifest.firmware_or_dma_linked){throw 'Not a verified D2 decode-test build'}
$binary=Join-Path $build $manifest.binary
if((Get-FileHash -LiteralPath $binary).Hash.ToLowerInvariant() -ne $manifest.sha256){throw 'EFI source hash mismatch'}
function Assert-HybridUsb {
 $partition=Get-Partition -DriveLetter E
 $disk=$partition | Get-Disk
 $volume=Get-Volume -DriveLetter E
 if($disk.BusType -ne 'USB' -or $disk.SerialNumber.Trim() -ne '6E0EA84049C9' -or $disk.Size -ne 15614803968 -or $partition.Guid -ne '{30d8b4d6-35b6-45bc-85f9-d1fcb463d3c1}' -or $volume.FileSystem -ne 'FAT32' -or $volume.FileSystemLabel -ne 'GPU_DIAG'){throw 'Kingston USB identity mismatch'}
}
Assert-HybridUsb
$old=(Get-FileHash -LiteralPath E:/EFI/BOOT/BOOTX64.EFI).Hash.ToLowerInvariant()
if($old -ne 'd241ad72d976f76b16d8e5215e1f455adaeebd05f1eff4986265b4697a85bb1b'){throw 'Unexpected previous EFI; refusal to overwrite'}
$backup=Join-Path $build ('usb-backup-'+[DateTime]::UtcNow.ToString('yyyyMMdd-HHmmss'))
[void][IO.Directory]::CreateDirectory($backup)
Copy-Item -LiteralPath E:/EFI/BOOT/BOOTX64.EFI -Destination (Join-Path $backup BOOTX64.EFI)
if((Get-FileHash (Join-Path $backup BOOTX64.EFI)).Hash.ToLowerInvariant() -ne $old){throw 'EFI backup mismatch'}
foreach($folder in @('AD103M-PROBE','MSI2717-UNLOCK','MSI2717-HYBRID')){
 $source=Join-Path E:/ $folder
 if(Test-Path -LiteralPath $source){
  $saved=Join-Path $backup $folder
  Copy-Item -LiteralPath $source -Destination $saved -Recurse
  foreach($file in Get-ChildItem -LiteralPath $source -File -Recurse){
   $relative=[IO.Path]::GetRelativePath($source,$file.FullName)
   if((Get-FileHash -LiteralPath $file.FullName).Hash -ne (Get-FileHash -LiteralPath (Join-Path $saved $relative)).Hash){throw 'Log backup mismatch'}
  }
 }
}
Assert-HybridUsb
if((Get-FileHash E:/EFI/BOOT/BOOTX64.EFI).Hash.ToLowerInvariant() -ne $old){throw 'EFI changed during backup'}
$instructions=@'
MSI 2717 HYBRID D2 - explicit PCI decode test, U unavailable
1. Select MSI Hybrid/MSHybrid mode and complete its required restart into Windows.
2. Keep AC connected. Choose Restart, then F11 -> UEFI Kingston (same USB).
3. At MSI 2717 HYBRID D2 press T within 30 seconds.
   T exclusively claims NVIDIA, enables only its PCI memory decode bit,
   reads registers/PROM, restores the full original config and releases claim.
   D or Enter instead performs the unchanged read-only observation.
4. Wait for Saved MSI2717-HYBRID/CAPxxxx.JSON, then return to firmware.
5. Select Windows Boot Manager if Windows does not start automatically.
6. Return to the chat with the Kingston connected. No benchmark needed yet.
T writes NVIDIA PCI Command only; no bus mastering, power transition,
GPU MMIO writes, driver detachment, DMA, GPU firmware execution or unlock.
If PCI restoration cannot be verified, the EFI stops instead of returning.
If no menu appears, wait for Windows and report it; do not launch U elsewhere.
'@
[void][IO.Directory]::CreateDirectory('E:/MSI2717-HYBRID')
Set-Content -LiteralPath E:/MSI2717-HYBRID/PROCEDURE.txt -Value $instructions -Encoding utf8
Copy-Item -LiteralPath (Join-Path $build 'manifest.json') -Destination E:/MSI2717-HYBRID/efi-manifest.json
Copy-Item -LiteralPath $binary -Destination E:/EFI/BOOT/BOOTX64.EFI
Assert-HybridUsb
$readback=(Get-FileHash E:/EFI/BOOT/BOOTX64.EFI).Hash.ToLowerInvariant()
if($readback -ne $manifest.sha256){throw 'USB EFI readback mismatch'}
$result=[ordered]@{status='verified';previous_efi=$old;installed_efi=$readback;backup=$backup;pci_config_write_capability=$true;gpu_mmio_write_capability=$false;unlock_capability=$false;hardware_executed=$false;utc=[DateTime]::UtcNow.ToString('o')}
$result | ConvertTo-Json | Set-Content (Join-Path $backup 'deployment.json') -Encoding utf8
$result | ConvertTo-Json
