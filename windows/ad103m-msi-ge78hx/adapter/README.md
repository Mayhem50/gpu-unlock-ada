# MSI adapter foundation: read-only PCI admission

This is the first adapter component, compiled only in a hosted test executable.
It uses the repository's pinned EDK2 `EFI_PCI_IO_PROTOCOL` types directly,
including the four-argument Attributes ABI. There is no EFI entry point, native
backend, protocol discovery/claim, BAR access, driver release, DMA, reset,
Windows transfer or unlock. The existing host port guards remain unchanged.
Nothing here is installed on the USB or executed against hardware.

`MsiObservePci` accepts a caller-supplied protocol and a session-lifetime,
zero-initialized journal. The only service calls are GetLocation,
Attributes(Supported/Get, zero input mask), and Pci.Read (64 DWORDs at offset 0).
It makes two bounded observations and requires exact success statuses, complete
outputs, stable function slots, location 0:1:0:0, 10de:2717 / 1462:13cd / A1,
display class and a type-0 header. It preserves attempted/completed masks, raw
statuses and samples on refusal. Reentry/retry is refused without another call.

PCI command bits 1/2/4 and protocol attribute bits 0x100/0x200/0x400 are
different namespaces. Both samples require matching IO/MEM decode enabled and
bus mastering disabled; support must include all three protocol bits, and active
attributes may not exceed reported support. This deliberately rejects legitimate
but unreviewed configurations. Every config DWORD must be written and differ
from the all-ones poison value; the two complete snapshots must match exactly.
Some changing status bits or valid all-ones words can therefore cause conservative
refusal. Do not relax them without a separately reviewed case and regression.

The returned `observed` flag describes these samples only. It is NOT a permit
to write, claim, reset, allocate DMA, or unlock. Two identical snapshots do not
prove isolation, quiescence or absence of intervening changes. The caller's
pointer/handle provenance, stable journal lifetime and legal TPL remain trusted
contracts, not properties established by these mocks. No exclusion primitive or
solution to the T7 transfer gap is supplied. This component is not yet wired to
`MsiOps` or to Stage A; all native gates remain open.

## CPU validation

From the repository root:

```powershell
pwsh -NoProfile -File windows/ad103m-msi-ge78hx/adapter/Build-Offline.ps1
```

148 cases / 3,211 assertions pass against fake EFIAPI callbacks. Coverage includes
every callback error/warning, missing output, reentry, function-table change,
all eight command/attribute combinations, MSI identity/header mismatches, wrong
attribute namespace, unsupported active attributes and each of 64 config words
drifting in the second snapshot. No failure retries a service. Native/mixed-unlock
compile guards are negative-tested. The compiler uses /W4 /WX and the actual
EDK2 protocol declarations; no hand-declared ABI or function-pointer cast is used.

Ignored `build/manifest.json` records passing tests separately from firmware
execution (false), source and all EDK2 header hashes, compiler hash/version and
UTC build time. Only CPU test programs are linked. Tests do not query the GPU,
inspect running workloads, modify security state or update the USB.

## Next independent gates

Resolve relevant GOP/console owners and their callbacks, review native protocol
claim/attribute journals, and settle a legal per-source exclusion/transfer design
before any release/reset implementation is made bootable. Stock postconditions,
native DMA/timing/terminal behavior and the repository's firmware transaction
still need their own implementations and qualification. Stage A success alone
does not close them. See the main launch checklist.
