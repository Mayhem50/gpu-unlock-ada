param(
 [Parameter(Mandatory=$true)][string]$PolicyDirectory,
 [Parameter(Mandatory=$true)][string]$DriverPath,
 [string]$Python='python',
 [string]$TraceDirectory,
 [switch]$WithoutBootTrace
)
$ErrorActionPreference='Stop'
$root=Split-Path -Parent $PSScriptRoot
$policy=(Resolve-Path -LiteralPath $PolicyDirectory).Path
$traceDir=$null
if(-not $WithoutBootTrace){
 if(-not $TraceDirectory){throw 'Specify -TraceDirectory or -WithoutBootTrace'}
 $traceDir=(Resolve-Path -LiteralPath $TraceDirectory).Path
 $capture=Get-Content (Join-Path $traceDir 'result.json') -Raw | ConvertFrom-Json
 if($capture.status -ne 'captured' -or -not $capture.boot_initialization_included){throw 'Boot capture has not completed; keep this boot running'}
}
$appliedRaw=Get-Content (Join-Path $policy 'apply-result.json') -Raw
$applied=$appliedRaw | ConvertFrom-Json
$boot=(Get-CimInstance Win32_OperatingSystem).LastBootUpTime.ToUniversalTime().ToString('o')
# Compare instants, not DateTime ticks with different Local/Utc kinds.
# ConvertFrom-Json may materialize ISO values as DateTime; retain the
# original explicit offset for this guard on all PowerShell versions.
$appliedIso=[regex]::Match($appliedRaw,'"utc"\s*:\s*"([^"]+)"').Groups[1].Value
if(-not $appliedIso){throw 'Missing policy apply timestamp'}
$applyInstant=[DateTimeOffset]::Parse($appliedIso)
$bootInstant=[DateTimeOffset]::Parse($boot)
if($bootInstant.ToUnixTimeMilliseconds() -le $applyInstant.ToUnixTimeMilliseconds()){throw 'A new U6 boot is required to test the override'}
$inspectDir=Join-Path $policy ('postboot-inspect-'+[DateTime]::UtcNow.ToString('yyyyMMddTHHmmssZ'))
& (Join-Path $PSScriptRoot 'Set-MsiHybridRuntimePower.ps1') -Action Inspect -EvidenceDirectory $inspectDir
if(-not $?){throw 'Power/display inspection failed'}
$inspection=Get-Content (Join-Path $inspectDir 'inspect-result.json') -Raw | ConvertFrom-Json
if(-not $inspection.after.exists -or $inspection.after.value -ne 0){throw 'Expected target runtime-power override is absent'}
if($traceDir){
 & (Join-Path $PSScriptRoot 'Export-MsiPowerTrace.ps1') -TracePath (Join-Path $traceDir 'boot.etl') -DxgOnly
 if(-not $?){throw 'Native ETL decode failed'}
 & $Python (Join-Path $PSScriptRoot 'analyze_msi_power_trace.py') $traceDir *> (Join-Path $traceDir 'analysis.log')
 if($LASTEXITCODE -ne 0){throw 'Power analysis failed'}
}
$readDir=Join-Path $policy ('postboot-read-'+[DateTime]::UtcNow.ToString('yyyyMMddTHHmmssZ'))
[void][IO.Directory]::CreateDirectory($readDir)
Copy-Item -LiteralPath $DriverPath -Destination (Join-Path $readDir 'pmxdrv.sys')
& (Join-Path $PSScriptRoot 'Read-MsiSelectors.ps1') -OutputDirectory $readDir -Python $Python
if(-not $?){throw 'Windows register read failed'}
$regs=Get-Content (Join-Path $readDir 'registers.json') -Raw | ConvertFrom-Json
$full=($regs.FEAT_PLM -eq 'ffffffff' -and $regs.SS0 -eq '88888888' -and $regs.SS1 -eq '00000008' -and $regs.READOUT_1 -eq '00000000')
$result=[ordered]@{utc=[DateTime]::UtcNow.ToString('o');boot_utc=$boot;trace=$traceDir;boot_trace_available=[bool]$traceDir;windows_registers=$regs;read_directory=$readDir;full_unlock_survived=$full;intel_route_verified=$true;benchmark_required=$full;physical_brightness_operator_check_required=$true}
$result | ConvertTo-Json -Depth 6 | Set-Content (Join-Path $policy 'postboot-result.json') -Encoding utf8
if($full){
 $benchmarkDirectory=Join-Path $root ('build/benchmarks/hybrid-u6-rtd3off-matched-'+[DateTime]::UtcNow.ToString('yyyyMMddTHHmmssZ'))
 & $Python (Join-Path $PSScriptRoot 'Run-MsiMatchedBenchmark.py') --output $benchmarkDirectory --label hybrid-U6-runtime-power-off
 if($LASTEXITCODE -ne 0){throw 'Matched benchmark failed'}
}
$result | ConvertTo-Json -Depth 6
