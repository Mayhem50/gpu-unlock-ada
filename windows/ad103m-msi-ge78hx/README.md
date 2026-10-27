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

There is no EFI payload, VBIOS image, installer, boot registration command, or
write-capable binary in this directory. `build/`, `local/`, and `captures/` are
ignored because captures can include proprietary firmware and machine-specific
data.

The only prepared launch is the host-only baseline collector:

```powershell
pwsh -NoProfile -File .\tools\windows-msi-ge78hx-readonly.ps1
```

Run it only once the active GPU workload is finished. It prints data only; it
does not write a file, call EFI code, alter BCD/ESP/BootOrder, modify Secure
Boot, install a driver, flash a VBIOS, or perform GPU MMIO writes.

See `../../docs/windows-msi-ge78hx-ad103m-port-plan.md` for the design and
`../../docs/windows-msi-ge78hx-launch-checklist.md` for the ordered gates.
