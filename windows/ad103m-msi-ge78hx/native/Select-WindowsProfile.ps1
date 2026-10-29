param([Parameter(Mandatory=$true)][string]$Observation)
$ErrorActionPreference='Stop'
$r=Get-Content -LiteralPath $Observation -Raw | ConvertFrom-Json
if(-not $r.success -or $r.esp_modified -or $r.protections_modified -or $r.authenticode_status -ne 'Valid' -or $r.signer -notmatch 'CN=Microsoft Windows,'){throw 'Invalid direct ESP observation'}
if($r.partition_guid -ne '{afc33558-0b71-4e83-ad8f-95ea14671843}' -or $r.partition_number -ne 1 -or $r.start_lba -ne 2048 -or $r.sectors -ne 614400){throw 'Unexpected ESP identity/geometry'}
$bytes=[IO.File]::ReadAllBytes($r.local_copy)
if($bytes.Length -ne $r.image_bytes -or (Get-FileHash -LiteralPath $r.local_copy).Hash.ToLowerInvariant() -ne $r.image_sha256){throw 'ESP copy drifted'}
$sig=Get-AuthenticodeSignature -LiteralPath $r.local_copy
if($sig.Status -ne 'Valid' -or $sig.SignerCertificate.Subject -notmatch 'CN=Microsoft Windows,'){throw 'Current signature verification failed'}
$pe=[BitConverter]::ToInt32($bytes,60)
if($bytes.Length -lt 256 -or $bytes[0] -ne 77 -or $bytes[1] -ne 90 -or $pe -lt 64 -or $pe -gt $bytes.Length-100 -or [BitConverter]::ToUInt32($bytes,$pe) -ne 0x4550 -or [BitConverter]::ToUInt16($bytes,$pe+4) -ne 0x8664 -or [BitConverter]::ToUInt16($bytes,$pe+24) -ne 0x20b -or [BitConverter]::ToUInt16($bytes,$pe+24+68) -ne 10){throw 'Expected x64 EFI application'}
$out=Join-Path $PSScriptRoot 'build/windows-profile.json'
if(Test-Path -LiteralPath $out){Copy-Item -LiteralPath $out -Destination ($out+'.'+[DateTime]::UtcNow.ToString('yyyyMMdd-HHmmss')+'.backup')}
$profile=[ordered]@{partition_guid=$r.partition_guid;partition_number=$r.partition_number;logical_sector_size=$r.logical_sector_size;start_lba=$r.start_lba;sectors=$r.sectors;image_bytes=$r.image_bytes;image_sha256=$r.image_sha256;image_version=$r.image_version;local_copy=$r.local_copy;hash_origin=$r.source;esp_bytes_read_and_matched=$true;operator_esp_confirmed=$false;authenticode_status='Valid';observation_sha256=(Get-FileHash -LiteralPath $Observation).Hash.ToLowerInvariant();observation=[IO.Path]::GetFullPath($Observation);esp_modified=$false}
$profile | ConvertTo-Json | Set-Content -LiteralPath $out -Encoding utf8
Write-Output ('Selected actual ESP image: '+$r.image_version+' / '+$r.image_bytes+' bytes / '+$r.image_sha256)
