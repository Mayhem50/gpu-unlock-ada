param([string]$EspConfirmation='',[string]$EspImage='')
$ErrorActionPreference='Stop'
$build=Join-Path $PSScriptRoot 'build'
[void][IO.Directory]::CreateDirectory($build)
$os=Get-Partition -DriveLetter C
$disk=Get-Disk -Number $os.DiskNumber
$esp=@(Get-Partition -DiskNumber $os.DiskNumber | Where-Object GptType -EQ '{c12a7328-f81f-11d2-ba4b-00a0c93ec93b}')
if($disk.PartitionStyle -ne 'GPT' -or $esp.Count -ne 1){throw 'Require one GPT ESP on the Windows disk'}
$part=$esp[0];$sector=[uint64]$disk.LogicalSectorSize
if($sector -ne 512 -and $sector -ne 4096){throw 'Unsupported logical sector size'}
if($part.Offset % $sector -or $part.Size % $sector){throw 'Unaligned ESP geometry'}
$volume=@($part.AccessPaths | Where-Object {$_ -like '\\?\Volume*'})
if($volume.Count -ne 1){throw 'Ambiguous ESP volume path'}
$espPath=$volume[0]+'EFI\Microsoft\Boot\bootmgfw.efi'
$osPath=Join-Path $env:windir 'Boot\EFI\bootmgfw.efi'
$servicingPath=$osPath
if($EspImage){
    if(-not $EspConfirmation){throw 'A selected component-store image requires the actual ESP fingerprint'}
    $candidate=[IO.Path]::GetFullPath((Resolve-Path -LiteralPath $EspImage).Path)
    $store=[IO.Path]::GetFullPath((Join-Path $env:windir 'WinSxS'))+[IO.Path]::DirectorySeparatorChar
    if(-not $candidate.StartsWith($store,[StringComparison]::OrdinalIgnoreCase) -or
       [IO.Path]::GetFileName($candidate) -notin @('bootmgfw.efi','bootmgfw_EX.efi')){throw 'Selected image must be an existing Windows component-store boot manager'}
    $osPath=$candidate
}
$osBytes=[IO.File]::ReadAllBytes($osPath)
$signature=Get-AuthenticodeSignature -LiteralPath $osPath
if($signature.Status -ne 'Valid' -or $signature.SignerCertificate.Subject -notmatch 'CN=Microsoft Windows,'){throw 'Unverified Microsoft Windows image'}
$origin=$osPath;$bytes=$osBytes;$espVerified=$false;$espError=$null
try {
    $espBytes=[IO.File]::ReadAllBytes($espPath)
    if([Convert]::ToHexString([Security.Cryptography.SHA256]::HashData($espBytes)) -ne [Convert]::ToHexString([Security.Cryptography.SHA256]::HashData($osBytes))){throw 'ESP image differs from the selected authenticated Windows image'}
    $bytes=$espBytes;$origin=$espPath;$espVerified=$true
} catch {
    # Access denial can use the authenticated servicing copy as a REQUIRED
    # runtime match. Any other failure/difference is a hard refusal.
    if($_.Exception.ToString() -notmatch 'UnauthorizedAccessException|Access.*denied|acc.s.*refus'){throw}
    $espError=$_.Exception.Message
}
$digest=[Security.Cryptography.SHA256]::HashData($bytes)
$operatorConfirmed=$false;$confirmationHash=$null
if($EspConfirmation){
    $confirmation=Get-Content -LiteralPath $EspConfirmation -Raw | ConvertFrom-Json
    if([uint64]$confirmation.TailleOctets -ne $bytes.Length -or $confirmation.SHA256.ToLowerInvariant() -ne [Convert]::ToHexString($digest).ToLowerInvariant()){throw 'Operator ESP fingerprint does not match the selected authenticated image; do not widen the pin'}
    $operatorConfirmed=$true;$confirmationHash=(Get-FileHash -LiteralPath $EspConfirmation).Hash.ToLowerInvariant()
}
$guid=([Guid]$part.Guid).ToByteArray()
$start=[uint64]($part.Offset/$sector);$sectors=[uint64]($part.Size/$sector)
$profile=[ordered]@{schema=1;purpose='Stage A normal Windows handoff only';partition_guid=$part.Guid;partition_number=$part.PartitionNumber;logical_sector_size=$sector;start_lba=$start;sectors=$sectors;image_bytes=$bytes.Length;image_sha256=[Convert]::ToHexString($digest).ToLowerInvariant();hash_origin=$origin;esp_bytes_read_and_matched=$espVerified;operator_esp_confirmed=$operatorConfirmed;operator_confirmation_sha256=$confirmationHash;esp_read_error=$espError;authenticode_status=$signature.Status.ToString();signer=$signature.SignerCertificate.Subject;gpu_access=$false;esp_modified=$false;protections_modified=$false;utc=[DateTime]::UtcNow.ToString('o')}
$profile.selected_image_version=(Get-Item -LiteralPath $osPath).VersionInfo.FileVersion
$profile.selected_image_issuer=$signature.SignerCertificate.Issuer
$profile.servicing_copy_sha256=(Get-FileHash -LiteralPath $servicingPath).Hash.ToLowerInvariant()
$profile.selected_component_store_image=[bool]$EspImage
$profile.usb_target=$null
if(Test-Path -LiteralPath E:/){
    $usbPartition=Get-Partition -DriveLetter E
    $usbDisk=Get-Disk -Number $usbPartition.DiskNumber
    $usbVolume=Get-Volume -DriveLetter E
    if($usbDisk.BusType -eq 'USB' -and $usbDisk.FriendlyName -eq 'Kingston DataTraveler 3.0' -and $usbVolume.FileSystem -eq 'FAT32'){
        $profile.usb_target=[ordered]@{drive='E';serial=$usbDisk.SerialNumber.Trim();size=$usbDisk.Size;partition_guid=$usbPartition.Guid;label=$usbVolume.FileSystemLabel}
    }
}
$profile | ConvertTo-Json | Set-Content (Join-Path $build 'profile.json') -Encoding utf8
$g=($guid | ForEach-Object {'0x{0:x2}' -f $_}) -join ','
$h=($digest | ForEach-Object {'0x{0:x2}' -f $_}) -join ','
$header='static const STOCK_PROFILE NativeProfile={{'+$g+'},{'+$h+'},'+$part.PartitionNumber+','+$start+'ULL,'+$sectors+'ULL,'+$bytes.Length+'};'
Set-Content (Join-Path $build 'profile.h') -Value $header -Encoding ascii
Write-Output ('Read-only profile prepared; direct ESP image verification: '+$espVerified)
