# MSI Raider GE78HX 13VI / AD103M Windows port workspace

This directory is intentionally a **non-transactional** workspace for the
exact laptop profile:

```text
PCI:       10de:2717
Subsystem: 1462:13cd
Revision:  a1
VBIOS:     95.03.2A.00.2A
System BIOS: E17S1IMS.114
```

The read-only EFI diagnostic is implemented and compiled under diagnostic/.
There is no unlock payload, VBIOS image or boot registration command.
The probe writes logs only to its own USB source. Captures 3/4 were executed
manually by the user; no new probe deployment or launch is part of current work.
The [offline port prototype](port/README.md) models MSI release, reset and
Windows handoff against CPU mocks derived from captures 3 and 4. It is not
bootable and does not contain an executable unlock backend.
The separate [Stage A stock handoff](stock-handoff/README.md) is now built for
a first normal Windows boot test: it has no GPU protocols, DMA, release/reset
or unlock. Its USB staging helper is gated on the actual ESP fingerprint and
verified removable-media identity. On 2026-10-28 the operator reported a
successful manual Stage A test; there is no captured execution/driver trace.
the current profile is tied to the operator-reported ESP fingerprint and an
identical authenticated component-store image. Current security/recovery checks
and future physical trials are separate from this source/build evidence.
The [adapter foundation](adapter/README.md) now host-tests read-only PCI
admission using actual UEFI protocol types. It has no EFI entry point, native
execution or write/reset/unlock path, and is not connected to the USB build.
`build/`, `local/`, and `captures/` are
ignored because captures can include proprietary firmware and machine-specific
data.

GPU-port validation remains CPU-only; Stage A has its own host/PE checks.
Use each component's README for its build/test command.
The host baseline collector below is a reference for a later authorized idle
GPU session, and is not run as part of an offline review:

```powershell
pwsh -NoProfile -File .\tools\windows-msi-ge78hx-readonly.ps1 -IncludeGpu -ConfirmGpuIdle
```

Run it only once the active GPU workload is finished. It prints data only; it
does not write a file, call EFI code, alter BCD/ESP/BootOrder, modify Secure
Boot, install a driver, flash a VBIOS, or perform GPU MMIO writes.

See [the design](../../docs/windows-msi-ge78hx-ad103m-port-plan.md),
[the launch checklist](../../docs/windows-msi-ge78hx-launch-checklist.md) and
[the diagnostic build instructions](diagnostic/README.md).
The [method alignment review](../../docs/windows-msi-ge78hx-method-alignment.md)
maps the prototype to repository requirements and records open native gates.
