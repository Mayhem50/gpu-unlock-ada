[CmdletBinding()]
param()

$ErrorActionPreference = 'Continue'

function Invoke-ReadOnlyCommand {
    param([Parameter(Mandatory)][string]$Name, [Parameter(Mandatory)][scriptblock]$Command)
    Write-Output "`n=== $Name ==="
    try { & $Command } catch { Write-Warning $_.Exception.Message }
}

# This collector deliberately has no output-file parameter and never calls an
# EFI program, changes BCD, mounts the ESP, changes a driver, or writes GPU MMIO.
Invoke-ReadOnlyCommand 'GPU identity and limits (NVML)' {
    & nvidia-smi --query-gpu=name,pci.bus_id,pci.device_id,pci.sub_device_id,driver_version,vbios_version,power.limit,power.default_limit,pstate --format=csv,noheader
    & nvidia-smi -q -d POWER,PERFORMANCE,TEMPERATURE,UTILIZATION
}

Invoke-ReadOnlyCommand 'Present NVIDIA PCI functions' {
    Get-PnpDevice -PresentOnly |
        Where-Object { $_.InstanceId -like 'PCI\VEN_10DE*' } |
        Select-Object Status, Class, FriendlyName, InstanceId |
        Format-List
}

Invoke-ReadOnlyCommand 'Platform firmware and Windows security state' {
    Get-CimInstance Win32_ComputerSystem | Select-Object Manufacturer, Model | Format-List
    Get-CimInstance Win32_BIOS | Select-Object Manufacturer, SMBIOSBIOSVersion, ReleaseDate | Format-List
    (Get-ComputerInfo -Property BiosFirmwareType).BiosFirmwareType
    Get-CimInstance -Namespace root\Microsoft\Windows\DeviceGuard -ClassName Win32_DeviceGuard |
        Select-Object VirtualizationBasedSecurityStatus, SecurityServicesConfigured,
            SecurityServicesRunning, CodeIntegrityPolicyEnforcementStatus |
        Format-List
    try { Confirm-SecureBootUEFI } catch { "Secure Boot status unavailable: $($_.Exception.Message)" }
}

Invoke-ReadOnlyCommand 'Boot configuration access check' {
    & bcdedit /enum '{bootmgr}'
}

