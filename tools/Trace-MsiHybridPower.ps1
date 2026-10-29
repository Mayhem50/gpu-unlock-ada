param(
 [ValidateSet('Probe','Live','ArmBoot','StopBoot','VerifyBoot')][string]$Mode='Live',
 [Parameter(Mandatory=$true)][string]$OutputDirectory,
 [string]$Python='python'
)
$ErrorActionPreference='Stop'
$OutputDirectory=[IO.Path]::GetFullPath($OutputDirectory)
[void][IO.Directory]::CreateDirectory($OutputDirectory)
$identity=[Security.Principal.WindowsIdentity]::GetCurrent()
if(-not ([Security.Principal.WindowsPrincipal]::new($identity)).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)){throw 'Administrator required for ETW'}
$root=Split-Path -Parent $PSScriptRoot
$traceProfile=(Join-Path $PSScriptRoot 'msi-hybrid-power.wprp')+'!MsiHybridPower'
$taskName='MSI2717-HybridPower-OneBoot'
function Wpr([string[]]$Arguments,[string]$LogName){
 & "$env:SystemRoot/System32/wpr.exe" @Arguments *> (Join-Path $OutputDirectory $LogName)
 if($LASTEXITCODE -ne 0){throw "WPR failed ($LASTEXITCODE); see $LogName"}
}
function Mark([string]$Name){
 [ordered]@{utc=[DateTime]::UtcNow.ToString('o');phase=$Name} | ConvertTo-Json -Compress | Add-Content (Join-Path $OutputDirectory 'phases.jsonl') -Encoding utf8
}
function Snapshot {
 $gpu=Get-PnpDevice -Class Display | Select-Object FriendlyName,InstanceId,Status
 $nv=$gpu | Where-Object InstanceId -Like 'PCI\VEN_10DE&DEV_2717*'
 if(@($nv).Count -ne 1){throw 'Expected one NVIDIA 2717'}
 $props=Get-PnpDeviceProperty -InstanceId $nv.InstanceId | Where-Object KeyName -Match 'Power|Bus|Location|Driver|Problem|Service' | Select-Object KeyName,Type,Data
 [ordered]@{utc=[DateTime]::UtcNow.ToString('o');boot_utc=(Get-CimInstance Win32_OperatingSystem).LastBootUpTime.ToUniversalTime().ToString('o');devices=$gpu;nvidia_properties=$props;identity=$identity.Name;raw_gpu_register_access=$false;power_settings_changed=$false} | ConvertTo-Json -Depth 8 | Set-Content (Join-Path $OutputDirectory 'snapshot.json') -Encoding utf8
}
try {
 if($Mode -eq 'Probe'){
  Wpr @('-start',$traceProfile,'-filemode','-recordtempto',$OutputDirectory) 'start.log'
  try {
   Wpr @('-status','collectors','-details') 'status.log'
   Get-ItemProperty -LiteralPath 'HKLM:/SYSTEM/CurrentControlSet/Control/Class/{4d36e968-e325-11ce-bfc1-08002be10318}/0001' -Name EnableRuntimePowerManagement -ErrorAction SilentlyContinue | Select-Object EnableRuntimePowerManagement | ConvertTo-Json | Set-Content (Join-Path $OutputDirectory 'registry-probe.json') -Encoding utf8
   Start-Sleep -Seconds 5
  }
  finally {Wpr @('-stop',(Join-Path $OutputDirectory 'probe.etl'),'MSI2717 provider validation') 'stop.log'}
  @{status='captured';mode=$Mode} | ConvertTo-Json | Set-Content (Join-Path $OutputDirectory 'result.json') -Encoding utf8
 } elseif($Mode -eq 'Live'){
  Snapshot
  Wpr @('-start',$traceProfile,'-filemode','-recordtempto',$OutputDirectory) 'start.log'
  try {
   Mark 'idle-before-no-NVML'; Start-Sleep -Seconds 20
   Mark 'single-matched-benchmark-start'
   $python=(Get-Command -Name $Python -CommandType Application -ErrorAction Stop).Source
   $env:CUDA_VISIBLE_DEVICES='GPU-ea71890f-a456-ef46-4715-eb6166e50012'
   $env:CUDA_DEVICE_ORDER='PCI_BUS_ID'
   & $python (Join-Path $root 'windows/ad104-msi/benchmark.py') --out (Join-Path $OutputDirectory 'benchmark.json') --label hybrid-u6-live-etw --size 4096 --samples 15 --batch 5 --warmup 3 *> (Join-Path $OutputDirectory 'benchmark.log')
   $benchExit=$LASTEXITCODE
   Mark 'single-matched-benchmark-end'
   Start-Sleep -Seconds 20
   Mark 'idle-after-no-NVML'
   if($benchExit -ne 0){throw "Benchmark exit $benchExit"}
  } finally {Wpr @('-stop',(Join-Path $OutputDirectory 'live.etl'),'MSI2717 Hybrid U6 idle CUDA idle') 'stop.log'}
  [ordered]@{status='captured';mode=$Mode;utc=[DateTime]::UtcNow.ToString('o');trace='live.etl';boot_initialization_included=$false} | ConvertTo-Json | Set-Content (Join-Path $OutputDirectory 'result.json') -Encoding utf8
 } elseif($Mode -eq 'ArmBoot') {
  $efi='E:/EFI/BOOT/BOOTX64.EFI'
  $expected='83f8c6656c87c2575f23f46a0d67641d19f9cea909aed35fa7bbf58d3b8d6dd7'
  if((Get-FileHash -LiteralPath $efi).Hash.ToLowerInvariant() -ne $expected){throw 'Expected Hybrid U6 on Kingston before arming boot trace'}
  if(Get-ScheduledTask -TaskName $taskName -ErrorAction SilentlyContinue){throw 'Existing MSI boot capture task; do not overwrite'}
  $wprKey='HKLM:/SYSTEM/CurrentControlSet/Control/WMI/Autologger'
  $existing=@(Get-ChildItem -LiteralPath $wprKey | Where-Object {$_.PSChildName -like 'WPR_initiated*' -and (Get-ItemProperty -LiteralPath $_.PSPath -ErrorAction SilentlyContinue).Start -ne 0})
  if($existing.Count){throw 'Existing active WPR boot logger; do not overwrite'}
  Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'msi-hybrid-power.wprp') -Destination (Join-Path $OutputDirectory 'profile.wprp')
  $exe=(Get-Process -Id $PID).Path
  $args='-NoProfile -ExecutionPolicy Bypass -File "'+$PSCommandPath+'" -Mode StopBoot -OutputDirectory "'+$OutputDirectory+'"'
  $action=New-ScheduledTaskAction -Execute $exe -Argument $args
  $trigger=New-ScheduledTaskTrigger -AtStartup
  $trigger.Delay='PT180S'
  $settings=New-ScheduledTaskSettingsSet -ExecutionTimeLimit (New-TimeSpan -Minutes 5) -AllowStartIfOnBatteries -DontStopIfGoingOnBatteries -StartWhenAvailable
  Register-ScheduledTask -TaskName $taskName -Action $action -Trigger $trigger -Settings $settings -User SYSTEM -RunLevel Highest -Description 'Stop one MSI GPU power boot trace after 180 seconds; removes itself on success' | Out-Null
  try {Wpr @('-boottrace','-addboot',((Join-Path $OutputDirectory 'profile.wprp')+'!MsiHybridPower'),'-filemode','-recordtempto',$OutputDirectory) 'arm.log'}
  catch {Unregister-ScheduledTask -TaskName $taskName -Confirm:$false;throw}
  Export-ScheduledTask -TaskName $taskName | Set-Content (Join-Path $OutputDirectory 'stop-task.xml') -Encoding utf8
  [ordered]@{status='armed';mode=$Mode;utc=[DateTime]::UtcNow.ToString('o');stop_after_boot_seconds=180;task=$taskName;requires_manual_boot=$true;efi='HYBRID U6';efi_sha256=$expected;profile_sha256=(Get-FileHash (Join-Path $OutputDirectory 'profile.wprp')).Hash.ToLowerInvariant();boot_logger_keys=@(Get-ChildItem -LiteralPath $wprKey | Where-Object PSChildName -Like 'WPR_initiated*' | ForEach-Object {$_.PSChildName})} | ConvertTo-Json -Depth 4 | Set-Content (Join-Path $OutputDirectory 'result.json') -Encoding utf8
 } elseif($Mode -eq 'VerifyBoot') {
  $task=Get-ScheduledTask -TaskName $taskName
  $key='HKLM:/SYSTEM/CurrentControlSet/Control/WMI/Autologger/WPR_initiated_WprApp_boottr_MSI2717 Hybrid Power'
  $logger=Get-ItemProperty -LiteralPath $key
  if($logger.Start -ne 1){throw 'Boot autologger not enabled'}
  $providers=@(Get-ChildItem -LiteralPath $key | ForEach-Object {$_.PSChildName.ToLowerInvariant()})
  foreach($guid in @('{802ec45a-1e99-4b83-9920-87c98277ba9d}','{331c3b3a-2005-44c2-ac5e-77220c37d6b4}','{9c205a39-1250-487d-abd7-e831c6290539}','{c514638f-7723-485b-bcfc-96565d735d4a}','{70eb4f03-c1de-4f73-a051-33d13d5413bd}')){if($providers -notcontains $guid){throw "Missing boot provider $guid"}}
  [ordered]@{verified=$true;logger_start=$logger.Start;providers=$providers;task_state=[string]$task.State;task_action=$task.Actions.Arguments;trigger_delay=$task.Triggers.Delay;utc=[DateTime]::UtcNow.ToString('o')} | ConvertTo-Json -Depth 5 | Set-Content (Join-Path $OutputDirectory 'verification.json') -Encoding utf8
 } else {
  Mark 'automatic-stop-boot'
  Wpr @('-boottrace','-stopboot',(Join-Path $OutputDirectory 'boot.etl'),'MSI2717 Hybrid U6 initialization and first 180 seconds') 'stop-boot.log'
  Unregister-ScheduledTask -TaskName $taskName -Confirm:$false -ErrorAction SilentlyContinue
  Snapshot
  [ordered]@{status='captured';mode=$Mode;utc=[DateTime]::UtcNow.ToString('o');trace='boot.etl';boot_initialization_included=$true;auto_stop_task_removed=$true} | ConvertTo-Json | Set-Content (Join-Path $OutputDirectory 'result.json') -Encoding utf8
 }
} catch {
 $_ | Out-String | Set-Content (Join-Path $OutputDirectory 'error.txt') -Encoding utf8
 throw
}
