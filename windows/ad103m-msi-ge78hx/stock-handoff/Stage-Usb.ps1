param()
$ErrorActionPreference='Stop'
$build=Join-Path $PSScriptRoot 'build'
$manifestPath=Join-Path $build 'manifest.json'
$manifest=Get-Content -LiteralPath $manifestPath -Raw | ConvertFrom-Json
if(-not $manifest.tests_passed -or $manifest.esp_confirmation_pending){throw 'Do not stage: tests or actual ESP fingerprint confirmation missing'}
$profile=Get-Content -LiteralPath (Join-Path $build 'profile.json') -Raw | ConvertFrom-Json
$pin=$profile.usb_target
if(-not $pin -or $pin.drive -cne 'E' -or -not $pin.serial){throw 'No reviewed USB identity in private profile'}
foreach($item in $manifest.source_sha256.PSObject.Properties){
 $path=Join-Path $PSScriptRoot $item.Name
 if((Get-FileHash -LiteralPath $path).Hash.ToLowerInvariant() -cne $item.Value){throw ('Stale build input: '+$item.Name)}
}
$binary=Join-Path $build 'MSI-STOCK-A.EFI'
if((Get-FileHash -LiteralPath $binary).Hash.ToLowerInvariant() -cne $manifest.sha256){throw 'Binary digest mismatch'}
function Assert-UsbIdentity {
 $partition=Get-Partition -DriveLetter E
 $disk=Get-Disk -Number $partition.DiskNumber
 $volume=Get-Volume -DriveLetter E
 if($disk.BusType -ne 'USB' -or $disk.SerialNumber.Trim() -cne $pin.serial -or
    $disk.Size -ne $pin.size -or $partition.Guid -cne $pin.partition_guid -or
    $volume.FileSystem -ne 'FAT32' -or $volume.FileSystemLabel -cne $pin.label){throw 'USB identity changed; nothing staged'}
}
Assert-UsbIdentity
$target=[IO.Path]::GetFullPath('E:\EFI\BOOT\BOOTX64.EFI')
if($target -cne 'E:\EFI\BOOT\BOOTX64.EFI' -or -not(Test-Path -LiteralPath $target -PathType Leaf)){throw 'Unexpected USB fallback path'}
$previous=(Get-FileHash -LiteralPath $target).Hash.ToLowerInvariant()
if($previous -ceq $manifest.sha256){Write-Output 'Exact stock test already staged; not executed';exit}
if($previous -cne '18efed6c4a60ce0ae682fb9c9d165f425c280a2159e32f57124f8cf3c41703f9'){throw 'USB fallback is not the reviewed diagnostic; refuse to replace it'}
$stamp=[DateTime]::UtcNow.ToString('yyyyMMddTHHmmssfffZ')
$backup=Join-Path $build ('usb-backups/'+$stamp)
[void][IO.Directory]::CreateDirectory($backup)
Copy-Item -LiteralPath $target -Destination (Join-Path $backup 'BOOTX64.EFI')
if((Get-FileHash -LiteralPath (Join-Path $backup 'BOOTX64.EFI')).Hash.ToLowerInvariant() -cne $previous){throw 'Backup verification failed'}
$temporary=[IO.Path]::GetFullPath('E:\EFI\BOOT\STOCK-A-'+$stamp+'.tmp')
if(-not $temporary.StartsWith('E:\EFI\BOOT\',[StringComparison]::Ordinal) -or (Test-Path -LiteralPath $temporary)){throw 'Unsafe temporary target'}
Assert-UsbIdentity
Copy-Item -LiteralPath $binary -Destination $temporary
if((Get-FileHash -LiteralPath $temporary).Hash.ToLowerInvariant() -cne $manifest.sha256){throw 'USB copy verification failed; original fallback retained'}
Assert-UsbIdentity
[IO.File]::Move($temporary,$target,$true)
if((Get-FileHash -LiteralPath $target).Hash.ToLowerInvariant() -cne $manifest.sha256){throw 'Final USB digest mismatch; use local verified backup'}
[ordered]@{purpose=$manifest.purpose;binary_sha256=$manifest.sha256;previous_sha256=$previous;backup=$backup;usb_serial=$pin.serial;firmware_executed=$false;protections_modified=$false;utc=[DateTime]::UtcNow.ToString('o')} | ConvertTo-Json | Set-Content (Join-Path $build 'deployment.json') -Encoding utf8
$manifest.deployed=$true
$manifest | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath $manifestPath -Encoding utf8
Write-Output ('Stock test staged only; not executed. Verified diagnostic backup: '+$backup)
