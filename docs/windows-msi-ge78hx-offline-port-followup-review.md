# MSI offline port: review of corrections

Reviewed revision: `c26b1c3`. Scope: source inspection and CPU mocks only.
The four original corrections hold for their integrated regression cases.
However, the core is not ready for direct translation into a native adapter:
two remaining blocking findings have three reproducible unsafe outcomes under
the late-event/partial-callback fault model already used by this project.

These are software fault injections. They do not establish that this MSI
firmware exhibits these behaviors or quantify a physical failure probability.
No native code, GPU query, EFI execution, unlock, USB update, reboot or boot
protection change was performed. This review changes no core behavior.

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

## Verification

The normal port suite was rebuilt and passed **26,974 assertions / 76 scenarios**,
including R1–R4 regressions and negative firmware compilation guards. All
**39 Python tests**, pure PowerShell identity tests and PE/source verification
passed again. Existing diagnostic EFI SHA256 is unchanged:
`18efed6c4a60ce0ae682fb9c9d165f425c280a2159e32f57124f8cf3c41703f9`.
The existing diagnostic CPU executables were also rerun: **7,925 application
checks** and **192 platform checks** passed; neither executes EFI on the machine.

`port/tests/review_followup.c` records the three reproductions separately from
the normal suite. Its zero exit status means the unsafe fault-model outcomes
were reproduced; it is not a passing safety qualification. Convert these probes
to rejection regressions when fixing F1/F2.

Reproduce with CPU-only compilation from the repository root (PowerShell):

```powershell
pwsh -NoProfile -File windows/ad103m-msi-ge78hx/port/Build-Offline.ps1
if ($LASTEXITCODE -ne 0) { throw 'Baseline failed' }
$followupCmd = Get-Content windows/ad103m-msi-ge78hx/port/build/compile.cmd -Raw
$followupCmd = $followupCmd.Replace('tests\host_tests.c','tests\review_followup.c').Replace('msi-offline-tests.exe','review-followup.exe').Replace('build\validation.log','build\review-followup.log')
Set-Content windows/ad103m-msi-ge78hx/port/build/compile-followup.cmd -Value $followupCmd -Encoding ascii
& $env:ComSpec /d /c windows\ad103m-msi-ge78hx\port\build\compile-followup.cmd
if ($LASTEXITCODE -ne 0) { throw 'Review probes failed' }
Get-Content windows/ad103m-msi-ge78hx/port/build/review-followup.log -Tail 5
```

Only ignored host build outputs are generated. No adapter or bootable artifact
is prepared. The real candidate remains unqualified.
