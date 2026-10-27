# Offline validation — 2026-10-27

Built using VS 2022 Community x64 MSVC, discovered by vswhere, and Python 3.13.
Only build tools, CPU test programs and static analysis were executed.
No PnP/CIM/NVML/CUDA collector, firmware program or GPU benchmark was run.

Results:

- C application mocks: 5,551 assertions passed. The EFI entry point runs
  against mock protocols and in-memory PCI/PROM/register data.
- C platform mocks: 179 assertions passed for loaded PE parsing and bounded
  protocol graph capture.
- Python: 11 tests passed, covering PE rejection, capture mutation rejection,
  driver digest validation, semantic graph comparison and unresolved graphs.
- PowerShell: pure identity tests passed for the expected PCI/NVML strings,
  duplicate GPUs, wrong device/subsystem/revision, driver and VBIOS.
- Collector syntax parsed successfully; the collector itself was not run.
- Shared EDK II header contents matched their pinned hashes.
- git diff --check passed.

Mock rejection cases include 2757/2782 devices, wrong revision/subsystem,
unexpected AD104 chip, active BME, missing memory decode, inconsistent BAR
resource, failed config/location/MMIO/PROM reads, identity drift, duplicate
devices, missing/non-USB log source, short writes and flush failures.
Write-capable GPU protocol callbacks are absent in the mock setup, while
static source checks reject forbidden access call sites.

Local artifact: build/AD103M-PROBE.EFI

SHA-256:
4768b7be6f62f6acaf619fd7d5f68473d788a73262cb5b38522c493505c5f2f3

The source/build/header identity and artifact digest are recorded in the
local build/manifest.json. build/qualification.json records the distinction
between successful host tests and unperformed hardware qualification.
Build outputs and simulated captures are ignored by Git.

This image is unsigned, undeployed and unexecuted in firmware. The exact
machine's expected chip value, safe firmware read exposure, complete ROM/FWSEC
availability, ownership topology and Secure Boot launch compatibility remain
unverified. A diagnostic snapshot cannot qualify FLR recovery, establish iGPU
recovery after a failed transaction, or prove unlock support.
