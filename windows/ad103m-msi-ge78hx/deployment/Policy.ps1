$ErrorActionPreference='Stop'
function Assert-DiagnosticUsb($Observed,$Pin) {
    if(-not $Pin -or $Pin.drive -cne 'E' -or -not $Pin.serial -or
       $Observed.drive -cne 'E' -or $Observed.bus -cne 'USB' -or
       $Observed.serial -cne $Pin.serial -or $Observed.size -ne $Pin.size -or
       $Observed.partition_guid -cne $Pin.partition_guid -or
       $Observed.filesystem -cne 'FAT32' -or $Observed.label -cne $Pin.label) {
        throw 'USB identity mismatch; refuse staging'
    }
}
function Assert-DiagnosticPacket($Manifest,[string]$Hash,[long]$Bytes) {
    if($Manifest.version -cne '0.4.4' -or $Manifest.binary -cne 'AD103M-PROBE.EFI' -or
       $Manifest.tests_passed -cne $true -or $Manifest.packet_ready -cne $true -or
       $Manifest.execution_approved -cne $false -or $Manifest.gpu_write_capability -cne $false -or
       $Manifest.unlock_capability -cne $false -or
       $Manifest.build_id -cne '5be4190808ab2fd9c320ec7018b066f5d75922de4df2fd093f189f9adf72afc3' -or
       $Manifest.sha256 -cne 'a97ebe9678e0d786ec12fdf32885415d51ed62f63e3243504ab2b38b999328f2' -or
       $Hash -cne $Manifest.sha256 -or $Bytes -ne 38912) {
        throw 'Not the reviewed diagnostic packet; refuse staging'
    }
}
