# MSI offline port review — 2026-10-27

Reviewed implementation: `deb342e`, host-only capture-3/4 prototype. No native
adapter should be prepared by directly translating its callbacks yet. Four
counterexamples were reproduced on the CPU; no GPU query, EFI execution, USB
update, unlock or boot-policy change was performed. This review changes no
production behavior and fixes none of the findings below.

## Reproduced findings

### R1 — P1: post-transaction PCI state becomes the reset reference

`port/src/reset.c:7` saves configuration after the transaction; its small identity
and FLR checks do not compare this state to the validated preflight reference.
Lines 38–40 then restore its control values. `msi_post_reset` checks selected
fields but does not check the complete approved control state.

Reproduction: the mock transaction toggles bit `0x20` of PCIe Device Control
(MPS). Reset restores that changed bit and Windows handoff is invoked with it
still changed. The existing tests reject a pre-transaction BAR drift but miss
this later drift. An unapproved control setting can therefore survive FLR and
reach the next OS. A changed BAR could also be restored and decoding enabled
before the later pinned-BAR rejection; detection at that point is too late.

Required correction: retain a private immutable preflight reference, compare
post-transaction state before destructive reset/restoration, and restore only
approved writable fields. Define any tolerated status changes explicitly rather
than replaying read-only/W1C fields. Test each control and BAR drift at transaction,
reset-ready, restoration and final-handoff boundaries; ensure rejected BARs are
never enabled, even briefly.

### R2 — P1: containment can become stale during failure cleanup

`port/src/session.c:99` sets `containment_verified` after clearing BME, then line
102 invokes `close_log` without another check. The successful path already has
fresh validation after closing the log; the failure path does not.

Reproduction: after a failed transaction, a mock log-close callback models a
late event that enables BME. The function returns `MSI_STOPPED` with DMA retained,
`containment_verified=true`, and BME actually enabled. This is a callback/event
fault model, not a claim that USB Close necessarily enables GPU bus mastering.

Required correction: invalidate stale containment evidence before any remaining
external callback, perform the final containment check after cleanup, and enter
a terminal failure policy that cannot return to firmware and reclaim retained
resources. The native policy must account for subsequent asynchronous events;
a single successful BME readback is not a permanent guarantee. Test partial or
failed log close, BME drift during cleanup, failed containment, and that no EFI
entry-point return, reconnect, free/unmap or automatic OS fallback follows.

### R3 — P2: hard-coded capability offsets are not validated

`port/src/profile.c:24` interprets offsets from `port.h:20` as PM, MSI and PCIe
capabilities without validating their IDs, layout/version or capability chain.
Header type is not pinned either.

Reproduction: change the PCIe capability ID at `0x78` from `0x10` to `0x11`,
leaving the fields currently tested unchanged: `msi_profile` still accepts it.
A native implementation could interpret the wrong register as FLR control.

Required correction: validate the type-0 header, status/pointer, bounded aligned
capability chain, expected IDs/versions and control layout before any write.
Test changed IDs/pointers, loops, duplicates, missing/truncated capabilities,
unavailable reads and unexpected interrupt capabilities. The pinned ROM hash
does not validate the PCI capability snapshot.

### R4 — P2: staging has no reentrancy latch

`port/src/session.c:55` calls `stage_windows` while the state is still `MSI_NEW`.
A reentrant call on the same session passes the entry guard at line 52.

Reproduction: the staging callback reenters with a missing transaction callback;
the nested call changes the active session to `MSI_REFUSED`. The already-tested
reuse after completion does not cover reuse while staging.

Required correction: consume the entry state before the first external callback,
and copy/pin the session inputs needed across callbacks. Add recursive-entry
faults at staging and every later callback. Each nested entry must be inert,
without replacing the outer state or duplicating stage, close, reset or handoff.

## Adapter contracts still missing, distinct from the four reproduced bugs

- **DMA lifetime and terminal stop:** `dma_live` is a transaction-supplied number
  and `release_dma` returns one boolean. There is no ledger of buffer addresses,
  sizes, mappings, partial allocation/unmap/free failures or pending PCI I/O
  attributes. The C orchestrator returns after mutation; a future EFI wrapper
  must explicitly prohibit ordinary entry-point return while resources remain
  unsafe. Test persistent retention and partial cleanup, not merely a counter.
- **Protocol attributes and ownership:** `exclusive` and three hashes are mock
  assertions. They do not check the actual PCI I/O interface, attributes versus
  config command, mapping descriptors, driver identity, BAR logical indices,
  callback targets, TPL/event behavior or all unresolved boundary owners. These
  checks need explicit adapter contracts and independently reviewed snapshots.
- **Time and transport faults:** `stall` has no status or elapsed-time evidence.
  The mock advances virtual time unconditionally. Failed/short stalls, transient
  config/MMIO transport failures, silently ignored writes and multiple simultaneous
  cleanup faults are not systematically covered. Quiet time must remain a
  verified property when the FLR write or the timing service fails. Bounding a
  polling loop cannot bound a hung external firmware callback.
- **Windows image and console:** boolean review flags and `stage_windows=true`
  do not pin an image handle, device path, origin/hash or load options. No staged
  image cleanup/unload API exists. `silence_console` cannot report a partial
  mutation or restore it. Console callbacks and image-loading side effects need
  specific tests, including errors before the first driver disconnect.
- **Reset without unlock:** the current postconditions assume synthetic unlocked
  selectors. A native release/reset/Windows smoke test with stock state requires
  its own mode and postconditions; the current success mock is not that test.
  No signature acceptance, actual FLR persistence, driver rebind, display recovery
  or Windows boot has been demonstrated.
- **Validation provenance:** the build manifest hashes selected C files and
  fixtures, but omits imported analyzer/comparator modules, headers and toolchain
  identity. Expand the dependency manifest before treating it as reproducible
  native build evidence. Fixture hashes establish byte identity, not firmware
  signature acceptance or complete hardware qualification.

## Executed verification and reproduction

Baseline: **5,789 assertions, 43 workflow scenarios**, plus **39 Python tests**
passed again. These passing tests coexist with all four reproduced defects.
Assertion count includes repeated mock-access checks; it is not a count of
independent safety requirements.

`port/tests/review_counterexamples.c` deliberately asserts the current defective
outcomes. It is excluded from the normal passing suite and must be replaced by
rejection regressions when the defects are corrected. A zero exit status from
this audit executable means the defects were reproduced, not that they are safe.

To reproduce on the CPU from the repository root (PowerShell):

```powershell
pwsh -NoProfile -File windows/ad103m-msi-ge78hx/port/Build-Offline.ps1
if ($LASTEXITCODE -ne 0) { throw 'Baseline failed' }
$reviewCmd = Get-Content windows/ad103m-msi-ge78hx/port/build/compile.cmd -Raw
$reviewCmd = $reviewCmd.Replace('tests\host_tests.c','tests\review_counterexamples.c').Replace('msi-offline-tests.exe','review-counterexamples.exe').Replace('build\validation.log','build\review-counterexamples.log')
Set-Content windows/ad103m-msi-ge78hx/port/build/compile-review.cmd -Value $reviewCmd -Encoding ascii
& $env:ComSpec /d /c windows\ad103m-msi-ge78hx\port\build\compile-review.cmd
if ($LASTEXITCODE -ne 0) { throw 'Counterexample run failed' }
Get-Content windows/ad103m-msi-ge78hx/port/build/review-counterexamples.log -Tail 6
python -m unittest discover -s windows/ad103m-msi-ge78hx/diagnostic/tests -p 'test_*.py'
```

The generated review command keeps host-only compilation guards and does not
update the normal validation manifest. All executables and ROM fixtures stay
under ignored `port/build/`. No native backend or bootable artifact is produced.
