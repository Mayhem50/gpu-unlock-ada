param([Parameter(Mandatory=$true)][string]$TracePath,[switch]$DxgOnly)
$ErrorActionPreference='Stop'
$TracePath=(Resolve-Path -LiteralPath $TracePath).Path
$out=Split-Path -Parent $TracePath
$native=Join-Path (Split-Path -Parent $PSScriptRoot) 'build/power-traces/read_msi_etl.exe'
if(Test-Path -LiteralPath $native){
 & $native $TracePath (Join-Path $out 'dxg-native.jsonl') *> (Join-Path $out 'native-reader-result.json')
 if($LASTEXITCODE -ne 0){throw 'Native ProcessTrace/TDH decode failed'}
 $validation=Get-Content (Join-Path $out 'native-reader-result.json') -Raw | ConvertFrom-Json
 if($validation.process_trace_status -ne 0 -or $validation.dxg_events -eq 0){throw 'Trace contains no confirmed DXG provider events'}
 [ordered]@{trace=$TracePath;sha256=(Get-FileHash -LiteralPath $TracePath).Hash.ToLowerInvariant();reader='Windows SDK ProcessTrace/TDH';selected_dxg_events=$validation.selected_events;events_lost=$validation.events_lost;utc=[DateTime]::UtcNow.ToString('o')} | ConvertTo-Json | Set-Content (Join-Path $out 'export-native-result.json') -Encoding utf8
 Get-Content (Join-Path $out 'export-native-result.json')
 return
}
throw 'Build tools/read_msi_etl.cpp first: Get-WinEvent ID filtering returned false empty results on these ETLs; do not infer absence from it.'
