param([Parameter(Mandatory=$true)][string]$OutputDirectory,[string]$Python='python')
$ErrorActionPreference='Stop'
$OutputDirectory=[IO.Path]::GetFullPath($OutputDirectory)
$root=Split-Path -Parent $PSScriptRoot
$service='MSI2717_READONLY_PMXDRV'
$driver=Join-Path $OutputDirectory 'pmxdrv.sys'
$created=$false;$hold=$null;$bodyError=$null;$cleanup=@()
function Find-Bar {
 $dev=@(Get-PnpDevice -Class Display | Where-Object {$_.InstanceId -like 'PCI\VEN_10DE&DEV_2717&SUBSYS_13CD1462&REV_A1\*'})
 if($dev.Count -ne 1 -or $dev[0].Status -ne 'OK'){throw 'Exact healthy NVIDIA 2717/1462:13cd required'}
 $id=$dev[0].InstanceId
 $ranges=@(Get-CimInstance Win32_PnPAllocatedResource | Where-Object {$_.Dependent.DeviceID -eq $id -and $_.Antecedent -match 'Memory'} | ForEach-Object {$m=Get-CimInstance $_.Antecedent;[pscustomobject]@{InstanceId=$id;Start=[uint64]$m.StartingAddress;End=[uint64]$m.EndingAddress;Bytes=([uint64]$m.EndingAddress-[uint64]$m.StartingAddress+1)}})
 $bar=@($ranges | Where-Object {$_.Bytes -eq 16777216})
 if($bar.Count -ne 1){throw 'Unique current Windows 16 MiB BAR required'}
 return $bar[0]
}
try {
 $principal=[Security.Principal.WindowsPrincipal]::new([Security.Principal.WindowsIdentity]::GetCurrent())
 if(-not $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)){throw 'Elevation required'}
 if((Get-FileHash -LiteralPath $driver).Hash.ToLowerInvariant() -ne 'b1a8ee1222eea5f199028d90b9b77c2acf46d6d84a9e125403b2888c6f681c72'){throw 'Driver hash mismatch'}
 $sig=Get-AuthenticodeSignature -LiteralPath $driver
 if($sig.Status -ne 'Valid' -or $sig.SignerCertificate.Subject -notmatch 'O=Intel Corporation'){throw 'Expected valid Intel driver signature'}
 if(Get-Service -Name $service -ErrorAction SilentlyContinue){throw 'Service already exists; no takeover'}
 & (Join-Path $root 'build/power-traces/read_msi_selectors.exe') --check-device *> (Join-Path $OutputDirectory 'device-check.log')
 if($LASTEXITCODE -ne 0){throw 'Existing or inaccessible PMXDRV device; refuse takeover'}
} catch {
 $bodyError=$_
}
if($bodyError){$bodyError | Out-String | Set-Content (Join-Path $OutputDirectory 'error.txt');throw $bodyError}
try {
 $bar=Find-Bar
 $bar | ConvertTo-Json | Set-Content (Join-Path $OutputDirectory 'windows-bar-before.json') -Encoding utf8
 & sc.exe create $service type= kernel start= demand binPath= $driver *> (Join-Path $OutputDirectory 'service-create.log')
 if($LASTEXITCODE -ne 0){throw 'Driver service create failed'}
 $created=$true
 & sc.exe start $service *> (Join-Path $OutputDirectory 'service-start.log')
 if($LASTEXITCODE -ne 0){throw 'Windows refused driver start; retain protections unchanged'}
 $python=(Get-Command -Name $Python -CommandType Application -ErrorAction Stop).Source
 $holdScript=Join-Path $PSScriptRoot 'hold_msi_awake.py'
 $hold=Start-Process -FilePath $python -ArgumentList ('"'+$holdScript+'" "'+$OutputDirectory+'"') -WindowStyle Hidden -RedirectStandardOutput (Join-Path $OutputDirectory 'awake.log') -RedirectStandardError (Join-Path $OutputDirectory 'awake-errors.log') -PassThru
 $deadline=[DateTime]::UtcNow.AddSeconds(45)
 while(-not (Test-Path (Join-Path $OutputDirectory 'awake-ready.json')) -and -not $hold.HasExited -and [DateTime]::UtcNow -lt $deadline){Start-Sleep -Milliseconds 200}
 if(-not (Test-Path (Join-Path $OutputDirectory 'awake-ready.json')) -or $hold.HasExited){throw 'GPU awake context unavailable; refuse MMIO read'}
 $barNow=Find-Bar
 if($barNow.InstanceId -ne $bar.InstanceId -or $barNow.Start -ne $bar.Start -or $barNow.End -ne $bar.End){throw 'Windows resource changed before map'}
 & (Join-Path $root 'build/power-traces/read_msi_selectors.exe') ([string]$barNow.Start) > (Join-Path $OutputDirectory 'registers.json') 2> (Join-Path $OutputDirectory 'reader-errors.txt')
 if($LASTEXITCODE -ne 0){throw "Register reader exit $LASTEXITCODE"}
 $barAfter=Find-Bar
 if($barAfter.Start -ne $bar.Start -or $barAfter.End -ne $bar.End){throw 'Windows resource changed during read'}
 $barAfter | ConvertTo-Json | Set-Content (Join-Path $OutputDirectory 'windows-bar-after.json') -Encoding utf8
} catch {$bodyError=$_;$_ | Out-String | Set-Content (Join-Path $OutputDirectory 'error.txt') -Encoding utf8}
finally {
 Set-Content (Join-Path $OutputDirectory 'awake-stop.txt') 'stop'
 if($hold){if(-not $hold.WaitForExit(10000)){$hold.Kill();$cleanup+='Awake helper exceeded cleanup wait; terminated'}}
 if($created){
  & sc.exe stop $service *> (Join-Path $OutputDirectory 'service-stop.log')
  $deadline=[DateTime]::UtcNow.AddSeconds(10)
  while((Get-Service -Name $service -ErrorAction SilentlyContinue).Status -eq 'StopPending' -and [DateTime]::UtcNow -lt $deadline){Start-Sleep -Milliseconds 200}
  if((Get-Service -Name $service -ErrorAction SilentlyContinue).Status -ne 'Stopped'){$cleanup+='Driver did not stop'}
  & sc.exe delete $service *> (Join-Path $OutputDirectory 'service-delete.log')
  if($LASTEXITCODE -ne 0){$cleanup+='Service delete failed'}
 }
 [ordered]@{utc=[DateTime]::UtcNow.ToString('o');service_created=$created;service_still_present=[bool](Get-Service -Name $service -ErrorAction SilentlyContinue);cleanup_errors=$cleanup;register_writes=0;read_succeeded=(-not $bodyError -and (Test-Path (Join-Path $OutputDirectory 'registers.json')))} | ConvertTo-Json -Depth 4 | Set-Content (Join-Path $OutputDirectory 'result.json') -Encoding utf8
}
if($bodyError){throw $bodyError}
if($cleanup.Count){throw 'Reader cleanup incomplete; inspect result.json'}
