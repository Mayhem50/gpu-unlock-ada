# Review of the raw FLR gate and clean stock lifecycle corrections

Originally reviewed revision: `db881c1`, 2026-10-27. F5 below describes that
revision and is now corrected in the host model; see remediation below.
Local source/reference review and CPU
simulations only. No GPU query, EFI execution, unlock, USB update, reboot or
boot-policy change. The original review changed no core behavior; subsequent
remediation changes only RAM classification and CPU mocks/regressions.

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

## Original verification (historical)

Normal suite rebuilt: **304 scenarios / 190,413 assertions** pass, including
negative compilation gates. The existing diagnostic CPU executables pass
**7,925 application / 192 platform checks**; **39 Python tests**, pure identity
tests and PE/source verification pass. Diagnostic EFI SHA256 remains
`18efed6c4a60ce0ae682fb9c9d165f425c280a2159e32f57124f8cf3c41703f9`.

At review commit `85f1d9d`, the separate probe reproduced F5. That file has
been removed; its three cases are now normal rejection/retention regressions
in `tests/retention_regressions.h`.

## F5 remediation and current validation

A shared RAM-only `msi_stock_retention` classifier records dirty, missing or
unreadable inventory independently of lease/log readiness. It never clears an
existing retention requirement, even if a later observation is clean. The
mock commit invokes this classification before state, replay, token, lease and
log early exits, and again after its reentry probe. Stock/unknown execution is
recognized from backend phase/mode as well as the token, so invalid lease data
cannot suppress resource accounting. No GPU query or repair is introduced.

The compound action still refuses transfer, preserves the terminal requirement
and retains any acquired/uncertain guard. Missing resources cannot be inferred
from failed ownership/readiness checks. A known-clean inventory with an ordinary
lease/log failure does not invent live DMA; an already uncertain inventory
cannot become safe merely by repairing counters or retrying a spent action.

The integrated regressions cover the three original reproductions, all ten
scoped uncertainty fields combined with ten early-exit categories (broken,
changed or inactive lease; wrong/missing token; log readiness; unexpected state;
spent action; unknown/incorrect mode), journal repair/replay and clean controls.
They verify retention, no handoff/free, terminal/guard preservation and zero
config/MMIO/transaction accesses on the stock error paths.

Current result: **410 workflow scenarios / 191,252 assertions** pass, alongside
the negative compilation guards, diagnostic CPU suites (7,925 application and
192 platform checks), 39 Python tests, pure identity tests and PE/source checks.
The diagnostic EFI hash above is unchanged. Failed-build manifest invalidation
is also checked. Assertion totals include repeated invariant checks.

V0 is complete as a host contract/regression in the
[checklist](windows-msi-ge78hx-launch-checklist.md). Native phases, a real DMA
ledger, timing services, exclusion, terminal stop and actual Windows recovery
remain open. No native adapter, payload or hardware qualification is supplied.

Run integrated CPU validation from the repository root:

```powershell
pwsh -NoProfile -File windows/ad103m-msi-ge78hx/port/Build-Offline.ps1
if ($LASTEXITCODE -ne 0) { throw 'Normal validation failed' }
python -m unittest discover -s windows/ad103m-msi-ge78hx/diagnostic/tests -p 'test_*.py'
```
