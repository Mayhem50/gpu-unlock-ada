# MSI GE78HX: evidence and validation checklist

Updated 2026-10-28 after the host DMA/timing work and UEFI feasibility review. This is a gate register,
not an execution packet. Checked means the stated evidence exists, not that
the machine is qualified for unlock. Open items block the relevant next stage.
Current scope: source/CPU validation, host-tested read-only PCI adapter
foundation and a local diagnostic 0.4.3 build/packet. Stage A has an operator-reported successful return to Windows;
no assistant EFI launch, GPU query/mutation, unlock or boot-protection change.
Stage A does not detach/reset the GPU. The adapter foundation is not bootable.

See the [method alignment review](windows-msi-ge78hx-method-alignment.md).
The operator's [capture 5 review](windows-msi-ge78hx-capture5-review.md) now
records three resolved boundary owners, an incomplete passive-dependency
observation and a third SS0 value outside the admitted host profile. No
qualification or admission rule was relaxed after that capture.
A complete checklist is not a completed checklist or a guarantee of recovery.

External comparison: [nv-uefi-unlocker v0.2.0 audit](windows-msi-ge78hx-nv-uefi-v020-audit.md).
The ASUS success report does not close native gates. The pinned source drops
its exclusive claim before the transaction, ignores partial DMA cleanup errors,
permits repeated reset paths and performs reconnect/OS discovery after GPU checks.
It does not admit this MSI's 2717 ID. No external code was integrated.

## 0. Historical evidence and present-state limits

- [x] Observed target recorded: 10de:2717 / 1462:13cd / A1,
  VBIOS 95.03.2A.00.2A, driver 595.79, BIOS E17S1IMS.114.
  These are captured values, not a fresh hardware query.
- [x] User-executed captures 3/4 reviewed privately: probe 0.4.1/0.4.2,
  matching 1 MiB PROM SHA256
  24b6d455544615dd3185e6c42512c104add2322b8c10053f6384566729ce7076.
- [x] BAR config slots 0/1/3/5 distinguished from logical indices 0/1/2/3.
  Cached ROM is only a prefix and cannot replace physical PROM/FWSEC.
- [x] Five GPU-source edges and three GPU driver identities compared;
  physical GOP and console roles/variables matched. Only the two observed
  SS0 values are admitted.
- [ ] **H1:** establish cold-entry provenance/conditions for each future test;
  matching fields alone do not prove a complete cold power cycle.
- [ ] **H2:** refresh security state in a later authorized read-only session.
  Last user-reported Secure Boot was false and BitLocker protection suspended.
  Do not assert their current values or change either during this work.
  A db certificate list alone does not establish trust for the unsigned EFI.
- [ ] **H3:** confirm independent access to the recovery key, backups,
  stock Windows boot, recovery medium and a usable display/input path.
  Two GPUs alone do not prove MUX/display recovery. Never log the recovery key.

## 1. Offline prototype evidence

- [x] Separate non-bootable port directory, no native backend or payload.
- [x] Real capture/PROM fixtures checked locally; private binaries ignored.
- [x] Private PCI reference, bounded exact capability chain, semantic ownership
  stages and narrow missing-original-IO/MEM restoration.
- [x] One simulated transaction/FLR; BME-off checks; 100 ms quiet period even
  after failed FLR write; bounded polling and decode-off restoration.
- [x] R1–R4 and F1/F2 reproductions converted to normal regressions.
  Cleanup preparations retain DMA/claim; ordinary callbacks precede actual
  release; final compound commit revalidates state and lease at its action.
- [x] Partial console mutation tracked before callback; terminal result on failure,
  without GPU cleanup for console-only errors or unverified automatic rollback.
- [x] 499 CPU workflow scenarios / 243,622 assertions and 45 isolated DMA ledger
  cases; diagnostic CPU suites
  (7,925 application / 605 platform), 62 Python tests, identity and PE/source
  checks recorded passing. Counts include repeated checks, not independent requirements.
- [x] Negative host/UEFI compilation guards and failed-build manifest invalidation.
- [x] Previous diagnostic 0.4.2 archived and copy verified, SHA256
  18efed6c4a60ce0ae682fb9c9d165f425c280a2159e32f57124f8cf3c41703f9.
  The [new diagnostic packet](windows-msi-ge78hx-diagnostic-v043-packet.md)
  is built/validated locally only. USB Stage A is not changed.
- [ ] **O1:** complete dependency/toolchain provenance: imported analyzer/comparator
  modules, shared headers, compiler/linker versions and private input manifests.
  Selected-file hashes are not a complete build closure.

Repeat from the repository root using CPU-only commands:

    pwsh -NoProfile -File windows/ad103m-msi-ge78hx/port/Build-Offline.ps1
    pwsh -NoProfile -File windows/ad103m-msi-ge78hx/diagnostic/Test-Host.ps1
    python -m unittest discover -s windows/ad103m-msi-ge78hx/diagnostic/tests -p 'test_*.py'
    pwsh -NoProfile -File windows/ad103m-msi-ge78hx/diagnostic/tests/test_identity.ps1
    python windows/ad103m-msi-ge78hx/diagnostic/tools/verify.py

The PE verifier reads the existing diagnostic EFI; it does not execute it.
Linux test-full and the full AD104 transaction suite were not rerun as part of
this MSI model validation. Their scenario counts are not MSI coverage.

## 2. Exact profile — not qualified for a transaction

- [ ] **P1:** validate/pin MSI system firmware, Windows build/driver, framebuffer
  and memory geometry, power state and cold-entry provenance at native admission.
  Current C pins are only part of this; review booleans are not evidence.
- [ ] **P2:** resolve the three relevant external owners (root bridge, keyboard,
  pointer) and actual console interface/callback/code identities. Do not demand
  resolution of unrelated global edges or use raw handle IDs as identity.
  Incoming-boundary identity collection is prepared and built locally in 0.4.3
  (605 platform checks; 62 Python tests); no deployment/execution occurred.
  Existing captures still lack these identities;
  console callback and USB/logging dependencies remain open. See the
  [boundary collection preparation](windows-msi-ge78hx-gop-console-analysis.md#read-only-boundary-collection-preparation--2026-10-28).
  [Passive dependency capture](windows-msi-ge78hx-passive-dependencies.md) now
  prepares 24 console/filesystem/log callback entries, registered-image/code
  provenance and opened USB-log binding. CPU tests refuse missing/ambiguous
  origins; the new EFI is built locally, not executed. Existing captures still lack the
  evidence; storage ownership and notification exclusion remain unproved.
- [ ] **P3:** bind exclusive PCI I/O interface, protocol IO/MEM/BME attributes,
  logical BAR descriptors and hardware command consistently. Review any status
  exception independently instead of widening admission masks.
  First [adapter admission component](../windows/ad103m-msi-ge78hx/adapter/README.md):
  actual EDK2 ABI, read-only Supported/Get, command/attribute namespace agreement,
  BME-off and two complete stable config/location observations are host-tested
  (148 cases / 3,211 assertions). No discovery, exclusive claim, BAR resources or
  native execution exists; this does not close P3 or authorize mutation.
- [ ] **P4:** pin private Windows manager bytes, origin, ESP/device path,
  loaded-image handle/options and stock fallback. Read-only ESP discovery is
  not installation; never reuse AD104 partition GUID/hash values.
- [ ] **P5:** validate five AD102-family asset names/sizes/hashes against the
  repository's pinned generators/inputs and notices. Do not copy/publish binaries.

## 3. Repository transaction parity — before any adapter

- [x] **T1 (host, F3 corrected):** session owns lifecycle scheduling. A typed pre-reset-only
  step must retain DMA and cannot own reset/unwind; the private helper owns one
  FLR/restore and guarded final commit alone performs cleanup. Unknown/full-core
  boundaries and recursive/repeated private-helper entry are refused. Raw FLR
  writes now use a separate single-use backend gate before callbacks/effects,
  including alias encodings and failed first writes. Repaired counters/phases
  cannot rearm it. See [remediation](windows-msi-ge78hx-pre-flr-review.md).
  Native enforcement remains T6/T7.
- [ ] **T2:** model/test GFW, FWSEC/FRTS/signature-version/fuse selection,
  payload publication, SEC2 completion/HALT, stock-signature restoration and
  selector phases. Require both signature phases; no early metadata change or
  free while SEC2 can consume it. Parsing/hashing is not signature acceptance.
- [x] **T3 (host):** sample all four PLMs with independent PCI/chip accessibility proof.
  Match upstream semantics: WPR_CFG/WPR/FEAT have required values; FBPA must be
  accessible but its expected value is not an upstream pass/fail gate.
  Raw samples and transport/valid masks survive partial sampling failure.
- [x] **T4 (host readbacks):** independently check effective readout before FLR
  and afterward, repeating the pre-FLR proof after pending polling. All 27 rate
  bits and transport/sentinel errors have negative regressions. Actual ordered
  single SS0/SS1 updates and firmware phases remain open under T2, not inferred
  from final selector values or mock proofs.
- [ ] **T5:** reproduce post-FLR GFW completion/HALT and SEC2 reset release/scrub
  sequence with bounded errors; preserve WPR2 HI.VAL/readout checks. No guessed
  engine reset before FLR; do not leave SEC2 held in reset.
- [ ] **T6:** real DMA ledger for address/size limits, partial Map/Unmap/FreeBuffer
  failures, pending protocol attributes and complete retention. Keep every
  firmware-visible address/handle valid until cleanup is proven safe.
  **Host substep complete:** per-resource states, sizes/addresses/tokens,
  full returned mapping coverage, uncertain/partial effects, stop-on-cleanup-error,
  tombstones, pending/reentrant attempts and stock history are modeled/tested.
  See the [DMA model](../windows/ad103m-msi-ge78hx/port/README.md#per-resource-dma-model--2026-10-28).
  There is still no native service binding, real quiescence/addressability proof,
  protocol-attribute journal or firmware qualification. Actual file bounds/digests
  before unsafe copies remain P5; build-time verification does not establish them.
- [ ] **T7:** legal UEFI TPL/events/reentrancy rules for every operation.
  Our atomic epoch/commit is CPU-only and differs from the reference ordering.
  A token or blanket RaiseTPL does not prove FreeBuffer/CloseProtocol/StartImage
  indivisible. Refuse/redesign if the contract cannot be implemented safely.
  The [specification review](windows-msi-ge78hx-uefi-handoff-timing.md) rules out
  treating raised TPL alone as continuous exclusion across StartImage. Restoration
  may dispatch notifications; source-specific quiescence and service callbacks
  still require evidence. No native strategy or adapter is supplied.
- [ ] **T8:** failed/short timing service, pending/readiness timeouts, failed or
  ignored accesses, hangs and combined errors. **Host substep now covered:** status-returning
  delay, independent clock evidence and elapsed budgets; unproved quiet time
  permits no PCI/log/cleanup callback and refuses replay. The old void stall was
  replaced. Native clock calibration, hung calls and terminal handling remain open.
- [ ] **T9:** native terminal failure mechanism: no ordinary EFI return,
  reconnect, retry, implicit guard release or unverified free after mutation.
  terminal_required is a result flag, not that implementation.

## 4. Separate stock release/reset/Windows tests — no unlock

- [x] **A0 software only:** separate [Stage A stock handoff](../windows/ad103m-msi-ge78hx/stock-handoff/README.md)
  built with the real adapter under 59 fake-UEFI cases / 1,909 assertions,
  plus native PE/source/header verification. No GPU protocols, payload, reset,
  DMA or file-write route. Explicit W only; timeout aborts.
- [x] **A1 image pin:** operator admin ESP fingerprint matched exactly to an
  existing, valid Microsoft-signed component-store `bootmgfw_EX.efi`, version
  10.0.28000.376. The default Windows servicing copy differed; the runtime now
  pins only the actual ESP image. No ESP file was changed.
- [x] **A1 USB preparation:** Stage A copied to the identity-checked Kingston;
  destination SHA256 verified, prior diagnostic backed up and its SHA256 checked.
  No formatting, internal ESP write or boot-variable change. Local deployment
  journal records the exact artifact and backup; no firmware execution occurred.
- [ ] **A1 remaining:** confirm current BitLocker/recovery state before boot.
  Windows reports Secure Boot disabled in its registry view; the non-admin
  process could not read BitLocker status. No protection was changed. Staging
  still refuses an unconfirmed fingerprint on any future rebuild.
  Operator subsequently reported C: fully encrypted/unlocked with protection
  suspended on 2026-10-28. Independent recovery-key access was reported earlier;
  no secret is stored here. Recheck security/recovery before any later trial.
- [x] **A2 operator report, 2026-10-28:** after the manual Stage A instructions,
  the operator reported "tout est OK". This is a reported successful return,
  not an independently observed banner/W/Windows trace or driver check. No
  USB execution log exists for Stage A; no physical result is inferred from
  the build manifest's software-only fields.
  This is not the release/reset test below and cannot close T2/T5–T9 or V1–V4.

- [ ] **V1:** explicit stock/no-unlock mode and stock postconditions.
  Current synthetic unlocked-selector success flow is not this test.
- [x] **V0 (host, F4/F5 corrected):** clean scoped lifecycle checked after stock log
  close and guard acquisition/validation, and independently at stock commit.
  Dirty/unavailable state retains uncertain resources/guard and requires terminal
  handling, including combined faults. The [F5 remediation](windows-msi-ge78hx-f3-f4-review.md)
  records resource uncertainty before every stock early exit independently of
  lease/log validity. Repair/replay cannot erase retention. Stock recovery on
  hardware remains V1–V4, not proved by these CPU results.
- [ ] **V2:** independently validate release-only, reset and stock Windows
  handoff in staged tests. Reset is a GPU mutation; none is authorized here.
- [ ] **V3:** before future hardware trials: idle workloads, saved work, AC,
  exact reviewed source/binary/input hashes, operator/observer roles, external
  recovery path and explicit approval of the exact stage and binary.
- [ ] **V4:** actual Windows login, display/input, NVIDIA driver recovery and
  error checks. StartImage invocation, frozen frame or a pre-handoff line
  does not prove recovery.

## 5. Unlock and workload validation — later separate gate

- [ ] **W1:** after previous gates, independently review the exact one-shot
  transaction and approval. Preserve stock boot; no VBIOS flash, driver patch,
  guessed writes, automatic installer or persistent default entry.
- [ ] **W2:** same binaries, driver, target, GPU mode, clock/power policy and
  comparable background activity for cold stock/unlocked/stock A/B/A.
- [ ] **W3:** deterministic numerical checks, instruction/GEMM distributions,
  FP16/BF16 FP32 accumulation, applicable TF32/FP8 and unchanged controls.
  Report repetitions/mismatches; selectors/getters do not prove workload gain.
  The Linux RM getter is not a Windows validator.
- [ ] **W4:** power/clock/thermal telemetry, driver errors, extended stability and
  actual workload correctness. Never rerun against a driver-owned card.
  Qualify warm reboot, battery and dGPU-only mode separately from cold hybrid.
- [ ] **W5:** verified stock boot and separately checked boot-security state per
  the user's chosen policy at session closure. Redact identifiers/secrets while
  preserving numerical results and provenance.

Another board's success or a CPU pass cannot turn an open gate into approval.
