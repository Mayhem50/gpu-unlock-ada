# MSI offline port review — 2026-10-27

Original reviewed implementation: `deb342e`, host-only capture-3/4 prototype.
The four findings below describe that original revision; they have since been
corrected in the offline core and converted to normal rejection/containment
regressions. See the remediation section below. The remaining adapter contracts
are still unresolved. No GPU query, EFI execution, USB update, unlock or
boot-policy change was performed during review or correction.

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

## Original verification and reproduction (historical)

Baseline: **5,789 assertions, 43 workflow scenarios**, plus **39 Python tests**
passed again. These passing tests coexist with all four reproduced defects.
Assertion count includes repeated mock-access checks; it is not a count of
independent safety requirements.

At review commit `760026d`, `port/tests/review_counterexamples.c` deliberately
asserted the defective outcomes. That file has now been removed and replaced
by integrated rejection regressions in `port/tests/regression_tests.h`.

## Remediation and current offline validation

- **R1:** privately copy the preflight observation before the first callback;
  compare all configuration DWORDs after disabling BME and before MMIO readback
  or FLR. Reset copies this approved reference and refuses drift before its FLR
  write. Verify the restored configuration with decode off, then again before
  handoff. No status/W1C registers are replayed. Tests cover control/BAR drift,
  mutations of the caller's copy and silent restoration failures. Invalid BAR
  restoration never reaches the decode-enable write.
- **R2:** invalidate the containment flag before failure cleanup, attempt prompt
  BME clearing, close the log once, and establish the final containment evidence
  after the last log callback. Late BME changes are cleared/read back; unreadable
  final state leaves `containment_verified=false`. Failure after mutation always
  sets `terminal_required=true`; unsafe DMA remains retained. Log-close errors
  are recorded. The host function still returns a result to the test harness;
  there is deliberately no native terminal policy or EFI adapter here.
- **R3:** require the captured type-0/multifunction header, capability-list status,
  initial pointer, four exact headers/layouts/versions and terminating pointer.
  The chain walk is bounded and validates pointers before indexing. Regressions
  mutate every bit of the four capability headers and initial pointer, plus the
  PCI header/status. Unexpected capabilities, loops and ID changes are refused.
- **R4:** consume `MSI_NEW` before any external callback. Copy callback operations
  and qualification flags as well as the observation. Reentrant calls cannot
  change the active session. Tests attempt nested entry across mock operations,
  including successful flow, decode restoration, staging failure, transaction
  failure, cleanup and skip-to-Windows.

The full port suite now passes **26,974 assertions / 76 workflow scenarios**,
including all converted regressions, and both negative host/UEFI compilation
guards. The unchanged diagnostic CPU suites pass **7,925 application checks**
and **192 platform checks**. **39 Python tests**, pure PowerShell identity tests
and PE/source checks pass. Failed toolchain builds also invalidate a previous
passing manifest. No diagnostic EFI rebuild is needed for these corrections.

The existing diagnostic EFI SHA256 is unchanged:
`18efed6c4a60ce0ae682fb9c9d165f425c280a2159e32f57124f8cf3c41703f9`.

The reset reference comparison is deliberately strict, including status fields.
A real FLR clearing status bits could cause refusal; no broader hardware
admission is inferred from these tests. The adapter contracts listed earlier,
especially DMA lifetime, terminal failure handling, asynchronous firmware
callbacks, timing failures and stock reset postconditions, remain prerequisites.
The candidate stays unqualified, non-bootable and without a payload/backend.

To run the integrated regressions on the CPU from the repository root:

```powershell
pwsh -NoProfile -File windows/ad103m-msi-ge78hx/port/Build-Offline.ps1
if ($LASTEXITCODE -ne 0) { throw 'Baseline failed' }
python -m unittest discover -s windows/ad103m-msi-ge78hx/diagnostic/tests -p 'test_*.py'
```

Regressions are included in the normal validation manifest, including the new
test header hash. Executables and ROM fixtures stay under ignored `port/build/`.
No native backend or bootable artifact is produced.
