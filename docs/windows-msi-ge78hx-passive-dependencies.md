# MSI passive console and USB/log dependency observations

2026-10-28. Source preparation and CPU tests only. No new EFI was built,
staged or executed; no GPU query, reset, release, unlock or protection change
occurred. The Kingston's Stage A installation is not changed by this work.

## Prepared capture

The diagnostic source now includes an optional `platform.dependencies` object.
It verifies each system console handle's registered text protocol against the
system table pointer before reading that protocol table. It records the nine
SimpleTextOut method addresses for ConOut and StdErr, the two SimpleTextIn
method addresses, the source filesystem's OpenVolume address and the active
log's Write/Flush/Close addresses: 24 entries when all observations succeed.
It does not call these captured function addresses. In particular it does not
reset a console, consume input, set a mode, open a volume or write/flush/close
a log while sampling dependencies. The enclosing diagnostic still creates and
writes its ordinary capture log on its own USB, as before.

`OpenLog` now retains the actual opened volume handle and the SHA256 of its
validated USB device path before file operations. It uses that saved handle
for filesystem lookup instead of rereading mutable loaded-image metadata.
The journal is marked opened only after a successful log open and directory
close. Dependency sampling compares the current source metadata/path against
that journal. Wrong volume, changed path, missing log, failed filesystem lookup
or unbound console interface is incomplete evidence. The sampler's log argument
comes directly from OpenLog; it is not an independently authenticated file handle.

The sampler scans at most 4,096 enumerated handles for registered LoadedImage
protocols. Captured function addresses are compared numerically to declared
image bounds, never dereferenced as arbitrary pointers. At most 32 matching
images are serialized, with their image handle/base/size, file path, code type
and bounded executable section evidence. The existing 1 MiB aggregate raw-code
budget is shared, after GPU and boundary driver samples. Exhaustion leaves
missing bytes and therefore unverified code; an image-count limit marks the
observation incomplete. Non-success image-query statuses other than the normal
unsupported/no-protocol case also make enumeration incomplete.

Invalid or wrapping image geometry is rejected before PE reads. These checks
do not make arbitrary firmware pointers trustworthy: registered protocol tables
and declared loaded-image memory still rely on firmware metadata being readable.
No memory-map validation, event exclusion or hardware fault recovery is added.
The observation is sequential, not atomic, and cannot demonstrate safe detach.

## Offline interpretation

The comparator treats absent fields in older captures as unknown. It requires
all 24 named callbacks exactly once, console binding agreement, a matching
opened log source and USB path, and complete image enumeration. Each non-null
callback must belong to exactly one captured image and fall inside its verified
non-relocated executable section. Complete raw code length and SHA256, image
geometry, BootServicesCode type and valid single-instance paths are checked.
Unmapped pointers, overlapping image ranges, absent bytes or relocations remain
unknown. A callback in allocated image data is not accepted as executable code.

Comparison uses code identity, file path and callback RVA rather than absolute
addresses or snapshot-local handles. It preserves both callback-address sharing
and image-instance sharing: moving functions into an identical second image
cannot conceal the different instance/global-state relationship. Console
interface aliasing is compared independently. Equal observations are reported
only as `dependency_diagnostics.callbacks_match`, never as release permission.

USB path ancestors are listed as collection candidates. They are **not** a
resolved storage-driver dependency closure. `storage_ownership_proved`,
`quiescence_proved` and `release_authorized` remain false even on comparison
success. The actual captures 3/4 contain no such callback/source observations;
their missing evidence is not synthesized by the comparator.

## Regressions and runner correction

CPU validation passes 7,925 application checks, 605 platform checks and 57
Python tests, including the actual C-produced passive-capture JSON. Native
protocol methods in the callback fixture point into inert test bytes; calling
them would fail the test. Cases cover mismatched/null interfaces, unmapped and
non-executable addresses, overlapping/invalid/wrapping images, missing callbacks,
image limit exhaustion, changed opened volume/path, unavailable logs, corrupted
code, ASLR/handle permutation and separate identical-code image instances.

One negative test exposed a real pre-existing image-range issue: the shared
driver code emitter could try a PE read from an image whose base plus size
wrapped. The emitter and PE-view helper now reject this before reading bytes.
The initial mock executable crashed on that test; its batch runner's
`if errorlevel 1` missed the negative Windows access-violation exit status.
Diagnostic runners now check equality to zero. Dedicated subprocess regressions
verify 0, 1, -1 and -1073741819; nonzero cannot reach the success path. The
final suite is rerun under the corrected runner, not inferred from that crash.

Repeat CPU-only checks with:

```powershell
pwsh -NoProfile -File windows/ad103m-msi-ge78hx/diagnostic/Test-Host.ps1
```

This tracked runner needs the existing private build_id.h, compiles only hosted
test programs, runs the Python/identity checks and performs read-only sanity
checks on the previous EFI. It does not link an EFI or overwrite the previous
native manifest. A separate host-only manifest records test source hashes and
`efi_built=false`, `firmware_executed=false`. Checking the previous PE's format
does not establish that it contains these new sources.

## Gates still open

Before another manual capture, review the complete new diagnostic build and
its source/header/toolchain provenance, version and exact binary hash; prepare
a dedicated USB packet separately. Existing Stage A and diagnostic binaries do
not contain these extensions. No new execution packet is supplied here.

P2 still requires real boundary/callback observations and storage dependencies.
T7 still requires review of actual Stop behavior, event notification sources,
service-internal callbacks and legal exclusion/transfer ordering. Matching code
does not prove behavior or global-state identity over time. Stock release/reset
postconditions, native timing/DMA/terminal handling and Windows driver recovery
remain separate unqualified gates. No release/reset/unlock adapter is enabled.
