# MSI 2717 native Hybrid U6

The active unlock build preserves the Intel display, verifies the exact MSI
identity/PROM/firmware, claims only NVIDIA, runs FWSEC/SEC2 and one FLR, checks
full selectors/readout, frees DMA and hands off with NVIDIA memory decoding
enabled and bus mastering disabled. It does not detach or reconnect Intel.

Stock SS0 is read and latched in full, without a historical-value whitelist;
invalid reads and the unlocked marker are refused at entry. Full selector
drift checks remain active until the intentional writes.

## Build and package

```powershell
pwsh -NoProfile -File windows/ad103m-msi-ge78hx/native/Build-HybridUnlock.ps1
python windows/ad103m-msi-ge78hx/native/package_hybrid_unlock.py
```

Requires Python, GCC/MinGW-w64 and MSVC x64 tools. `-Gcc` overrides the default
Scoop path. The generator derives an isolated source tree from `ad104-msi` and
the retained diagnostic. The shared reference remains untouched. `prepare.py`
is the base generator used by `prepare_hybrid_unlock.py`, not a separate
supported dedicated-GPU release.

Private inputs must already be present and match the generator's hashes:

- `../captures/2026-10-28-run6/CAP0006.JSON`;
- `build/hybrid/hardware/CAP0002/CAP0002.JSON` and CAP0003 equivalent;
- `build/hybrid-unlock/hardware/RUN0004/RUN0004.TXT` (entry evidence);
- `build/windows-profile.json` and its verified local Windows image copy;
- `build/assets/`: the five pinned 610.43.03 assets and NVIDIA-LICENSE.

`Read-EspImage.ps1` makes a read-only observation of the actual ESP image;
`Select-WindowsProfile.ps1` checks its signature/hash/geometry before selecting
the local profile. No firmware or Windows image is distributed in source.
A clean checkout without private inputs deliberately refuses to build.

The full build runs 30 backend scenarios, 12 application flows, profile and
Windows-image checks, and 240 native-core scenarios using simulated EFI/MMIO
transports. `-ReuseCoreValidation` checks unchanged inputs and the prior log
hash. Host tests do not replace hardware tests. EFI/provenance are under ignored
`build/hybrid-unlock/`; the original tested U6 SHA256 is
`83f8c6656c87c2575f23f46a0d67641d19f9cea909aed35fa7bbf58d3b8d6dd7`.

Packaging verifies all five asset hashes and ZIP members. USB staging requires
the exact Kingston identity, a complete verified package and an explicitly
provided hash of the EFI currently on the key:

```powershell
pwsh -File windows/ad103m-msi-ge78hx/native/Stage-HybridUnlockUsb.ps1 -ExpectedPreviousEfiSha256 <verified-current-USB-EFI-SHA256>
```

This backs up the old EFI/logs and verifies readback; it never formats the key
or boots it. Read `TRIAL-HYBRID-U.txt` before a manual trial. The Windows-only
policy and checks are documented in [the Windows tools guide](../../../tools/README-msi-hybrid.md).

## Diagnostics and evidence

`Build-Hybrid.ps1` builds the separate D/T capture EFI: D observes, T temporarily
enables NVIDIA memory decode to capture cold data and restores it. It remains
useful for fresh topology evidence and never runs the unlock payload. Keep its
captures separate from `MSI2717-HYBRID-U` transaction logs.

RUN0008 and the matched Windows measurements establish success for the tested
machine/driver/boot. See [validation](../../../docs/windows-msi-ge78hx-hybrid-runtime-power-validation.md).
Generated manifests describe build validation separately from archived hardware
evidence. Do not infer future-boot or suspend qualification.

GPL-2.0-only except imported files with their own licences. Keep EDK2/NVIDIA
notices from `../../ad104-msi/THIRD_PARTY_NOTICES.md` and
`../../ad104-msi/UPSTREAM-WINDOWS-NOTICES.md` with redistributed packages.
