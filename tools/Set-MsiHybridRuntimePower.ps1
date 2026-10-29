param([ValidateSet('Inspect','Apply','Restore')][string]$Action='Inspect',[Parameter(Mandatory=$true)][string]$EvidenceDirectory)
$ErrorActionPreference='Stop'
$EvidenceDirectory=[IO.Path]::GetFullPath($EvidenceDirectory)
[void][IO.Directory]::CreateDirectory($EvidenceDirectory)
$valueName='EnableRuntimePowerManagement'
$classRoot='SYSTEM\CurrentControlSet\Control\Class\'
function ValueState([Microsoft.Win32.RegistryKey]$Key){
 $exists=$Key.GetValueNames() -contains $valueName
 return [ordered]@{exists=$exists;kind=if($exists){[string]$Key.GetValueKind($valueName)}else{$null};value=if($exists){$Key.GetValue($valueName)}else{$null}}
}
function Resolve-Gpu([string]$Pattern){
 $devices=@(Get-PnpDevice -Class Display | Where-Object InstanceId -Like $Pattern)
 if($devices.Count -ne 1 -or $devices[0].Status -ne 'OK'){throw "Expected one healthy device: $Pattern"}
 $driver=(Get-PnpDeviceProperty -InstanceId $devices[0].InstanceId -KeyName DEVPKEY_Device_Driver).Data
 if($driver -notmatch '^\{4d36e968-e325-11ce-bfc1-08002be10318\}\\\d{4}$'){throw 'Unexpected display driver registry path'}
 return [ordered]@{instance=$devices[0].InstanceId;registry=$classRoot+$driver}
}
function Display-Snapshot([string]$Name){
 $binary=Join-Path (Split-Path -Parent $PSScriptRoot) 'build/power-traces/snapshot_msi_display.exe'
 $text=& $binary
 if($LASTEXITCODE -ne 0){throw 'Display route query failed'}
 $text | Set-Content (Join-Path $EvidenceDirectory $Name) -Encoding utf8
 $routes=$text | ConvertFrom-Json
 if(-not @($routes.paths | Where-Object {$_.adapter -match 'VEN_8086&DEV_A788' -and $_.monitor_path -match 'AUO84A6'}).Count){throw 'Expected internal MSI panel driven by Intel'}
 return $text
}
try {
 $nv=Resolve-Gpu 'PCI\VEN_10DE&DEV_2717&SUBSYS_13CD1462&REV_A1\*'
 $intel=Resolve-Gpu 'PCI\VEN_8086&DEV_A788&SUBSYS_13CD1462&REV_04\*'
 if($nv.registry -eq $intel.registry){throw 'GPU keys unexpectedly identical'}
 $beforeDisplay=Display-Snapshot 'display-before.json'
 $intelKey=[Microsoft.Win32.Registry]::LocalMachine.OpenSubKey($intel.registry,$false)
 $intelBefore=ValueState $intelKey
 $writable=$Action -ne 'Inspect'
 $key=[Microsoft.Win32.Registry]::LocalMachine.OpenSubKey($nv.registry,$writable)
 if(-not $key -or $key.GetValue('MatchingDeviceId') -ne 'pci\ven_10de&dev_2717&subsys_13cd1462'){throw 'NVIDIA registry identity mismatch'}
 $state=ValueState $key
 $backupPath=Join-Path $EvidenceDirectory 'original-value.json'
 if($Action -eq 'Apply'){
  if(Test-Path -LiteralPath $backupPath){throw 'Backup already exists; do not overwrite original state'}
  if($key.GetValue('DriverVersion') -ne '32.0.15.9579'){throw 'Expected analyzed NVIDIA 595.79'}
  $systemDriver=(Get-CimInstance Win32_SystemDriver -Filter "Name='nvlddmkm'").PathName
  if((Get-FileHash -LiteralPath $systemDriver).Hash.ToLowerInvariant() -ne '1b2e45b375b9a7fc90d0bc8a85f8841f5737dd91183a8647b003b95e01dc5e1f'){throw 'Analyzed NVIDIA binary changed'}
  if($state.exists -and $state.kind -ne 'DWord'){throw 'Unexpected existing setting type'}
  [ordered]@{utc=[DateTime]::UtcNow.ToString('o');target=$nv;value_name=$valueName;original=$state;intel=$intel;intel_original=$intelBefore;driver_version=$key.GetValue('DriverVersion');driver_sha256='1b2e45b375b9a7fc90d0bc8a85f8841f5737dd91183a8647b003b95e01dc5e1f';applies_after_manual_boot=$true} | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath $backupPath -Encoding utf8
  $saved=Get-Content -LiteralPath $backupPath -Raw | ConvertFrom-Json
  if($saved.target.registry -ne $nv.registry -or $saved.original.exists -ne $state.exists){throw 'Backup readback failed'}
  $key.SetValue($valueName,0,[Microsoft.Win32.RegistryValueKind]::DWord);$key.Flush()
  $after=ValueState $key
  if(-not $after.exists -or $after.kind -ne 'DWord' -or $after.value -ne 0){throw 'NVIDIA setting readback failed'}
 } elseif($Action -eq 'Restore'){
  $saved=Get-Content -LiteralPath $backupPath -Raw | ConvertFrom-Json
  if($saved.target.registry -ne $nv.registry -or $saved.target.instance -ne $nv.instance -or $saved.value_name -ne $valueName){throw 'Backup target mismatch'}
  if($state.exists -and ($state.kind -ne 'DWord' -or $state.value -ne 0)){throw 'Setting changed independently; refuse overwrite'}
  if($saved.original.exists){$key.SetValue($valueName,[int]$saved.original.value,[Microsoft.Win32.RegistryValueKind]::DWord)}else{$key.DeleteValue($valueName,$false)}
  $key.Flush();$after=ValueState $key
  if($after.exists -ne $saved.original.exists -or ($after.exists -and $after.value -ne $saved.original.value)){throw 'Restore readback failed'}
 } else {$after=$state}
 $intelAfter=ValueState $intelKey
 if(($intelBefore | ConvertTo-Json -Compress) -ne ($intelAfter | ConvertTo-Json -Compress)){throw 'Intel value changed during operation'}
 $afterDisplay=Display-Snapshot 'display-after.json'
 if($beforeDisplay -ne $afterDisplay){throw 'Active display routes changed during operation'}
 Get-CimInstance -Namespace root/wmi -ClassName WmiMonitorBrightness -ErrorAction SilentlyContinue | Select-Object Active,InstanceName,CurrentBrightness | ConvertTo-Json | Set-Content (Join-Path $EvidenceDirectory 'brightness-observation.json') -Encoding utf8
 [ordered]@{status='verified';action=$Action;utc=[DateTime]::UtcNow.ToString('o');target=$nv;value_name=$valueName;before=$state;after=$after;intel_value_unchanged=$true;display_routes_unchanged=$true;hardware_fix_validated=$false;manual_U6_boot_required=$true;gpu_reset_performed=$false} | ConvertTo-Json -Depth 6 | Set-Content (Join-Path $EvidenceDirectory ($Action.ToLowerInvariant()+'-result.json')) -Encoding utf8
} catch {$_ | Out-String | Set-Content (Join-Path $EvidenceDirectory 'error.txt') -Encoding utf8;throw}
finally {if($key){$key.Dispose()};if($intelKey){$intelKey.Dispose()}}
