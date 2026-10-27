# Review of the raw FLR gate and clean stock lifecycle corrections

Reviewed revision: `db881c1`, 2026-10-27. Local source/reference review and CPU
simulations only. No GPU query, EFI execution, unlock, USB update, reboot or
boot-policy change. This review changes no core behavior.

## Confirmed

F3's backend gate is spent before callbacks/effects, independent of counters.
Repeat requests through the valid FLR byte/word/dword encodings are refused
without a config access. Failed first writes, with or without modeled effects,
spend the same attempt and receive quiet time. Recursive requests and repaired
phase/counter state do not rearm the gate. The helper remains one-shot as well.
No additional F3 defect was found in the reviewed single-threaded fault model.
This does not qualify real firmware concurrency or a native timing service.

F4 checks clean scoped lifecycle after log close and after lease callbacks,
including failed partial acquisition. The valid-lease stock action also checks
it directly before transfer. Existing regressions correctly cover each dirty
field separately, unknown bookkeeping, retry/replay and the clean stock flow.

These remain consistent with the reference's intent: one reset attempt,
no uncertain free and an unmodified Windows handoff only after proof. The local
AD104 reference checks allocation/DMA/pending-attribute state in GuSafeToReturn
and separates terminal failure from successful handoff. Our atomic commit is
still a CPU-only abstraction, not an implemented UEFI equivalent.

## F5 — P2: invalid lease skips stock resource-uncertainty classification

In `port/tests/host_tests.c:commit_windows`, this early return precedes the
stock lifecycle predicate:

    if (!guard_current(m,g) || !m->log_closed) return;

If a late fault both invalidates the lease and introduces live/unknown DMA
bookkeeping, the predicate that sets the output dma_retained flag is skipped.
The orchestrator reaches MSI_STOPPED with terminal_required and guard_retained
true, so it still refuses Windows and keeps the terminal guard. But its
dma_retained flag incorrectly remains false while the mock journal has one
live buffer. The existing tests inject one dirty property at a time and miss
this combination.

`tests/review_f3_f4.c` reproduces three variants: broken lease plus live DMA,
epoch drift plus live DMA, and broken lease plus live/unreadable bookkeeping.
All refuse transfer and perform no free, config/MMIO operation or GPU transaction.
This is a retention-reporting/contract gap, **not evidence of an unauthorized
Windows handoff or a buffer actually freed in these tests**. P2 reflects that
the outer terminal guard still works, while future cleanup/status consumers
must not receive false evidence that resources need no retention.

Required correction: classify resource uncertainty conservatively before every
stock early exit, independently of lease/log readiness. Invalid lease must not
prevent recording live or unknown owned resources. Preserve the one-shot commit
and terminal/guard retention, and add no GPU query/reset/free to this path.

Missing regressions: dirty/unreadable lifecycle combined with invalid lease,
epoch, log readiness and callback failure; check output retention as well as
refused handoff. Continue checking no GPU accesses, no free and no replay.

## Verification and scope

Normal suite rebuilt: **304 scenarios / 190,413 assertions** pass, including
negative compilation gates. The existing diagnostic CPU executables pass
**7,925 application / 192 platform checks**; **39 Python tests**, pure identity
tests and PE/source verification pass. Diagnostic EFI SHA256 remains
`18efed6c4a60ce0ae682fb9c9d165f425c280a2159e32f57124f8cf3c41703f9`.

The separate review probe exits zero when it reproduces F5; it is not a passing
safety test. Convert it to rejection/retention regressions when F5 is corrected.
V0 in the [checklist](windows-msi-ge78hx-launch-checklist.md) is partial again;
native phases, DMA ledger, timing, exclusion and recovery remain open.

CPU reproduction from the repository root:

```powershell
pwsh -NoProfile -File windows/ad103m-msi-ge78hx/port/Build-Offline.ps1
if ($LASTEXITCODE -ne 0) { throw 'Normal validation failed' }
$reviewCmd = Get-Content windows/ad103m-msi-ge78hx/port/build/compile.cmd -Raw
$reviewCmd = $reviewCmd.Replace('tests\host_tests.c','tests\review_f3_f4.c').Replace('msi-offline-tests.exe','review-f3-f4.exe').Replace('build\validation.log','build\review-f3-f4.log')
Set-Content windows/ad103m-msi-ge78hx/port/build/compile-f3-f4-review.cmd -Value $reviewCmd -Encoding ascii
& $env:ComSpec /d /c windows\ad103m-msi-ge78hx\port\build\compile-f3-f4-review.cmd
if ($LASTEXITCODE -ne 0) { throw 'Review probe failed' }
Get-Content windows/ad103m-msi-ge78hx/port/build/review-f3-f4.log -Tail 3
```
