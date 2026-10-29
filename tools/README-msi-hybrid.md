# MSI 2717 Windows Hybrid tools

These tools support the exact MSI GE78HX 2717/13cd + Intel A788/AUO84A6
configuration. See [hardware results](../docs/windows-msi-ge78hx-hybrid-runtime-power-validation.md).
All output directories below are local and ignored. Run commands from the
repository root; replace placeholders with absolute paths.

## Build and policy

```powershell
pwsh -File tools/Build-MsiWindowsTools.ps1
pwsh -File tools/Set-MsiHybridRuntimePower.ps1 -Action Inspect -EvidenceDirectory build/policy-inspection
```

The build discovers MSVC with vswhere and compiles three C++ tools into
`build/power-traces/`. It does not load a driver or query GPU registers.
The policy tool queries the active Windows display routes using User32 and
requires the MSI internal screen on Intel. It dynamically resolves adapter
class keys and never assumes NVIDIA is key 0001 on another installation.

In an elevated PowerShell, `-Action Apply` with a **new** evidence directory
backs up the original value, sets only NVIDIA EnableRuntimePowerManagement
DWORD 0, verifies readback and confirms Intel value/routes stayed unchanged.
Apply is pinned to the analyzed NVIDIA 595.79 driver version and binary hash.
The change takes effect on the next operator-initiated Windows boot; the tool
does not reset a GPU or reboot. The original state is required for rollback:

```powershell
pwsh -File tools/Set-MsiHybridRuntimePower.ps1 -Action Restore -EvidenceDirectory <original-policy-evidence-directory>
```

Keep that directory. Do not apply again over the existing backup. Restore
returns the exact saved value or removes it if originally absent, followed
by a normal manual reboot. Runtime-power savings for NVIDIA are disabled by
the workaround; battery impact, sleep/resume and hibernation are unmeasured.

## Register verification and benchmark

Select a Python environment with working CUDA PyTorch. The matched runs used
Python 3.11.9, torch 2.7.1+cu128 and cuBLAS 120803. Use that interpreter explicitly
for a controlled comparison. The benchmark and awake helper pin the captured
GPU UUID; this is a one-machine reference.

`Read-MsiSelectors.ps1` requires elevation, a fresh output directory containing
`pmxdrv.sys`, and `-Python <CUDA-Python-executable>`. The driver is an external
dependency, not shipped in this repository. The tested upstream source is
`physicsdolphin/nv-uefi-unlocker/tools/ssdiag/pmxdrv.sys`, SHA256
`b1a8ee1222eea5f199028d90b9b77c2acf46d6d84a9e125403b2888c6f681c72`.
The wrapper checks that hash and the valid Intel signature before loading its
own temporary kernel service; it refuses existing services/devices and does
not change Windows protections. It discovers the current Windows BAR0,
keeps NVIDIA awake through a small CUDA job, reads only BOOT_0/FEAT_PLM/SS0/
SS1/READOUT_1, then unmaps and unloads/deletes its own service. It has no
register-write or selector-reapply path.

After a manual Hybrid U6 boot, the completion helper verifies a newer boot
than the policy application, checks the Intel route and policy, performs the
read, and benchmarks only if every unlock register matches:

```powershell
pwsh -File tools/Complete-MsiHybridRuntimeTest.ps1 -WithoutBootTrace -PolicyDirectory <original-policy-evidence-directory> -DriverPath <verified-pmxdrv.sys-path> -Python <CUDA-Python-executable>
```

Physical brightness must still be checked by the operator. For a standalone
three-process benchmark (no register inspection or configuration changes):

```powershell
& <CUDA-Python-executable> tools/Run-MsiMatchedBenchmark.py --output build/benchmarks/new-run --label hybrid-U6
```

Each process runs `windows/ad104-msi/benchmark.py` with 4096³, 15 samples,
batch 5 and warmup 3 s. Keep stock and unlocked environments and settings
identical. Mobile power.draw telemetry can be invalid after a firmware
transaction; do not interpret it as measured physical power.

## Optional power traces

`Trace-MsiHybridPower.ps1` supports Probe, Live, ArmBoot, StopBoot and VerifyBoot
with an explicit output directory. Live accepts `-Python`. ArmBoot registers
one SYSTEM startup task to stop its WPR capture after 180 s; it checks the
expected USB EFI first and never reboots. Use these only when collecting
another trace is intended. `msi-hybrid-power.wprp` contains the verified event
providers, including registry queries.

`Export-MsiPowerTrace.ps1 -TracePath <ETL>` uses the compiled ProcessTrace/TDH
reader. `analyze_msi_power_trace.py <capture-directory>` correlates exact adapter
identities with completed power transitions. Unattributed events do not prove
NVIDIA stayed in D0; D3 events alone do not distinguish hot/cold. The decoder
replaces Get-WinEvent multi-ID filtering, which returned false empty results.
To include a completed boot trace in the completion helper, supply
`-TraceDirectory` instead of `-WithoutBootTrace`.
