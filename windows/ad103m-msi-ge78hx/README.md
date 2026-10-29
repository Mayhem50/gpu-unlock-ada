# MSI Raider GE78HX 13VI — Windows Hybrid unlock

Machine-specific reference for RTX 4090 Laptop `10de:2717`, subsystem
`1462:13cd`, revision A1, VBIOS `95.03.2A.00.2A`, BIOS `E17S1IMS.114`.

The validated path is [native Hybrid U6](native/README.md) plus the
NVIDIA-only Windows runtime-power workaround. On 2026-10-29, RUN0008 retained
the full unlock through Windows driver initialization, preserved the Intel
A788 internal display route and physical brightness, and passed three matched
CUDA GEMM runs: FP16 +84.65%, BF16 +83.27%, FP32 approximately unchanged.

See the [results and limits](../../docs/windows-msi-ge78hx-hybrid-runtime-power-validation.md)
and [public report #117](https://github.com/JustAResearcher/gpu-unlock-ada/issues/117).
Sleep/resume, hibernation and long-term stability remain unqualified. A new
boot without U is not expected to retain the transaction's volatile state.

- `native/`: Hybrid U6 build, exact admission, Intel-preserving handoff,
  D/T data collection, packaging and verified USB staging.
- `diagnostic/`: capture inventory, host tests and shared source used by the
  native generator. Host checks do not establish hardware unlock success.
- [Windows tools](../../tools/README-msi-hybrid.md): build the readers, inspect,
  apply or restore the targeted policy, read registers and benchmark.

Firmware assets, physical PROM/captures, ESP observations, generated sources,
binaries, private backups and ETLs remain ignored. No build or test command
automatically boots the EFI, resets a GPU, changes the MUX or modifies the ESP.
The offline adapter/port prototypes and provisional stock-handoff route
are superseded by the native path and have been removed from the source tree.
