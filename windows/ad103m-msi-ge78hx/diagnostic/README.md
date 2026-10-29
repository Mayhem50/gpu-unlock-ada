# MSI GE78HX capture diagnostic

This diagnostic inventories PCI, GOP/console ownership, driver code identity,
BARs and cold registers for the exact MSI 2717/13cd. Its captures provided
inputs to the [native Hybrid build](../native/README.md). The native generator
also reuses its bounded PCI/GOP inventory and host-test transport.

```powershell
pwsh -File windows/ad103m-msi-ge78hx/diagnostic/Build.ps1
pwsh -File windows/ad103m-msi-ge78hx/diagnostic/Test-Host.ps1
```

Build requires MSVC and shared `../../ad104-msi/vendor/edk2` headers.
The host-only runner requires an existing generated `build/build_id.h`.
Python analysis and regression tools are under `tools/` and `tests/`.
Private captures and generated outputs stay ignored. This diagnostic does
not execute an unlock payload or establish performance/persistence results.

For Hybrid D/T collection use `native/Build-Hybrid.ps1`; for the validated
unlock use `native/Build-HybridUnlock.ps1`. Historical host-only validation
is in `VALIDATION.md`. Current hardware results and limits are in
[validation](../../../docs/windows-msi-ge78hx-hybrid-runtime-power-validation.md).

See `LICENSE.txt` and `THIRD_PARTY.md` for retained source notices.
