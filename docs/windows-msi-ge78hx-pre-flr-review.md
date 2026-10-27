# Review of FBPA/readout and lifecycle ownership changes

Original reviewed revision: `729b38f`, 2026-10-27. Findings below describe that
revision. F3/F4 are now corrected in the host model and converted to normal
regressions; see the remediation section. Source review, local reference comparison
and CPU mocks only. No GPU query, EFI execution, unlock, reboot, USB deployment
or boot-policy modification. The original review changed no core behavior;
subsequent remediation changes only host code and CPU mocks/tests.

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

## Original review evidence (historical)

The normal suite still passes **223 scenarios / 164,445 assertions**. The
existing diagnostic CPU executables pass **7,925 application / 192 platform
checks**; **39 Python tests**, pure identity tests and PE/source checks pass.
The unchanged diagnostic EFI SHA256 is
`18efed6c4a60ce0ae682fb9c9d165f425c280a2159e32f57124f8cf3c41703f9`.

At review commit `6e878b2`, `port/tests/review_method_followup.c` separately
reproduced F3/F4. That file is removed and replaced by integrated rejection
regressions in `port/tests/lifecycle_regressions.h`.

## Host remediation and current validation

**F3:** a backend-owned MsiResetGate is consumed before callbacks or the first
raw reset write. Its spent/violation flags are independent of lifecycle counters
and phase labels. Any repeat or unauthorized request is rejected before effects
and marks a sticky violation. All valid byte/word/dword encodings covering the
FLR bit use the gate. A failed first write, whether or not it had a modeled
effect, still spends the attempt and still receives the required quiet period.
No code path implicitly rearms the gate; only a fresh host test fixture creates
a fresh session. This is not an instruction for automatic hardware retry.

Regressions cover readiness, restoration, quiet time, recursive raw writes,
failed first writes with/without effects, aliases, and repaired phase/counters.
Each test observes at most one raw effect; rejected repeats do not even perform
a virtual config access. The normal orchestrator still spends its own helper
attempt, and the journal detects the recorded violation before cleanup/handoff.

**F4:** stock lifecycle is checked after log close and again after guard
acquisition/validation, even if acquisition or close failed. The stock compound
action independently checks the current scoped bookkeeping at its action point.
Dirty or unreadable state refuses transfer, sets dma_retained conservatively,
retains any acquired/uncertain guard and requires terminal handling. Entry-time
bookkeeping uncertainty is now terminal too. A stock cleanup failure cannot
be presented as a harmless ordinary-return refusal. No GPU query, reset,
detach, unmap/free or automatic repair is added to this path.

Regressions inject every counter, live buffer, sticky ownership flag, gate
history and unreadable ledger at log close, guard acquisition/validation and
commit entry. Failed partial guard acquisition and log close are covered.
Stock retries and commit replay after journal repair remain refused. A clean
stock flow still transfers without virtual PCI/MMIO accesses or cleanup.

Current result: **304 workflow scenarios / 190,413 assertions** pass, together
with the negative compilation gates, diagnostic CPU suites (7,925 application
and 192 platform checks), 39 Python tests, pure identity tests and PE/source
verification. The diagnostic EFI hash above is unchanged. Assertions include
repeated invariants, not independent safety requirements.

T1 and V0 are complete only as host contracts/regressions. Native firmware
phases, real DMA ledger, timing, exclusion and terminal behavior remain
unqualified under the [checklist](windows-msi-ge78hx-launch-checklist.md).
The backend must enforce the reset gate and stock predicate before effects;
these trusted contracts are not isolation against malicious C code. There is
no adapter, actual GPU operation or new hardware qualification here.

Run the integrated CPU suite from the repository root:

```powershell
pwsh -NoProfile -File windows/ad103m-msi-ge78hx/port/Build-Offline.ps1
if ($LASTEXITCODE -ne 0) { throw 'Normal suite failed' }
python -m unittest discover -s windows/ad103m-msi-ge78hx/diagnostic/tests -p 'test_*.py'
```
