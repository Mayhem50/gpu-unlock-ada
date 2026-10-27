# MSI GE78HX read-only UEFI probe

Prepared offline for PCI 10DE:2717 / 1462:13CD / revision A1. This is a
diagnostic build with no unlock code or NVIDIA firmware linked. It has not
been executed in UEFI or qualified on hardware.

From the repository root:

```powershell
pwsh -NoProfile -File windows/ad103m-msi-ge78hx/diagnostic/Build.ps1
```

This compiles an x64 EFI and executes only CPU host mocks, PE checks and
Python/PowerShell tests. MSVC is located through vswhere, or can be specified
with -VcVars. Python 3 and the Windows x64 MSVC toolchain are required.
Output: diagnostic/build/AD103M-PROBE.EFI and manifest.json.

At a future explicitly approved UEFI run, the probe would inventory display
PCI devices, GOP paths, console variables and the active protocol graph.
MMIO reads require the exact PCI identity, BDF 0000:01:00.0, memory decode
already enabled, BME already disabled, and a consistent BAR descriptor large
enough for the fixed read allowlist. It never changes those states. The first
chip read must equal 0x193000a1 (an expected AD103-family value, not yet observed
on this machine), or further sampling stops. A mismatch is diagnostic failure,
not permission to change this expectation without review.

The PROM window is bounded to 1 MiB at BAR0+0x300000. A complete window is not
necessarily a complete laptop VBIOS. Cached UEFI ROM is separately labelled and
bounded to 4 MiB; unavailable or truncated firmware is reported, never worked
around by enabling ROM decode. FWSEC eligibility remains an offline review.

Only new CAPxxxx.JSON log files in AD103M-PROBE on the application's own
USB-backed filesystem are written. The probe rejects a non-USB source.
No MMIO/PCI writes, DMA mappings, firmware loading, FLR, driver release,
boot-variable changes or Windows chainloading are implemented.

Offline capture review:

```powershell
python windows/ad103m-msi-ge78hx/diagnostic/tools/analyze.py CAP0001.JSON
python windows/ad103m-msi-ge78hx/diagnostic/tools/compare.py CAP0001.JSON CAP0002.JSON
```

Use --manifest to select the manifest for those exact captures. Build IDs,
data lengths, target identity and supplied firmware/code digests are checked.
Graph IDs are normalized by unique device paths. Unresolvable graph nodes and
relocated driver code produce an unknown result, never a passing comparison.
GOP parents, active console associations, console paths and ConIn/ConOut
variables are compared separately as topology_matches. Array/handle ordering
is normalized. Empty complete graphs or inconsistent target references are
rejected.

Version 0.4.1 distinguishes a successful SEC2_CPUCTL read returning a BADF
sentinel from a transport error. Only that specific register/value class can
produce classification=sec2_cpuctl_unavailable with prom_read_allowed=true.
The register snapshot stays complete=false; its raw value is preserved.
All 22 samples must have been attempted, the PCI header must be unchanged,
and a final chip read must still match AD103 before continuation is allowed.
SampleProm then independently repeats its PCI/BAR/BME and chip checks.
Transport errors, BADF at other registers, identity drift and failed final
chip reads still block PROM sampling. No inaccessible register is treated as
valid, and this classification never authorizes an unlock.

Version 0.4.2 records config_slot/config_offset separately from uefi_index.
For the MSI layout, config slots 0,1,3,5 map to logical protocol indices
0,1,2,3. High DWORDs of 64-bit BARs are never queried as separate resources.
Malformed/reserved encodings are reported without a GetBarAttributes call.
The analyzer checks the mapping against captured config bytes. BAR0 remains
logical index 0; MMIO/PROM access and their guards are unchanged.
The report reserves 64 MiB to fit the bounded ROM, paths, code and graph data;
a maximum-size ROM capture is exercised through the complete mocked entry
point and validated offline.
Volatile selector/register samples remain available for separate review.
Neither analyzer generates an admission profile or authorizes a transaction.

Keep raw captures private: they contain proprietary ROM, loaded code and
machine-specific details. No deployment or boot-registration script is
provided. The compiled image is unsigned. The operator disabled Secure Boot
for the earlier diagnostic captures; this does not establish trust under an
enabled Secure Boot policy or authorize a new launch automatically.
