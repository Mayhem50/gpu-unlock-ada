# MSI GE78HX: UEFI handoff feasibility and failed timing services

2026-10-28. Scope: source/specification review and CPU mocks. No hardware
query, benchmark, EFI build/launch, native adapter, USB deployment or protection
change in this work. This document is not an executable hardware test packet.

## Result

A continuous `RaiseTPL`-only implementation of our abstract final commit is
not established and must not be substituted for it. The native T7 gate remains
open. A separate host defect is now addressed: a failed or short timing service
cannot authorize post-FLR accesses. When quiet time is unproven, even ordinary
failure cleanup/log close is suppressed. Native timing and terminal behavior
are still unqualified; this does not make the machine ready for an unlock.

## Specification evidence and interpretation

The reviewed primary source is the official UEFI specification repository,
[`07_Services_Boot_Services.rst` at 82804ed40f173589b372fa303d1e69956dcfa153](https://github.com/UEFI/UEFI-Specification-Release/blob/82804ed40f173589b372fa303d1e69956dcfa153/source/07_Services_Boot_Services.rst).
Relevant sections: TPL Usage/Restrictions, event notification queues, RaiseTPL
and RestoreTPL. The [published UEFI 2.10 chapter](https://uefi.org/specs/UEFI/2.10/07_Services_Boot_Services.html)
also describes this execution model.

The table requires LoadImage/StartImage below TPL_CALLBACK; for the exposed
application TPL choices this means TPL_APPLICATION. File-system operations may
run up to TPL_CALLBACK, memory allocation services up to TPL_NOTIFY. Stall has
a broader service limit, which is not permission for applications to use the
firmware-reserved HIGH_LEVEL as a general critical section. Notifications queued
at masked levels can execute when the level is lowered. Higher-priority firmware
activity is not excluded merely by using TPL_NOTIFY.

**Our inference:** a raised-TPL snapshot is not a portable guarantee spanning
cleanup, protocol close, lowering TPL and OS transfer. Per-service legality,
callbacks and actual competing mutation sources require their own evidence.
Neither a token/epoch nor a successful run on ASUS supplies that evidence for MSI.

Source lines checked: 79–112 (usage), 306–314/345–359 (selected restrictions),
542–544 (notification queues), 1099–1148 (raising/restoring). Source text is
cached only under ignored `build/audits/uefi-handoff-policy/`; no external code
was executed. The source provenance is recorded in
[the manifest](evidence/uefi-handoff-timing/source-manifest.json).

## Counterexamples to the blanket-TPL proposal

| Proposed sequence | Why it does not establish the contract |
| --- | --- |
| Raise NOTIFY; prove state; release DMA/claim; StartImage at NOTIFY | StartImage violates its TPL restriction |
| Raise NOTIFY; prove state; release DMA/claim; restore APPLICATION; StartImage | A pending notification can run during restore and invalidate state after release |
| Restore APPLICATION; re-read state; StartImage | A new notification or service-internal callback can intervene after the read; a final read alone is not exclusion |
| Keep NOTIFY throughout log close, cleanup and transfer | File-system and image-service constraints differ; extended elevated execution is not a general solution |
| Use HIGH_LEVEL instead | Reserved firmware context, not an application escape hatch |

These are specification-derived possible schedules, not observations that a
particular MSI callback actually executes them. We must identify the relevant
sources rather than assume all firmware callbacks write the GPU. Conversely,
not observing an event in two captures cannot prove its absence.

## Requirements before any native realization

1. Pin/load the reviewed Windows image before release, at a legal TPL; include
   its private origin/path/options and loaded-image ownership in the journal.
2. Resolve the GPU graph and relevant external owners/callbacks from P2. Review
   the actual implementation and notification footprint of each protocol/service
   used in the final interval, including clock, stall, Unmap, FreeBuffer and
   CloseProtocol. A list of allowed TPL values is not a callback audit.
3. Show how relevant asynchronous mutators are quiesced and how that exclusion
   persists across restoring TPL, each service call and claim release. An epoch
   can detect a mutation already observed; it cannot prevent a future mutation.
   Merely canceling this application's own timers is not global exclusion.
4. Keep all DMA mappings/allocation records until quiescence and their individual
   cleanup outcomes are proved. After partial cleanup retain the unresolved
   records and guard; do not retry, reconnect, reset again or start another image.
5. Specify a nonreturning terminal path which requires neither console/file I/O
   nor GPU access. Returning from the host function remains a test mechanism,
   not the future EFI failure behavior.

Whether to hold a claim through transfer, close it in a specially reviewed
sequence, or redesign the handoff is unresolved. None is silently approved here.
Do not implement the native adapter until this feasibility review is resolved.

## Host timing correction

Previously `MsiOps.stall` returned void. The model assumed the requested time
elapsed and performed ordinary PCI containment/log cleanup after any error.
That was inadequate for a reset whose quiet interval could not be confirmed.

The model now requires:

- A status-returning stall and a separate monotonic `clock_us` callback. The
  latter's contract is CPU-only: no target PCI/MMIO, console or file callback.
- Timestamps around the delay **after** the FLR write returns; both clock reads
  must succeed, time must not go backwards and elapsed time must meet 100 ms.
  Even a failed write may have taken effect, so the full delay is attempted.
- Status success and elapsed-time evidence are independent. A failed stall with
  a separately proved full quiet interval still aborts the transaction, but may
  enter ordinary containment. An unproved interval requires RAM-only stop.
- `quiet_uncertain` is set before the reset write and cleared only by adequate
  elapsed-time evidence. While set, no further PCI/MMIO, ownership snapshots,
  log close, DMA cleanup or transfer is invoked. The uncertainty also refuses
  replay after repairing the session state/reset counter.
- Pending (100 ms) and readiness (1 s) budgets use observed elapsed time. Failed,
  frozen/backwards clocks or short waits refuse continuation. Oversleep beyond
  the total budget does not issue another device poll.

An unavailable clock still does not skip the requested quiet delay attempt.
Raw clock values, validity, delay status and request are retained in `s.quiet`.
`containment_verified` stays false after unproved quiet time: lack of a fresh
observation is not a BME-off proof.

The fake clock is not a real calibrated counter. Counter frequency, rollover,
monotonicity across platform power states, and the native Stall implementation
still need qualification. Deadlines are checked at call boundaries; they cannot
interrupt a hung firmware/PCI/clock service. This is not a hard watchdog or a
proof that a native failure can terminate safely.

## Offline validation

`tests/timing_regressions.h` runs in the normal host suite. Cases include:

- successful/failed/possibly ineffective FLR combined with zero/partial/full
  elapsed delays and status failures;
- clock failure before/after quiet, a frozen/backwards counter and rollover;
- no external callbacks after unproved quiet, resource/claim retention and
  rejection after session-state/counter repair;
- pending/readiness delay failure, short success, elapsed-budget overshoot and
  missing/failed timing capability.

Current C result: **499 workflow scenarios / 243,622 assertions**, plus the
existing **45 isolated DMA ledger cases**. Counts include repeated checks,
not independent hardware requirements. The full normal offline validation also
covers the diagnostic CPU mocks, Python tests, identity parser and existing
diagnostic PE/source verification. No Linux full-core or AD104 hardware suite
is implied by these results.

## Later staged test plan — blocked, no binary or launch command

| Stage | Intended bounded behavior | Required evidence before preparing its binary |
| --- | --- | --- |
| A | Reviewed Windows image handoff only; no GPU detach/reset/firmware work | Private boot-image/path pin, stock boot and display/input/recovery route, legal service/error/return policy |
| B | Targeted display release and stock Windows handoff; no unlock payload | A plus P2/P3 ownership/callback review, legal exclusion/terminal behavior and explicit stock-state postconditions |
| C | Single stock reset then Windows; no unlock payload | B plus qualified timing/PCI restore/post-FLR sequence and actual recovery criteria |
| D | Single unlock transaction, then matched workload measurements | Earlier gates, full firmware phase model/review, qualified native implementation and separate exact-binary authorization |

Stages B/C mutate GPU/platform state even without an unlock. A missing payload
in someone else's tool is not a read-only mode: firmware replay may still write
or reset hardware. Existing model success selectors are unlocked selectors;
they must not be reused as stock postconditions for B/C.

Before any later hardware stage: verify then-current idle state, target identity,
security policy, independently accessible recovery key/backups, known stock boot,
external recovery/display/input route and exact reviewed source/binary/input
hashes. Do not log the recovery key. The last reported Secure Boot/BitLocker
state is historical, not queried or changed here. Two GPUs do not demonstrate a
working recovery path. No guaranteed recovery or zero-brick-risk claim is made.

Record separately: preconditions, last stage reached, actual Windows login,
display/input/NVIDIA initialization, new driver errors, and a clean stock return.
A pre-handoff log line or a StartImage call is insufficient. The two benchmark
batches already saved are performance references, not evidence for stages A–D.
