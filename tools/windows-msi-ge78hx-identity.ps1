function Assert-MsiGpuIdentity {
    param([string[]]$InstanceIds)
    $gpus = @($InstanceIds | Where-Object { $_ -match '^PCI\\VEN_10DE&DEV_2717&' })
    if ($gpus.Count -ne 1 -or $gpus[0] -notmatch '^PCI\\VEN_10DE&DEV_2717&SUBSYS_13CD1462&REV_A1\\') {
        throw 'Expected exactly one MSI 10DE:2717 / 1462:13CD / A1 GPU.'
    }
}
function Assert-MsiNvmlIdentity {
    param([string[]]$Rows)
    if ($Rows.Count -ne 1) { throw 'Expected exactly one NVML GPU row.' }
    $f = @($Rows[0].Split(',') | ForEach-Object { $_.Trim() })
    if ($f.Count -ne 5 -or $f[1] -ine '0x271710DE' -or $f[2] -ine '0x13CD1462' -or
        $f[3] -ne '595.79' -or $f[4] -ine '95.03.2A.00.2A') { throw 'NVML identity/driver/VBIOS mismatch.' }
}
