[CmdletBinding()]
param([switch]$IncludeGpu, [switch]$ConfirmGpuIdle)
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'windows-msi-ge78hx-identity.ps1')
if ($IncludeGpu -and -not $ConfirmGpuIdle) {
    throw 'NVML requires -ConfirmGpuIdle after the active workload has finished.'
}
# Print only. No EFI execution, boot changes, driver installation or GPU writes.
$failures = [System.Collections.Generic.List[string]]::new()
try {
    $devices = @(Get-PnpDevice -PresentOnly)
    Assert-MsiGpuIdentity -InstanceIds @($devices.InstanceId)
    $devices | Where-Object { $_.Class -eq 'Display' } |
        Select-Object Status,Class,FriendlyName,InstanceId | Format-List
} catch { $failures.Add("PCI identity: $($_.Exception.Message)") }
if ($IncludeGpu) {
    try {
        $rows = @(& nvidia-smi --query-gpu=pci.bus_id,pci.device_id,pci.sub_device_id,driver_version,vbios_version --format=csv,noheader)
        if ($LASTEXITCODE -ne 0) { throw "nvidia-smi failed: $LASTEXITCODE" }
        Assert-MsiNvmlIdentity -Rows $rows
        $rows
        & nvidia-smi -q -d POWER,PERFORMANCE,TEMPERATURE,UTILIZATION
        if ($LASTEXITCODE -ne 0) { throw "Telemetry failed: $LASTEXITCODE" }
    } catch { $failures.Add("NVML: $($_.Exception.Message)") }
} else { Write-Output 'NVML skipped; driver and VBIOS identity remain unverified.' }
try {
    $system = Get-CimInstance Win32_ComputerSystem
    $bios = Get-CimInstance Win32_BIOS
    $system | Select-Object Manufacturer,Model | Format-List
    $bios | Select-Object Manufacturer,SMBIOSBIOSVersion,ReleaseDate | Format-List
    if ($system.Model -notmatch 'Raider GE78\s*HX 13VI' -or $bios.SMBIOSBIOSVersion -ne 'E17S1IMS.114') {
        throw 'System model or BIOS differs from the discovery baseline.'
    }
    (Get-ComputerInfo -Property BiosFirmwareType).BiosFirmwareType
    Get-CimInstance -Namespace root\Microsoft\Windows\DeviceGuard -ClassName Win32_DeviceGuard |
        Select-Object VirtualizationBasedSecurityStatus,SecurityServicesConfigured,SecurityServicesRunning | Format-List
    Write-Output "Secure Boot: $(Confirm-SecureBootUEFI)"
    & bcdedit /enum '{bootmgr}'
    if ($LASTEXITCODE -ne 0) { throw "BCD listing failed: $LASTEXITCODE" }
} catch { $failures.Add("Platform/security: $($_.Exception.Message)") }
if ($failures.Count) {
    $failures | ForEach-Object { Write-Warning $_ }
    Write-Error 'Baseline incomplete or mismatched. No further stage is admitted.' -ErrorAction Continue
    exit 1
}
Write-Output 'Host collection complete; this does not authorize EFI execution or establish unlock support.'

