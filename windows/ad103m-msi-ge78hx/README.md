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
There is no unlock payload, VBIOS image, installer or boot registration command.
The probe writes logs only to its own USB source at a future approved launch.
The [offline port prototype](port/README.md) models MSI release, reset and
Windows handoff against CPU mocks derived from captures 3 and 4. It is not
bootable and does not contain an executable unlock backend.
`build/`, `local/`, and `captures/` are
ignored because captures can include proprietary firmware and machine-specific
data.

The only prepared launch is the host-only baseline collector:

```powershell
pwsh -NoProfile -File .\tools\windows-msi-ge78hx-readonly.ps1 -IncludeGpu -ConfirmGpuIdle
```

Run it only once the active GPU workload is finished. It prints data only; it
does not write a file, call EFI code, alter BCD/ESP/BootOrder, modify Secure
Boot, install a driver, flash a VBIOS, or perform GPU MMIO writes.

See [the design](../../docs/windows-msi-ge78hx-ad103m-port-plan.md),
[the launch checklist](../../docs/windows-msi-ge78hx-launch-checklist.md) and
[the diagnostic build instructions](diagnostic/README.md).
