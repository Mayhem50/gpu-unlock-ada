# Review of FBPA/readout and lifecycle ownership changes

Reviewed revision: `729b38f`, 2026-10-27. Source review, local reference comparison
and CPU mocks only. No GPU query, EFI execution, unlock, reboot, USB deployment
or boot-policy modification. Core behavior is unchanged by this review.

## Confirmed alignment

- The four PLM offsets and three required values match
  `windows/ad104-msi/vendor/unlock_610.c:ad102_permissions`.
  FBPA must be readable/non-BADF, but need not equal all ones. Independent
  config/chip checks make all-one PLM values admissible; transport failure does
  not. All four samples remain available after an earlier PLM failure.
- Effective readout uses the reference's low-27-bit full-rate mask, rejecting
  transport failures, all ones and BADF. Checks run after the synthetic phase
  and again after pending polling before the owned FLR. Existing post-FLR checks
  remain. None proves real firmware/signature execution or ordered selector writes.
- The typed pre-reset boundary rejects a complete/self-resetting transaction
  declaration before mutation; incompatible direct binding fails compilation.
  The private reset helper consumes its state before callbacks and refuses
  recursive/repeated entry. Cleanup stays deferred to guarded final commit.

Two gaps remain in the claimed ownership/fail-closed model. The probes inject
callback/event faults; they do not show that this MSI firmware causes them.

## F3 — P1: raw reset primitive has no single-use gate during the reset phase

The reset helper in `port/src/reset.c` spends `s->flrs` before the FLR, correctly
preventing another call to that helper. However the mock raw write primitive in
`port/tests/host_tests.c` only checks `S.state == MSI_RESET` before issuing FLR.
That state spans the reset write, quiet period, readiness and restoration. It
does not reject another raw FLR once `life.resets` is already nonzero.

Reproduction: a readiness-read callback issues a second raw FLR after the first
quiet period, waits another 100 ms, and returns normal identity. Both modeled
reset effects are accepted. The later lifecycle check detects two resets and
stops before free/Windows, but cannot undo the extra reset: `S.flrs=1`, actual
mock journal `resets=2`. This does not mean normal orchestration calls twice;
it means its trusted backend has not enforced the single-use guarantee against
the callback/event fault model used by these tests.

Required correction: consume a backend reset authorization/attempt budget before
the first raw write, including partial/failed writes; refuse any repeat before
effects and record a sticky violation. A phase label or after-the-fact journal
comparison is insufficient. The native implementation must enforce the same
contract; this is not C memory isolation against a malicious backend.

Missing regressions: duplicate raw FLR during readiness/restore, reentrant raw
writes, failed first writes and repaired state. Check that only one raw attempt
can reach the virtual hardware, and uncertain resources stay retained.

## F4 — P1: stock handoff bypasses the final clean lifecycle invariant

`port/src/session.c` checks an empty lifecycle at entry and after staging, but
the skip/stock branch then closes the log, acquires a guard and commits without
checking it again. The stock branch of the mock `commit_windows` checks the
lease and absence of transaction/stops, not empty scoped resource bookkeeping.
The final/unlocked branch does check its lifecycle within `atomic_safe`.

Reproduction: log close introduces one live DMA buffer in the virtual lifecycle
journal. Stock Windows invocation still occurs with `live_dma=1`, despite no
transaction/detach. There are zero config/MMIO accesses in that reproduction;
all state changes are RAM bookkeeping. This is scoped prototype-owned resource
state, not a requirement that unrelated firmware/driver resources be absent.

Required correction: recheck scoped clean lifecycle after stock cleanup and
validate it at the guarded stock action itself, including all counters and the
sticky violation flag. Refuse unreadable/dirty state, retain uncertain resources
and the guard, and prohibit ordinary return/fallback. Do not introduce a GPU
query/reset/free to repair a stock-path bookkeeping failure.

Missing regressions: each counter/live resource/violation dirtied at log close,
guard acquisition and action entry; lifecycle-read failure; stock retries;
no GPU config/MMIO access or unauthorized cleanup in the failure path.

## Evidence and remaining gates

The normal suite still passes **223 scenarios / 164,445 assertions**. The
existing diagnostic CPU executables pass **7,925 application / 192 platform
checks**; **39 Python tests**, pure identity tests and PE/source checks pass.
The unchanged diagnostic EFI SHA256 is
`18efed6c4a60ce0ae682fb9c9d165f425c280a2159e32f57124f8cf3c41703f9`.

`port/tests/review_method_followup.c` separately reproduces F3/F4. Its zero exit
status means the gaps were reproduced, not a safety qualification. Convert it
to rejection regressions when correcting them. The [checklist](windows-msi-ge78hx-launch-checklist.md)
reopens T1 for raw single-use enforcement and adds stock-path lifecycle closure.
Native firmware phases, DMA ledger, timing, exclusion and terminal behavior
remain unqualified. No new hardware trial is justified by these CPU passes.

CPU-only reproduction from the repository root:

```powershell
pwsh -NoProfile -File windows/ad103m-msi-ge78hx/port/Build-Offline.ps1
if ($LASTEXITCODE -ne 0) { throw 'Normal suite failed' }
$reviewMethod = Get-Content windows/ad103m-msi-ge78hx/port/build/compile.cmd -Raw
$reviewMethod = $reviewMethod.Replace('tests\host_tests.c','tests\review_method_followup.c').Replace('msi-offline-tests.exe','review-method-followup.exe').Replace('build\validation.log','build\review-method-followup.log')
Set-Content windows/ad103m-msi-ge78hx/port/build/compile-method-review.cmd -Value $reviewMethod -Encoding ascii
& $env:ComSpec /d /c windows\ad103m-msi-ge78hx\port\build\compile-method-review.cmd
if ($LASTEXITCODE -ne 0) { throw 'Review probe failed' }
Get-Content windows/ad103m-msi-ge78hx/port/build/review-method-followup.log -Tail 3
```
