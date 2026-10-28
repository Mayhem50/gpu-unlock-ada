# Stage A: normal Windows handoff only

This is a separate, deliberately limited EFI application for the first manual
boot test. It is **not an unlock adapter**, a release/reset test, or proof that
the MSI GPU can recover after a firmware transaction. The GPU port under
`../port` remains host-only and its native compilation guards are unchanged.

The application locates exactly one privately pinned internal ESP, reads only
`\EFI\Microsoft\Boot\bootmgfw.efi`, verifies the complete byte count and SHA256,
and passes those verified bytes with their exact device path to LoadImage.
It checks the child's volume, parent, options, image geometry and file path.
It closes its files/frees its ordinary CPU buffers, discards previously queued
keys, and asks for **W**. It checks the staged image metadata again after the
last prompt/console callback before invoking StartImage once.

Esc, timeout, a mismatched pin or an ordinary pre-start refusal aborts. There is
no automatic OS launch on timeout, no fallback image search and no retry.
An unproved child image owner, failed cleanup, or any returned StartImage enters
a RAM-only nonreturning pause loop. No console/cleanup callback follows a
returned StartImage. The application does not alter the inherited firmware
watchdog; firmware may apply its own watchdog policy if a boot hangs.

No PCI/GOP protocol, GPU read/write, display detach/reconnect, GPU DMA, reset,
TPL manipulation, NVIDIA asset, VBIOS flash, boot variable or file write exists
in this application. Ordinary pool allocation and file reads are used. Console
services and Windows itself perform their normal platform activity; the entire
OS boot is not a read-only operation. Secure Boot is enforced by the firmware;
this unsigned application cannot authorize itself under an incompatible policy.

## Prepared artifacts and provenance

Build locally with PowerShell 7:

```powershell
pwsh -NoProfile -File windows/ad103m-msi-ge78hx/stock-handoff/Build.ps1
```

This reads disk/ESP metadata and the existing Windows servicing copy; it does
not mount the ESP, modify partitions, query the GPU or execute the EFI. The
Windows copy must have a valid Microsoft Windows Authenticode signature and
be an x64 EFI application. Direct ESP bytes are used only when readable and
identical; disagreement is a refusal, not a reason to widen the profile.

With a non-admin build process, direct ESP reading may be denied. The build
can still produce a provisional artifact pinned to the authenticated Windows
copy. It records `esp_confirmation_pending=true`, and the USB staging script
refuses it. The EFI itself would also refuse a differing ESP file at runtime.
An operator's admin read-only fingerprint may confirm equality; save the
returned `TailleOctets`/`SHA256` object in an ignored local JSON and pass it:

```powershell
pwsh -NoProfile -File windows/ad103m-msi-ge78hx/stock-handoff/Build.ps1 -EspConfirmation windows/ad103m-msi-ge78hx/stock-handoff/build/esp-confirmation.json
```

Never fabricate this confirmation from the Windows copy. A differing ESP
fingerprint needs separate investigation, not a relaxed hash or forced launch.
If an existing `bootmgfw.efi` or `bootmgfw_EX.efi` under the Windows component
store matches the operator's ESP bytes exactly, `-EspImage` may select that
reference. It must have a valid Microsoft Windows signature, pass the PE checks
and match the confirmed byte count and full SHA256. The runtime still accepts
one exact hash; this does not add alternate accepted images or change the ESP.
On 2026-10-28 the operator's ESP fingerprint matched an authenticated WinSxS
`bootmgfw_EX.efi`, version 10.0.28000.376. The ordinary servicing copy differed.
Four negative profile-selection tests verify missing confirmation, an invalid
reference location, a wrong fingerprint and the mismatching default servicing
copy cannot overwrite the approved profile.
Windows updates may invalidate the pin; re-read/review and rebuild rather than
accept another image. Partition GUIDs, local fingerprints and build outputs
remain ignored. No Windows/NVIDIA proprietary image is embedded or published.

Outputs under ignored `build/`:

- `MSI-STOCK-A.EFI`: unsigned, stock-only x64 application.
- `profile.json` / `profile.h`: private ESP/geometry/image pins and read provenance.
- `manifest.json`: binary/input/toolchain hashes, tests, pending ESP confirmation,
  and separate `deployed`/`firmware_executed`/`hardware_tested` fields.
- `host-validation.log`, negative build-guard logs and compiler/linker identity.

## Validation and limits

The real `StockRun` function runs against fake UEFI services in
`tests/host_tests.c`: **59 cases / 1,909 assertions**. This includes duplicate
ESP matches, malformed device paths, short/extra/corrupt input, wrong child
identity/options, late child mutation, partial cleanup failures, ambiguous
LoadImage/allocation outputs, reentry, caller-reference mutation, queued keys,
timeout/escape and returned Windows images. Host tests never call real boot
services. The native entry's pause loop is compiled/inspected, not executed.

The verifier checks the EFI PE, header pins, authenticated image pin and an
explicit firmware-call whitelist. Source checking is supplementary to review;
it is not a formal proof of firmware behavior. This application has not been
tested on the physical MSI. It does not resolve GPU-port T2/T5–T9 or validate
GOP release, an FLR, DMA quiescence or Windows recovery after those operations.

## USB staging — separate from execution

The staging script refuses pending ESP confirmation, stale source/build hashes,
a changed USB identity, non-FAT32 media, or an unexpected existing fallback.
It only accepts the reviewed diagnostic as the previous USB `BOOTX64.EFI`,
backs it up locally and verifies both copies before replacement. It never
formats, changes boot entries, alters the Windows ESP, changes protections,
reboots or runs the EFI. E: must still be the Kingston identity recorded in
the private build profile. No operation targets F: or any other drive.

Only after the profile is confirmed and the exact build is reviewed:

```powershell
pwsh -NoProfile -File windows/ad103m-msi-ge78hx/stock-handoff/Stage-Usb.ps1
```

Deployment is not firmware execution and neither proves hardware success.
Current build/deployment state is recorded locally, not inferred from this README.

## Manual Stage A test — only after the remaining prerequisites

Before booting, confirm the actual ESP fingerprint, idle workloads/saved work,
AC power, a working stock Windows Boot Manager path and independent recovery-key
access. Check then-current Secure Boot/BitLocker status in admin PowerShell:

```powershell
Confirm-SecureBootUEFI
```

```powershell
manage-bde -status C:
```

These commands read status only. Do not paste a recovery password. Boot-path
changes may trigger BitLocker recovery. No protection change is performed or
authorized by this package; an incompatible security policy blocks this test.

After prerequisites and verified USB staging, use the MSI boot menu (F11,
[MSI guidance](https://us.msi.com/support/technical_details/NB_OS_USB_Device_Detection)),
select the Kingston USB, and verify the **MSI STAGE A v0.1** banner. At A5,
press **W** to start the pinned Windows Boot Manager, or Esc to abort. Leave
normal internal boot as the default. Do not execute any other EFI/payload.

Record the last stage/status and whether Windows actually reaches login with
normal display/input and NVIDIA driver initialization. A6 is only an attempted
handoff, not success. There is intentionally no USB log from this application;
on refusal the screen shows the stage/status when console output is usable.
If it stops, no retry/reset/cleanup is initiated by the application; return to
the normal internal Windows Boot Manager via the external firmware boot path.
Any manual shutdown has the ordinary risk of unsaved work/data loss. Do not
treat the presence of two GPUs or a single successful Stage A boot as guaranteed
recovery from a later GPU experiment.

Afterward restore the reviewed diagnostic from the verified local USB backup
if needed. No automatic restoration, permanent boot change, GPU unlock or
security-policy restoration occurs in this application.
