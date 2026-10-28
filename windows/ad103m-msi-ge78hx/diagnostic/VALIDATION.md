# Offline validation — 2026-10-27

Built using VS 2022 Community x64 MSVC, discovered by vswhere, and Python 3.13.
Only build tools, CPU test programs and static analysis were executed.
No PnP/CIM/NVML/CUDA collector, firmware program or GPU benchmark was run.

Results:

- C application mocks: 7,925 assertions passed. The EFI entry point runs
  against mock protocols and in-memory PCI/PROM/register data.
- C platform mocks: 192 assertions passed for loaded PE parsing and bounded
  protocol graph capture.
- Python: 39 tests passed, covering PE rejection, capture mutation rejection,
  driver digest validation, semantic graph comparison and unresolved graphs.
  The comparator-only update adds pathless driver/console anchors, distinct
  GPU/boundary/global diagnostics, handle-renumbering and ownership-mutation
  tests, independent-manifest CLI checks and the private CAP0003/CAP0004
  integration. All 39 were rerun without rebuilding or changing the EFI.
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
18efed6c4a60ce0ae682fb9c9d165f425c280a2159e32f57124f8cf3c41703f9

Regression coverage added after review:

- Version 0.4.2 maps config DWORD slots to logical UEFI BAR indices.
  CPU mocks cover all-32-bit, the captured MSI mixed layout, all-64-bit,
  unimplemented BAR slots, a truncated trailing 64-bit BAR and reserved
  memory encodings. Resource queries are checked against independent expected
  logical indices and addresses. Python rejects missing/extra entries,
  incorrect protocol indices, wrong high-DWORD assembly and inconsistent
  metadata. No new GPU/PCI write operation was introduced.
- MMIO transport failure, invalid chip, identity drift, final chip-read failure
  and BADF at required registers skip PROM sampling. The failed snapshot is
  still written for diagnosis.
- Version 0.4.1 adds a distinct partial classification for successful
  SEC2_CPUCTL reads returning BADF (including the captured 0xbadf5620).
  With stable PCI and final AD103 identity, it permits only the independently
  guarded PROM capture. The register snapshot remains incomplete. A full
  mocked application run produces a validated PROM with this exact sentinel.
- Regression tests combine optional SEC2 BADF with final-chip failure,
  identity drift, active BME, missing memory decode, invalid BAR and wrong
  chip. All block PROM access. An EFI read error at SEC2 also remains blocking.
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

Version 0.4.3 adds boundary owner and passive callback/opened-log observations.
It remains unsigned and is prepared locally without firmware execution or USB
deployment. Offline suites pass 7,925 application / 605 platform checks and
62 Python tests, plus identity/PE/source checks. Packet tests reject altered
inputs/binary/toolchain/build identity, missing generated provenance and PE
failure, and refuse overwriting an existing packet. The version-specific
analyzer regression preserves the strict 0.4.2 BAR and SEC2 continuation rules.
The compiled banner, JSON version, manifest and packet use 0.4.3 consistently.

Earlier operator captures 3/4 established AD103 identity, a complete parsable
ROM chain/FWSEC and corrected BAR mapping. The new observations still need an
independently approved future capture. The previous local image/manifest is
archived before rebuild; the USB Stage A is not replaced. Read-only snapshots
cannot qualify FLR recovery, establish iGPU recovery after a failed transaction,
or prove unlock support. Keep historical captures and their own manifests.
