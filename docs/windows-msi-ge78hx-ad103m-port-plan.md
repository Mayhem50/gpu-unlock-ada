# Windows port discovery: MSI Raider GE78HX 13VI / AD103M

**Current status: read-only capture analysis and a host-only prototype; no native
unlock adapter or EFI unlock payload is built, deployed, or run.** The user has
executed diagnostic captures 3/4; that diagnostic history is separate from
qualification of release, reset or unlock. Current work is repository/CPU-only.
See the [current gate checklist](windows-msi-ge78hx-launch-checklist.md) and
[repository-method alignment review](windows-msi-ge78hx-method-alignment.md).

This document is a fail-closed porting plan for the exact local machine. It neither
authorizes nor contains a VBIOS flash, boot-protection change, BCD/ESP write,
driver patch, GPU-MMIO write, or experimental payload.

## Captured Windows baseline (2026-10-27)

| Property | Observed value |
|---|---|
| dGPU | NVIDIA GeForce RTX 4090 Laptop GPU |
| PCI identity | `10DE:2717`, subsystem `1462:13CD`, revision `A1`, BDF `01:00.0` |
| GPU VBIOS | `95.03.2A.00.2A` |
| NVIDIA driver | `595.79` (CUDA reported as 13.2) |
| System firmware | MSI `E17S1IMS.114`, released 2025-10-16 |
| Firmware mode | UEFI |
| Graphics topology | Intel UHD and NVIDIA display adapters are both present (hybrid ownership must be captured, not assumed) |
| Security at initial collection | VBS/security service reported active; that non-elevated query could not read Secure Boot or BCD. Later user-reported Secure Boot=false and BitLocker protection suspended; current state has not been re-queried |
| Power observation | `nvidia-smi` reported a 155 W current limit, 150 W default limit and active `SW Power Cap`; no hardware or software thermal slowdown was active during sampling |

The recorded values are observations, not an admission profile. In particular,
`SW Power Cap` is not evidence that a tensor-rate port should alter MSI EC,
power, or VBIOS settings.

Run `tools/windows-msi-ge78hx-readonly.ps1` to repeat the host-only
collection. It prints to stdout and intentionally has no persistence or action
switches for persistence. NVML collection now requires both -IncludeGpu and
-ConfirmGpuIdle; without them the driver/VBIOS check is explicitly incomplete.
Identity mismatches, failed commands and unavailable security information
return a failure exit code.

## Why the existing Windows port cannot be retargeted

`windows/ad104-msi` is a manual UEFI reference for one **desktop** MSI RTX
4070 Ti: PCI `10DE:2782`, subsystem `1462:5132`, AD104, VBIOS
`95.04.3C.40.FD`, and MSI Z490 firmware. Its code pins the PCI device, a 16 MiB
BAR0 shape, PROM/FWSEC hashes and offsets, loaded GOP/console driver code,
controller graph, and cold-entry register values. It also writes to its USB
log directory and, after an explicit `U`, performs the transaction. Those pins
must not be copied to this laptop.

AD103 and AD103M share an Ada-family transaction lineage, not a board profile:

| Aspect | AD103 desktop evidence | AD103M laptop consequence |
|---|---|---|
| PCI admission | Exact desktop IDs have separate opt-ins | `2717` is absent from this repository's allow-list; do not map it to `2757` |
| Firmware assets | Pinned AD102-family boot assets are shared in Linux experiments | Shared assets do not establish compatible PROM/FWSEC geometry, retention, or payload offsets |
| Display/ownership | Desktop port captures a single NVIDIA GOP stack | Hybrid Intel/NVIDIA ownership may expose no NVIDIA GOP or a materially different protocol graph |
| Power/reset | Desktop FLR and decode restoration are captured once | Laptop EC, Dynamic Boost, MUX and dGPU power-gating make FLR and warm-boot retention board-specific |
| Windows execution | AD104 EFI is unsigned; its guide uses Secure Boot disabled | User later changed policy for the diagnostic. A fresh trust/security/recovery review is needed for any future binary; no new protection change or launch is in scope |

## Issue #76 and the `2757` evidence

Issue #76 is encouraging only for the already admitted `10DE:2757` Alienware
AD103M profile. It reports full RM rate readback and matched 2.00x issue-rate
results on cold and warm runs, with checked FP16/BF16/TF32/FP8 workloads. It
does **not** cover `2717`, MSI firmware, VBIOS `95.03.2A.00.2A`, Windows, or an
MSI hybrid-GOP path. Its measured GEMM gains were lower than 2x when power
limited, and it explicitly leaves battery operation, dGPU-only display mode,
longer stability, end-to-end workloads, and a second post-unlock warm reboot
unqualified. The pre-existing `2757` report is likewise inconclusive: its
getter changed but its throughput did not.

Therefore issue #76 is a hypothesis to test under a new profile, never a
justification for aliasing `2717` to `2757` or relaxing safety checks.

## Proposed architecture

```text
host-only collector (read-only)
        |
        +-- hardware manifest + cold-baseline comparison
        |
        +-- UEFI read-only capture, reviewed and accepted by existing boot policy
                |  PCI config / BAR geometry / ROM descriptor and FWSEC
                |  GOP, Console, handle/protocol ownership graph
                |  cold and warm retention snapshots (no MMIO writes)
                v
        strict `2717/1462:13CD/A1/VBIOS-hash/system-firmware-profile` admission gate
                |
                +-- reject on any unknown or inconsistent field
                v
        host-only orchestration (CPU mocks implemented; native transaction absent)
                |  explicit one-shot consent, external observer, fail-close
                |  release only captured consumers; bounded DMA and FLR
                v
        unmodified NVIDIA Windows driver -> matched correctness/performance suite
```

The new code should live beside, not inside, `windows/ad104-msi`, for example
`windows/ad103m-msi-ge78hx/`. It should share only audited generic utilities
(formatting, SHA-256, PE validation and selected transaction primitives), while
having an independent `profile`, `ownership`, `boot I/O`, fixture set and
documentation. Build targets must distinguish `probe` (read-only capability)
from `transaction` (write-capable), with the latter absent until the probe data
has been reviewed. The dedicated diagnostic probe is now implemented in
windows/ad103m-msi-ge78hx/diagnostic, compiled and exercised under CPU mocks.
Diagnostic deployments/captures were subsequently performed by the user. No
release/reset/unlock hardware qualification has occurred. The host prototype
under windows/ad103m-msi-ge78hx/port models orchestration only; its transaction
callback does not implement GFW/FWSEC/signature/selector phases.

The reference's complete gpu_unlock_610_unlock owns reset and unwind. A future
adapter must not connect that function to our callback and perform an outer FLR
or cleanup again. The guarded compound commit is also a new mock-only abstraction,
not a demonstrated native UEFI sequence. These integration decisions, missing
FBPA accessibility sampling and pre-FLR effective-readout proof remain explicit
open gates in the checklist.

The repository's tools/read_sm_rates.py uses Linux RM ioctls and is not a
Windows validator. A Windows rate-query implementation remains future work;
Windows validation must use matched instruction/GEMM correctness and timing
unless a separately verified rate query is available.

## Preconditions before any write-capable design review

1. An elevated **read-only** collection must resolve the actual Secure Boot
   state and export the BCD/boot-manager *listing* without changing either.
2. Obtain at least two cold UEFI captures, compare normalized identity,
   BAR descriptors, ROM/code digests and resolvable ownership relationships, and
   parse the local VBIOS/PROM and FWSEC metadata offline. Raw ROM, loaded
   Windows images, partition GUIDs and proprietary capture data stay private.
3. Establish the Intel/NVIDIA GOP and protocol-owner graph for every capture;
   compare semantic paths rather than enumeration IDs. Unknown or changed
   ownership requires review; raw capture hashes need not match across boots.
4. Establish dGPU power state, BME, BAR geometry, WPR2/selector/readout and
   retention across ordinary cold and warm stock boots. A read-only probe cannot
   establish FLR recovery; that remains a later, separately approved test.
   No values from AD104, `2757`,
   Clevo, Lenovo or Alienware are valid substitutes.
5. Preserve a proven stock boot path without modifying default BootOrder, BCD
   or the internal ESP. Re-establish the actual boot-security/recovery state
   for the exact future image; do not treat prior user changes for the diagnostic
   as approval for another policy change or payload. All such actions remain
   outside the current CPU-only scope.
6. Prepare a reproducible stock benchmark baseline, exact binary/source
   hashes, numerical comparators, telemetry and an external recovery observer.

## Validation ladder

1. Host-only read-only script: verify the identity and collect no files.
2. Offline build/tests: host mocks, static PE checks and negative admission
   tests for `2757`, every other device, subsystem, VBIOS and unknown ownership
   graph. Passing mocks are not hardware validation.
3. Trusted UEFI read-only probe: capture only after its source and deployment
   path are reviewed. Confirm that it makes no GPU writes, BCD writes, ESP
   changes or BootOrder changes.
4. Profile review: compare normalized cold captures and investigate changes; add the
   exact profile only after all expected fields are known.
5. A separately approved, one-shot transaction can be considered only if a
   recovery path and boot-security policy remain intact. This step is expressly
   not prepared or executable by this branch.
6. Post-transaction proof would require an unmodified NVIDIA driver handoff,
   an independently verified Windows rate query if available, numerical checks, matched stock/unlocked
   instruction and GEMM tests, telemetry, cold A/B/A and warm-reboot runs.
   Report no application-wide performance claim from microbenchmarks alone.

## Risks and stop conditions

- A wrong PROM/FWSEC/ownership/FLR assumption can hang the dGPU or host, lose
  unsaved work, or leave firmware DMA state uncertain. Power off rather than
  retrying a suspected partial transaction.
- Laptop EC/MUX/Dynamic-Boost behavior can change performance independently of
  tensor rate; the observed `SW Power Cap` makes power telemetry mandatory.
- A successful selector or rate getter alone is insufficient. The historic
  `2757` evidence includes a no-throughput-change result.
- Secure Boot, VBS/HVCI and a hybrid display stack impose compatibility and
  recovery constraints. They are not to be bypassed to make a test possible.
- Any mismatch, unknown capture field, unexpected PCI ownership edge, already
  bound driver, non-fresh state, missing external observer, or absent trusted
  launch path is a hard stop.

## Sources

- Repository Windows reference: `windows/ad104-msi/README.md` and
  `docs/ad104-windows-latence-app.md`.
- Existing AD103M result: `docs/ada-2757-report.md`.
- [Issue #76: Alienware 2757 AD103M matched result](https://github.com/JustAResearcher/gpu-unlock-ada/issues/76).
