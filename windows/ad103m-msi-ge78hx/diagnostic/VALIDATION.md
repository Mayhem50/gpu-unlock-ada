# Offline validation — 2026-10-27

Built using VS 2022 Community x64 MSVC, discovered by vswhere, and Python 3.13.
Only build tools, CPU test programs and static analysis were executed.
No PnP/CIM/NVML/CUDA collector, firmware program or GPU benchmark was run.

Results:

- C application mocks: 6,594 assertions passed. The EFI entry point runs
  against mock protocols and in-memory PCI/PROM/register data.
- C platform mocks: 192 assertions passed for loaded PE parsing and bounded
  protocol graph capture.
- Python: 19 tests passed, covering PE rejection, capture mutation rejection,
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
5dff6eee4c6ed68f33221785038877caa858b001284db8b0aba3ef1a5809e744

Regression coverage added after review:

- MMIO failure, invalid chip, identity drift, final chip-read failure and BADF
  readout skip PROM sampling. Failed register capture also skips the extended
  PCI-config read; the failed snapshot is still written for diagnosis.
- A full application run with a 4 MiB cached ROM and 1 MiB PROM generates a
  valid report above 10 MiB, then passes the Python capture validator.
- The report buffer is 64 MiB to cover the bounded path/graph/code/ROM maxima;
  file analyzers use the same 64 MiB capture ceiling. This reserves 64 MiB of
  image data at a future firmware load.
- GOP parents, console paths and console variables are compared semantically.
  Display/handle enumeration reordering preserves matches; changed routes
  fail comparison, while incomplete topology is reported as unknown.
- Empty complete graphs, absent/out-of-range/boolean target references and
  a graph target inconsistent with the PCI path are rejected. Console graph
  roots must exist and are included in comparison.

The source/build/header identity and artifact digest are recorded in the
local build/manifest.json. build/qualification.json records the distinction
between successful host tests and unperformed hardware qualification.
Build outputs and simulated captures are ignored by Git.

This image is unsigned, undeployed and unexecuted in firmware. The exact
machine's expected chip value, safe firmware read exposure, complete ROM/FWSEC
availability, ownership topology and Secure Boot launch compatibility remain
unverified. A diagnostic snapshot cannot qualify FLR recovery, establish iGPU
recovery after a failed transaction, or prove unlock support.
