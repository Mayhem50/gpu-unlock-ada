param([string]$Python='python')
$ErrorActionPreference='Stop'
. (Join-Path $PSScriptRoot 'Policy.ps1')
$workspace=Split-Path $PSScriptRoot -Parent
$diagnostic=Join-Path $workspace 'diagnostic'
$packet=Join-Path $diagnostic 'build/packets/diagnostic-0.4.3-2463cfd10c1524f0'
$manifest=Get-Content -LiteralPath (Join-Path $packet 'manifest.json') -Raw | ConvertFrom-Json
function Hash([string]$Path){(Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash.ToLowerInvariant()}
& $Python (Join-Path $diagnostic 'tools/packet.py') verify
if($LASTEXITCODE -ne 0){throw 'Local build verification failed'}
if((Hash (Join-Path $packet 'manifest.json')) -cne (Hash (Join-Path $diagnostic 'build/manifest.json'))){throw 'Packet manifest differs from verified build'}
$binary=Join-Path $packet 'AD103M-PROBE.EFI'
Assert-DiagnosticPacket $manifest (Hash $binary) (Get-Item -LiteralPath $binary).Length
$pin=(Get-Content -LiteralPath (Join-Path $workspace 'stock-handoff/build/profile.json') -Raw | ConvertFrom-Json).usb_target
function CheckUsb {
 $parts=@(Get-Partition -DriveLetter E);if($parts.Count -ne 1){throw 'Ambiguous E: partition'}
 $part=$parts[0];$disk=Get-Disk -Number $part.DiskNumber;$vol=Get-Volume -DriveLetter E
 $observed=[pscustomobject]@{drive='E';bus=[string]$disk.BusType;serial=$disk.SerialNumber.Trim();size=$disk.Size;partition_guid=$part.Guid;filesystem=$vol.FileSystem;label=$vol.FileSystemLabel}
 Assert-DiagnosticUsb $observed $pin
 foreach($path in @('E:\','E:\EFI','E:\EFI\BOOT','E:\EFI\BOOT\BOOTX64.EFI')) {
  $item=Get-Item -LiteralPath $path
  if($item.Attributes -band [IO.FileAttributes]::ReparsePoint){throw 'Refuse redirected USB path'}
 }
}
CheckUsb
$target='E:\EFI\BOOT\BOOTX64.EFI'
$old=Hash $target
if($old -cne '8ee3689bae43e8edd760612c358d059c3b2bc570712ecc0d844dee9170a7b1d0'){throw 'Expected reviewed Stage A as current fallback; nothing replaced'}
$stamp=[DateTime]::UtcNow.ToString('yyyyMMddTHHmmssfffffffZ')
$backup=[IO.Path]::GetFullPath((Join-Path $diagnostic ('build/usb-backups/'+$stamp)))
$buildRoot=[IO.Path]::GetFullPath((Join-Path $diagnostic 'build'))+[IO.Path]::DirectorySeparatorChar
if(-not $backup.StartsWith($buildRoot,[StringComparison]::OrdinalIgnoreCase) -or (Test-Path -LiteralPath $backup)){throw 'Invalid backup destination'}
[void][IO.Directory]::CreateDirectory($backup)
Copy-Item -LiteralPath $target -Destination (Join-Path $backup 'STAGE-A-BOOTX64.EFI')
if((Hash (Join-Path $backup 'STAGE-A-BOOTX64.EFI')) -cne $old){throw 'Stage A backup verification failed'}
$journal=[ordered]@{status='prepared';version='0.4.3';sha256=$manifest.sha256;build_id=$manifest.build_id;previous_sha256=$old;backup=$backup;usb_serial=$pin.serial;usb_partition_guid=$pin.partition_guid;firmware_executed=$false;gpu_access=$false;protections_modified=$false;utc=[DateTime]::UtcNow.ToString('o');staging_script_sha256=(Hash $PSCommandPath);policy_sha256=(Hash (Join-Path $PSScriptRoot 'Policy.ps1'))}
$journalPath=Join-Path $backup 'deployment.json'
function SaveJournal {$journal | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath $journalPath -Encoding utf8}
SaveJournal
$usbPacket='E:\AD103M-PACKETS\'+$manifest.sha256
CheckUsb
if(Test-Path -LiteralPath 'E:\AD103M-PACKETS') {
 if((Get-Item -LiteralPath 'E:\AD103M-PACKETS').Attributes -band [IO.FileAttributes]::ReparsePoint){throw 'Redirected metadata directory'}
}
if(Test-Path -LiteralPath $usbPacket){throw 'Existing packet directory; refuse overwrite'}
[void][IO.Directory]::CreateDirectory($usbPacket)
foreach($name in @('AD103M-PROBE.EFI','manifest.json','LIRE-AVANT-TEST.txt')) {
 CheckUsb
 Copy-Item -LiteralPath (Join-Path $packet $name) -Destination (Join-Path $usbPacket $name)
 if((Hash (Join-Path $packet $name)) -cne (Hash (Join-Path $usbPacket $name))){throw 'USB packet copy mismatch; fallback unchanged'}
}
$journal.status='packet_copied';$journal.usb_packet=$usbPacket;SaveJournal
$temporary='E:\EFI\BOOT\DIAG-043-'+$stamp+'.tmp'
if(Test-Path -LiteralPath $temporary){throw 'Existing temporary path'}
CheckUsb
Copy-Item -LiteralPath $binary -Destination $temporary
if((Hash $temporary) -cne $manifest.sha256){throw 'Temporary binary mismatch; fallback unchanged'}
CheckUsb
if((Hash $target) -cne $old -or (Hash $temporary) -cne $manifest.sha256){throw 'USB fallback or staged bytes drifted; nothing replaced'}
[IO.File]::Move($temporary,$target,$true)
$journal.status='fallback_replaced';SaveJournal
CheckUsb
if((Hash $target) -cne $manifest.sha256){throw 'Final fallback verification failed; use verified Stage A backup'}
$journal.status='verified';$journal.deployed=$true;SaveJournal
Copy-Item -LiteralPath $journalPath -Destination (Join-Path $diagnostic 'build/deployment-043.json')
Write-Output ('Diagnostic 0.4.3 staged, NOT EXECUTED. Stage A backup: '+$backup)
