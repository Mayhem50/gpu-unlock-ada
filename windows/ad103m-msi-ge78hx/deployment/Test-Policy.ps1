$ErrorActionPreference='Stop'
. (Join-Path $PSScriptRoot 'Policy.ps1')
$pin=[pscustomobject]@{drive='E';serial='synthetic-usb';size=1000;partition_guid='synthetic-guid';label='GPU_DIAG'}
$good=[ordered]@{drive='E';bus='USB';serial='synthetic-usb';size=1000;partition_guid='synthetic-guid';filesystem='FAT32';label='GPU_DIAG'}
Assert-DiagnosticUsb ([pscustomobject]$good) $pin
$cases=1
foreach($key in $good.Keys) {
 $bad=[ordered]@{};foreach($k in $good.Keys){$bad[$k]=$good[$k]}
 $bad[$key]=if($key -eq 'size'){999}else{'incorrect'}
 $refused=$false;try{Assert-DiagnosticUsb ([pscustomobject]$bad) $pin}catch{$refused=$true}
 if(-not $refused){throw ('Did not refuse changed USB '+$key)};$cases++
}
$m=[ordered]@{version='0.4.4';binary='AD103M-PROBE.EFI';tests_passed=$true;packet_ready=$true;execution_approved=$false;gpu_write_capability=$false;unlock_capability=$false;build_id='5be4190808ab2fd9c320ec7018b066f5d75922de4df2fd093f189f9adf72afc3';sha256='a97ebe9678e0d786ec12fdf32885415d51ed62f63e3243504ab2b38b999328f2'}
Assert-DiagnosticPacket ([pscustomobject]$m) $m.sha256 38912;$cases++
foreach($key in $m.Keys) {
 $bad=[ordered]@{};foreach($k in $m.Keys){$bad[$k]=$m[$k]}
 $bad[$key]=if($m[$key] -is [bool]){-not $m[$key]}else{'incorrect'}
 $refused=$false;try{Assert-DiagnosticPacket ([pscustomobject]$bad) $m.sha256 38912}catch{$refused=$true}
 if(-not $refused){throw ('Did not refuse changed packet '+$key)};$cases++
}
foreach($check in @(@('bad-hash',38912),@($m.sha256,1))) {
 $refused=$false;try{Assert-DiagnosticPacket ([pscustomobject]$m) $check[0] $check[1]}catch{$refused=$true}
 if(-not $refused){throw 'Changed packet bytes admitted'};$cases++
}
Write-Output ('PASS: '+$cases+' staging policy cases; RAM-only, no devices or file mutations')
