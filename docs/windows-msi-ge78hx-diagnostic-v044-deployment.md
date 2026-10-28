# Corrected diagnostic 0.4.4 prepared and staged — 2026-10-28

The operator requested preparation, offline validation, a backup of the current
Kingston version and USB replacement, without GPU access or EFI/unlock execution.
That scope is complete. No reboot, firmware launch, GPU query/reset/detach,
unlock, flash, internal boot modification or protection change was performed.

| Evidence | Value |
| --- | --- |
| Version/banner | 0.4.4 |
| Bytes | 38,912 |
| Final USB SHA256 | `a97ebe9678e0d786ec12fdf32885415d51ed62f63e3243504ab2b38b999328f2` |
| Build ID | `5be4190808ab2fd9c320ec7018b066f5d75922de4df2fd093f189f9adf72afc3` |
| Previous 0.4.3 SHA256 | `2463cfd10c1524f04b4b4c91b87412d483fa2dccc254d991f07a892647e6cb6c` |
| Deployment journal | ignored `diagnostic/build/deployment-044.json`, status `verified` |
| Firmware execution / GPU access / protection changes | false / false / false |

This build includes the [capture-5 classification correction](windows-msi-ge78hx-capture5-remediation.md).
Valid unrelated large image ranges require no PE/path/code read. Relevant large
images, invalid geometry, missing metadata and ambiguous coverage remain
unverified/refused. Image-scan reasons are now reported. The code-reading caps,
unknown result of capture 5 and narrow MSI SS0 admission are not relaxed.

## Validation before USB writes

- Native /W4 /WX build, pinned header/input snapshot, deterministic link and
  sealed source/generated-input/toolchain hashes passed; PE/source checks pass.
- Diagnostic: 7,925 application checks, 722 platform checks, 68 Python tests,
  plus pure identity checks. 0.4.4 keeps strict BAR/SEC2 continuation validation;
  previous capture versions remain supported with their own manifests.
- Host MSI port: 500 scenarios / 243,628 assertions; admission adapter:
  148 cases / 3,211 assertions. All are CPU simulations, not hardware proof.
- USB staging policy: 20 RAM-only admission/refusal tests passed. New artifact
  version/build ID/hash/size are pinned independently of the old fallback pin.

The exact sealed local packet is ignored under
`diagnostic/build/packets/diagnostic-0.4.4-a97ebe9678e0d786/`. Packet verification
was repeated before staging. The native build archived the previous local image
and metadata separately. No NVIDIA payload or write/reset/unlock path was added.

## Verified USB backup and replacement

E: matched the reviewed Kingston serial, size, partition GUID, USB bus, FAT32
and label. Disk numbers were not treated as identity. Its current fallback
matched the reviewed 0.4.3 SHA256 before any replacement.

The actual USB fallback was copied and verified at:

`windows/ad103m-msi-ge78hx/diagnostic/build/usb-backups/20261028T1747538848553Z/DIAG-043-BOOTX64.EFI`

That backup directory also contains the USB's exact 0.4.3 manifest and review
instructions, copied with digest verification. The earlier Stage A backup is
preserved. Existing captures and the old USB metadata directory are untouched.

The new packet/manifest/instructions are independently stored under
`E:/AD103M-PACKETS/<full 0.4.4 SHA256>/`. Only the EFI fallback was replaced by
the verified temporary-copy/rename sequence, with identity/current-byte checks
before replacement and a final destination SHA256 check. No formatting, F:
operation, internal ESP/BCD/BootOrder or boot-protection modification occurred.
The separate deployment journal records staging; sealed manifest false values
describe build-time state and are not silently rewritten.

## Remaining manual test

No 0.4.4 capture exists yet. The next deliberate boot from the Kingston starts
**diagnostic v0.4.4 directly**, without Stage A's W prompt. Its runtime includes
the existing bounded PCI/MMIO/PROM reads and a new log on its own USB. It is not
a zero-GPU-access program when executed, and no execution took place here.

Before a separately chosen test, check current security/recovery access, saved
work, idle GPU workloads and AC. Keep the new capture with the 0.4.4 manifest;
do not assume its CAP number or overwrite historical captures. A successful
diagnostic remains insufficient to qualify release, reset, TPL/event exclusion,
Windows driver recovery after mutation or unlock.
