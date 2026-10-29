# MSI 2717 Hybrid: Windows runtime-power workaround

Hardware validation on 2026-10-29: Hybrid U6 RUN0008 retained the unlock
through Windows startup with NVIDIA-only `EnableRuntimePowerManagement=0`.
The internal AUO84A6 panel remained on Intel A788, and the operator confirmed
that changing the brightness slider changed the physical brightness.

Public report: [upstream issue #117](https://github.com/JustAResearcher/gpu-unlock-ada/issues/117).
The compact [evidence summary](evidence/msi-ge78hx-hybrid/results.json) contains
the register observations, three-run medians and source evidence hashes;
full private captures and generated outputs remain local.

## Scope and evidence

- Target: NVIDIA 10de:2717, subsystem 1462:13cd; Windows driver 595.79
  (`32.0.15.9579`). The policy tool pins the installed driver hash.
- `tools/Set-MsiHybridRuntimePower.ps1` dynamically resolves the NVIDIA class
  key, backs up the original value, and changes only that adapter's value.
  On this machine the NVIDIA key is `0001`; Intel is `0002` and is untouched.
- The original NVIDIA value was absent. Restore removes the added value.
- EFI remained HYBRID U6, SHA256
  `83f8c6656c87c2575f23f46a0d67641d19f9cea909aed35fa7bbf58d3b8d6dd7`.
- Archived firmware evidence:
  `windows/ad103m-msi-ge78hx/native/build/hybrid-unlock/hardware/RUN0008/`.
- Windows evidence:
  `build/hybrid-runtime-power-off-20261029T173955Z/hardware-validation-RUN0008.json`
  and `postboot-read-20261029T180209Z/`.
- Actual Windows read: FEAT_PLM `ffffffff`, SS0 `88888888`, SS1 `00000008`,
  READOUT_1 `00000000`. The reader performed zero register writes, unmapped
  BAR0, and stopped/deleted its temporary driver service without cleanup errors.
- Three matched benchmark processes, identical runtime/library/benchmark
  hashes and numerical outputs:
  `build/benchmarks/hybrid-u6-rtd3off-matched-20261029T180234Z/comparison.json`.

| GEMM | Stock median TFLOP/s | Hybrid workaround median | Change |
| --- | ---: | ---: | ---: |
| FP32 pedantic | 27.584 | 27.379 | -0.74% |
| FP16 / FP32 accumulation | 85.239 | 157.396 | +84.65% |
| BF16 / FP32 accumulation | 85.179 | 156.103 | +83.27% |

The stock MUX and MSI power profile were not independently recorded. These
are short GEMM measurements, not general application speedup guarantees.

## Interpretation

The preceding U6 boot without this override returned to FEAT_PLM `ffffff8f`,
SS0 `04003025`, SS1 `0` and READOUT_1 `00410080` under Windows. Its boot trace
showed a completed NVIDIA D3 transition 563 ms after DdiStartDevice completed.
Static analysis of the installed NVIDIA driver found the runtime-power registry
query and consumers of its enable bit. The new boot demonstrates that the
targeted workaround preserves the unlock on this configuration.

The exact instruction that closes permissions, and whether the earlier D3 was
hot or cold, remain unproven. No boot trace was armed for RUN0008. This single
successful boot does not validate sleep/resume, hibernation, a driver update,
or persistence across a new boot without running U again. Runtime power saving
for the NVIDIA adapter is disabled by the workaround; battery impact is unmeasured.

## Existing controls

Run `tools/Set-MsiHybridRuntimePower.ps1 -Action Restore -EvidenceDirectory`
with the original absolute evidence directory to restore the exact original
value state. Administrative elevation is required. A normal Windows reboot
applies the restored policy; do not automatically reboot or reset either GPU.

For another operator-initiated U6 boot,
`tools/Complete-MsiHybridRuntimeTest.ps1 -WithoutBootTrace` with explicit
`-PolicyDirectory`, `-DriverPath` and `-Python` verifies the setting,
Intel route and Windows registers, and runs the matched benchmark only if all
unlock registers still match. It does not reboot or rerun the EFI payload.
See the [Windows tools guide](../tools/README-msi-hybrid.md) for the commands.
