# MSI offline port: review of corrections

Reviewed revision: `c26b1c3`. Scope: source inspection and CPU mocks only.
The findings below describe that revision. Their three unsafe outcomes have now
been converted to rejection regressions and corrected in the host model; see
the remediation section. This still does not qualify direct translation into a
native adapter: the exclusion/commit contract requires independent implementation
and review, and no native implementation is supplied.

These are software fault injections. They do not establish that this MSI
firmware exhibits these behaviors or quantify a physical failure probability.
No native code, GPU query, EFI execution, unlock, USB update, reboot or boot
protection change was performed. The original review changed no core behavior;
the subsequent remediation changes only the host-only core and CPU tests.

## F1 — P1: safety evidence is not preserved until its use

### DMA release after a graph callback

`port/src/session.c:91` checks reset completion, then calls the graph snapshot.
Line 93 releases DMA if the graph is valid, without re-establishing hardware
quiescence after that callback. Ownership graph validity alone does not prove
that BME, engine state or mappings remain safe.

CPU reproduction: the post-reset snapshot returns the expected graph but models
a late event enabling BME. The mock DMA-free callback observes BME enabled and
frees the allocations. A later config check refuses Windows, but the allocations
have already been freed and `dma_retained` is false. That refusal cannot undo
the unsafe release. This is permission for DMA, not evidence of an actual DMA
transfer or memory corruption in the simulated process.

### Windows handoff after final MMIO callbacks

`port/src/reset.c:70` checks config before its final MMIO sequence at line 85.
Those later callbacks can invalidate the earlier config evidence; no subsequent
BME validation or enforced exclusion protects the handoff at session.c:102.

CPU reproduction: during final MMIO reads after log close, a callback models BME
being enabled while returning expected register values. Windows invocation still
occurs with BME set. Existing late-drift tests inject faults before these checks,
not during or after them.

Required correction: specify and enforce a bounded critical-section/event
exclusion contract covering the safety proof and the protected operation. Ensure
DMA-free and handoff consume fresh evidence from the same ownership/config/
quiescence epoch. A final BME read alone is insufficient if an unbounded callback
or asynchronous owner can change state immediately afterward. Validate the
native TPL/event/ownership rules separately; blindly raising TPL around arbitrary
firmware callbacks is not an established solution.

Missing regressions: drift during post-reset snapshots, each cleanup operation,
and the final MMIO sequence; failed exclusion/epoch validation; no free/unmap
when proof is invalidated; no handoff with BME or other safety state changed.
Mocks should assert BME/quiescence at free time, not only FLR count and BAR writes.

## F2 — P2: partial console mutation is classified as an unmutated refusal

`port/src/session.c:75` calls `silence_console`; `mutated` is first set only at
line 80, before the first disconnect. A false return from the former is therefore
treated as `MSI_REFUSED` without a terminal requirement or rollback contract.

CPU reproduction: the silence callback changes console state, marks it no longer
alive, then returns false. The orchestrator returns `MSI_REFUSED`,
`mutated=false`, `terminal_required=false`, with the console still unavailable.
No driver disconnect or GPU transaction is required to reach this outcome.

Required correction: define silence as either a proven side-effect-free operation
or a tracked operation with partial-mutation reporting and verified rollback.
Distinguish console mutation from GPU mutation and define the failure policy
before entering the callback. Do not infer safe ordinary return just from the
absence of a driver detach. Test false return after each partial console change,
and failed rollback. This finding concerns possible loss of preboot console;
it does not demonstrate permanent hardware damage.

## Remaining contracts and test limits

The previous review already records unresolved DMA allocation/mapping lifetime,
terminal failure implementation, timing-service errors, pinned Windows image,
PCI I/O attributes and external ownership. They remain open. `terminal_required`
is a host result flag, not an implemented non-returning EFI path. The synchronous
recursive-entry tests validate reentry of `msi_run`; they do not establish
asynchronous firmware exclusion. Successful reset mocks assume their own
completion/register behavior and do not qualify stock no-unlock reset on the MSI.

## Original review verification (historical)

The normal port suite was rebuilt and passed **26,974 assertions / 76 scenarios**,
including R1–R4 regressions and negative firmware compilation guards. All
**39 Python tests**, pure PowerShell identity tests and PE/source verification
passed again. Existing diagnostic EFI SHA256 is unchanged:
`18efed6c4a60ce0ae682fb9c9d165f425c280a2159e32f57124f8cf3c41703f9`.
The existing diagnostic CPU executables were also rerun: **7,925 application
checks** and **192 platform checks** passed; neither executes EFI on the machine.

At review commit `3e69d8a`, `port/tests/review_followup.c` reproduced the three
unsafe outcomes. It has now been removed and replaced with normal rejection
regressions in `port/tests/followup_regressions.h`.

## Host-only remediation of F1/F2

- **F1:** acquire a purpose/epoch guard before the post-reset graph/proof and
  retain it through cleanup preparation, log close and transfer. Missing guards,
  partial acquisition and malformed leases refuse progress. Separate preparations
  no longer free/unmap DMA or close the exclusive claim. No ordinary callback
  therefore runs after actual resource release. The final compound
  `commit_windows` action independently revalidates the lease, full config,
  quiescence/register state, ownership/boundary and prepared resources, then
  releases DMA, closes the claim and transfers with no intervening ordinary
  callback. The host mock models this as an indivisible action. Revalidation
  happens at commit, so earlier readbacks and a stale guard_valid result are
  insufficient. Replay of a preparation or commit attempt is refused even
  following failure. Rejected commits retain buffers and the lease.
- **F2:** record `console_may_be_changed` before entering silence. A failure or
  subsequent failed console check now returns `MSI_STOPPED` with
  `terminal_required=true`, even without any GPU detach. GPU mutation remains
  separately tracked; the console-only error path performs no PCI/MMIO cleanup.
  There is no automatic, unverified rollback or ordinary-return permission.

Only the CPU mock implements the atomic/exclusion contract. This is not an EFI
adapter or a native TPL policy. A native implementation must establish that its
internal cleanup/transfer operations respect the exclusion, track partial cleanup
and preserve remaining resources on errors. It must refuse if that cannot be
proved; adding a boolean guard around existing callbacks is insufficient.
The real candidate remains unqualified, non-bootable and without any payload.

Current validation: **109,139 assertions / 157 workflow scenarios**, including
the former reproductions and late changes in every final MMIO callback, cleanup
preparation/log close, and action entry. Tests include each safety register,
config/BME, engine, epoch, boundary and exclusive ownership; failed/partial guard
acquisition, invalid tokens, replay attempts and partial console silence. Assertion
count includes repeated access checks, not independent safety requirements.
Both negative firmware compilation guards, the unchanged diagnostic CPU suites
(**7,925 application / 192 platform checks**), **39 Python tests**, pure
PowerShell identity tests and PE/source checks pass. The diagnostic EFI hash
above remains unchanged. All other previously documented native prerequisites,
including DMA ledger/partial cleanup, timing failures and stock reset validation,
still apply.

Run the integrated CPU regressions from the repository root (PowerShell):

```powershell
pwsh -NoProfile -File windows/ad103m-msi-ge78hx/port/Build-Offline.ps1
if ($LASTEXITCODE -ne 0) { throw 'Baseline failed' }
python -m unittest discover -s windows/ad103m-msi-ge78hx/diagnostic/tests -p 'test_*.py'
```

Only ignored host build outputs are generated. No adapter or bootable artifact
is prepared. The real candidate remains unqualified.
