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
    if($Manifest.version -cne '0.4.3' -or $Manifest.binary -cne 'AD103M-PROBE.EFI' -or
       $Manifest.tests_passed -cne $true -or $Manifest.packet_ready -cne $true -or
       $Manifest.execution_approved -cne $false -or $Manifest.gpu_write_capability -cne $false -or
       $Manifest.unlock_capability -cne $false -or
       $Manifest.build_id -cne 'e704259c189d3cd164212337e4df016ea32f43da0ee416e96cc824e6805435ab' -or
       $Manifest.sha256 -cne '2463cfd10c1524f04b4b4c91b87412d483fa2dccc254d991f07a892647e6cb6c' -or
       $Hash -cne $Manifest.sha256 -or $Bytes -ne 37888) {
        throw 'Not the reviewed diagnostic packet; refuse staging'
    }
}
