$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot '..\..\..\..\tools\windows-msi-ge78hx-identity.ps1')
$valid = 'PCI\VEN_10DE&DEV_2717&SUBSYS_13CD1462&REV_A1\mock'
Assert-MsiGpuIdentity @($valid)
$row = '00000000:01:00.0, 0x271710DE, 0x13CD1462, 595.79, 95.03.2a.00.2a'
Assert-MsiNvmlIdentity @($row)
foreach ($bad in @('', $valid.Replace('A1','A2'), $valid.Replace('13CD','13CE'), $valid.Replace('2717','2757'))) {
    $rejected = $false
    try { Assert-MsiGpuIdentity @($bad) } catch { $rejected = $true }
    if (-not $rejected) { throw 'Invalid PCI identity accepted' }
}
$rejected = $false
try { Assert-MsiGpuIdentity @($valid,$valid) } catch { $rejected = $true }
if (-not $rejected) { throw 'Duplicate GPU accepted' }
foreach ($bad in @('', $row.Replace('595.79','616.56'), $row.Replace('2a.00.2a','2a.00.2b'), $row.Replace('2717','2757'))) {
    $rejected = $false
    try { Assert-MsiNvmlIdentity @($bad) } catch { $rejected = $true }
    if (-not $rejected) { throw 'Invalid NVML identity accepted' }
}
Write-Output 'PASS: pure identity tests; no hardware or collector calls.'
