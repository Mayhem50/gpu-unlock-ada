# MSI GE78HX AD103M Windows port: launch checklist

This is an execution packet, not approval to run every stage. Each stage must
pass before the next one exists. A profile or payload launcher is deliberately
absent: building one before private read-only capture data would be unsafe.

## Stage 0 — preserve the working system

- Finish active GPU jobs and save their outputs.
- Keep AC connected; do not change GPU mode, MSI Center profile, driver,
  Windows build, BIOS, VBIOS, power limit, Secure Boot, BitLocker, BCD, ESP,
  BootOrder, or Fast Startup during a matched baseline.
- Confirm a normal cold boot, normal Windows login and normal NVIDIA workload
  before collecting any porting evidence.
- Keep a separate, known-good backup of important work. This is an operational
  prerequisite, not a prompt to flash or modify firmware.

## Stage 1 — host baseline (prepared, read-only)

After the GPU workload is idle, from the repository root:

```powershell
pwsh -NoProfile -File .\tools\windows-msi-ge78hx-readonly.ps1
```

Expected identity: `10DE:2717`, `1462:13CD`, `A1`, VBIOS `95.03.2A.00.2A` and
NVIDIA driver `595.79`. A mismatch stops the port. The script only displays
data. Save its terminal transcript manually outside the repository if desired;
review it before any subsequent stage.

## Stage 2 — evidence review gate (not an execution step)

Review the Stage-1 identity, boot-security visibility and hybrid graphics
topology. The following are hard stops:

- system or GPU identity mismatch;
- unclear Secure Boot policy or unavailable stock Windows Boot Manager path;
- dGPU-only display mode with no independently proven recovery display path;
- current GPU workload, unsaved work, or no external observer/recovery plan.

Only an explicit approval after this review can authorize work on a dedicated
UEFI **read-only** probe. The existing AD104 probe is not launchable here: it
is pinned to device `2782`, AD104 chip registers, a desktop GOP graph and a
different ROM/FWSEC profile.

## Stage 3 — dedicated UEFI read-only probe (not yet built)

The future probe must be a new program under
`windows/ad103m-msi-ge78hx/`, and must prove through source review and tests
that it:

- reads PCI configuration, BAR description, bounded PROM/FWSEC descriptors,
  selected safe MMIO registers, GOP/console ownership and device paths only;
- never enables BME or memory decode, writes MMIO, resets/FLRs the GPU,
  releases a UEFI driver, changes NVRAM/BCD/BootOrder/ESP, or loads a payload;
- emits its capture only to a separately prepared removable medium; and
- has an existing-policy trust/signing route. Do not disable Secure Boot as a
  substitute for such a route.

Two cold captures with matching hardware-critical fields are required. The raw
captures remain private; only a reviewed, redacted profile digest may enter
source control.

## Stage 4 — exact admission profile (blocked on Stage 3)

Implement an allow-list for all of: PCI device, subsystem, revision, VBIOS
digest/version, system firmware profile, chip/readout, BAR layout, FWSEC
metadata and ownership graph. Add negative tests for `2757`, all AD104 profiles
and every mismatch. Any unknown field rejects before the GPU is claimed.

## Stage 5 — transaction design review (blocked on Stage 4)

Only then review an isolated write-capable implementation. It must retain the
stock boot path, require a new explicit approval, have no automatic retry and
fail closed before driver handoff. No such implementation is present in this
branch, and this checklist does not authorize creating or launching one.

## Stage 6 — post-approval validation (blocked on Stage 5)

An approved one-shot trial would require: an external observer; clean cold
entry; unmodified NVIDIA driver handoff; post-FLR checks; deterministic
numerical tests; matched stock/unlocked tensor/GEMM tests; power/clock/thermal
telemetry; cold A/B/A; and a separate warm-reboot qualification. A selector
change, a rate getter, or a microbenchmark alone is not sufficient evidence.
