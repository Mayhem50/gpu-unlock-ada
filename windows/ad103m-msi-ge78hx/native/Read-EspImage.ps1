param([Parameter(Mandatory=$true)][string]$OutputDirectory)
$ErrorActionPreference='Stop'
# Elevated read-only ESP observation. Writes only to the caller's local output.
$result=[ordered]@{success=$false;esp_modified=$false;protections_modified=$false}
try {
 $expected=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot 'build'))+[IO.Path]::DirectorySeparatorChar
 $out=[IO.Path]::GetFullPath($OutputDirectory)
 if(-not $out.StartsWith($expected,[StringComparison]::OrdinalIgnoreCase)){throw 'Output must be inside native/build'}
 [void][IO.Directory]::CreateDirectory($out)
 $os=Get-Partition -DriveLetter C
 $disk=Get-Disk -Number $os.DiskNumber
 $parts=@(Get-Partition -DiskNumber $os.DiskNumber | Where-Object GptType -EQ '{c12a7328-f81f-11d2-ba4b-00a0c93ec93b}')
 if($disk.PartitionStyle -ne 'GPT' -or $parts.Count -ne 1){throw 'Require one ESP on OS disk'}
 $part=$parts[0]
 $paths=@($part.AccessPaths | Where-Object {$_ -like '\\?\Volume*'})
 if($paths.Count -ne 1){throw 'Ambiguous ESP path'}
 $source=$paths[0]+'EFI\Microsoft\Boot\bootmgfw.efi'
 $bytes=[IO.File]::ReadAllBytes($source)
 $copy=Join-Path $out 'bootmgfw.efi'
 [IO.File]::WriteAllBytes($copy,$bytes)
 $hash=[Convert]::ToHexString([Security.Cryptography.SHA256]::HashData($bytes)).ToLowerInvariant()
 if((Get-FileHash -LiteralPath $copy).Hash.ToLowerInvariant() -ne $hash){throw 'Local copy mismatch'}
 $sig=Get-AuthenticodeSignature -LiteralPath $copy
 $sector=[uint64]$disk.LogicalSectorSize
 $result.success=$true
 $result.source=$source;$result.partition_guid=$part.Guid;$result.partition_number=$part.PartitionNumber
 $result.logical_sector_size=$sector;$result.start_lba=[uint64]($part.Offset/$sector);$result.sectors=[uint64]($part.Size/$sector)
 $result.image_bytes=$bytes.Length;$result.image_sha256=$hash;$result.local_copy=$copy
 $result.authenticode_status=$sig.Status.ToString();$result.signer=$sig.SignerCertificate.Subject
 $result.image_version=(Get-Item -LiteralPath $copy).VersionInfo.FileVersion
 $result.utc=[DateTime]::UtcNow.ToString('o')
} catch {$result.error=$_.Exception.ToString()}
if(Test-Path -LiteralPath $OutputDirectory){$result | ConvertTo-Json -Depth 4 | Set-Content (Join-Path $OutputDirectory 'result.json') -Encoding utf8}
if(-not $result.success){exit 1}
