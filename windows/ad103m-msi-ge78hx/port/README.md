# MSI AD103M offline port prototype

This is a **host-only, non-bootable** C prototype for PCI `10de:2717`,
subsystem `1462:13cd`, revision A1, using private captures 3 and 4.
It has no native hardware adapter, EFI entry point, linked unlock payload,
firmware installer or automatic reboot path. Building it does not access the GPU.
The real candidate remains unqualified and refused.

## Reproduce the offline validation

From the repository root, with Python and the MSVC x64 C toolchain installed:

```powershell
pwsh -NoProfile -File .\windows\ad103m-msi-ge78hx\port\Build-Offline.ps1
```

The private run3/run4 captures and their manifests must already exist under
`../captures/2026-10-27-run3` and `../captures/2026-10-27-run4`.
`prepare_fixtures.py` checks their provenance, ROM equality and relevant graph
comparison before creating fixtures. It copies the ROM only into ignored
`build/`; proprietary firmware and generated machine data are not committed.
The script compiles and executes a normal CPU test program, not firmware.
Results, source hashes and the executable hash are recorded in
`build/manifest.json`; a failed build cannot leave a previous passing manifest.

Local C is compiled with `/std:c11 /W4 /WX /O2`. The existing upstream ROM
parser is compiled separately, suppressing only its narrowing warning C4244.
Negative compilation tests verify that this prototype cannot be compiled
without `MSI_OFFLINE_HOST` or with `GU_UEFI_PORT` enabled.

## Architecture

- `src/profile.c`: validate actual ROM/FWSEC bytes and the narrowly captured
  PCI, BAR and register profile. Accept the two observed SS0 values only.
  Firmware signatures are located and hashed, not cryptographically verified.
- `src/ownership.c`: match the five internal GPU/console ownership edges and
  verify each proposed detach stage. Preserve boundary, console and logging
  fingerprints. Stable fingerprints do not resolve the identities of external
  owners or establish callback safety.
- `src/session.c`: stage the future Windows image before mutation, detach
  TextOut then GraphicsConsole then NVIDIA, obtain exclusive ownership, check
  a synthetic transaction result, reset, release DMA and hand off to Windows.
  Every operation uses an injected interface. Only RAM mocks implement it.
- `src/reset.c`: bounded pending/readiness polling, one FLR attempt, at least
  100 ms without PCI/MMIO access even if the FLR write reports failure,
  restore pinned BARs with decoding disabled, leave bus mastering disabled,
  and validate post-reset state again immediately before handoff.

The synthetic transaction callback models the contract needed by a later port;
it does not implement or execute the unlock. Post-transaction SEC2 HALT must be
readable: the diagnostic's optional `SEC2_CPUCTL` BADF exception is deliberately
not accepted as evidence of completion. Tests also model explicit skip-to-Windows.

After a possible mutation, failure stops progression and attempts to disable bus
mastering. Unproven DMA allocations remain retained. There is no second attempt,
automatic stock fallback or forced reset/power-off recovery. Containment itself
can fail and is reported separately. A returned Windows start callback is always
an error, including a nominal success return; it proves no operating-system boot.

## Validation result — 2026-10-27

The offline C suite passed **5,789 assertions across 43 workflow scenarios**.
Coverage includes both captures, profile/ROM mutations, missing/extra/duplicate
owners, each partially failed detach, boundary/console/logging drift, exclusive
claim failure, narrow decode restoration, invalid transaction proofs, inaccessible
post-transaction SEC2, pending/readiness timeouts, failed FLR write, mandatory
quiet time, BAR restoration, late MSI/identity/BAR/register drift, DMA retention,
cleanup failures, failed containment, skip and returned Windows handoff.
Session reuse and missing callbacks are rejected. These are CPU simulations.

The real qualification fixture sets ownership, console callbacks, Windows image
and assets review to false. Only tests use explicitly synthetic approval flags.
No test result authorizes a hardware transaction.

## Required before a native port or hardware validation

1. Review the unresolved external ownership boundary and console callbacks from
   the [GOP analysis](../../../docs/windows-msi-ge78hx-gop-console-analysis.md).
   Captures cannot demonstrate that console silence or detach is safe.
2. Implement and independently review a native UEFI adapter: protocol-aware
   stop/claim, BAR mapping, DMA lifetime, logging lifetime and failure handling.
   The host compilation guards must not simply be removed to make an EFI build.
3. Qualify the exact MSI firmware, Windows boot manager/device path, parser and
   future transaction assets with pinned provenance. The capture profile alone
   does not verify BIOS version, Windows driver or cryptographic acceptance.
4. Review the actual transaction and its completion/readback contract separately.
   Mock marker/HALT/selectors are not evidence that MSI firmware will accept it.
5. Design an explicitly approved, staged hardware validation plan with recovery
   media and an accessible BitLocker recovery key. Native release, FLR behavior,
   display continuity and Windows recovery remain untested on this laptop.

The future hardware sequence must establish release/ownership first, then reset
and Windows handoff with an appropriate no-unlock test design, and only later
consider an unlock after separate explicit validation. A no-unlock reset needs
its own stock postconditions; it cannot use this prototype's synthetic unlocked
selector expectations. Windows success requires observed boot, NVIDIA driver
binding and display recovery, followed by stability testing; StartImage invocation
alone is insufficient.

The principal remaining risks are loss of preboot display/input, a frozen boot,
failed reset, DMA corruption if ownership or completion assumptions are wrong,
and inability to return to Windows without a cold recovery. Host tests cannot
measure their probability or guarantee recovery. No VBIOS flash is part of this
prototype. Secure Boot/BitLocker policy changes are neither performed nor tested.

This turn changed only repository source and ignored host build outputs. It did
not query the GPU, execute EFI, update the USB stick or alter boot protections.
