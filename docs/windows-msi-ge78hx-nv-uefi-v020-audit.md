# nv-uefi-unlocker v0.2.0: offline comparison with MSI GE78HX

Audit date: 2026-10-28. MSI reference: `ebe0bd9f1a629d7ff36f17f7bd8d38514b576e9e`.
Scope: source inspection and the existing MSI CPU suite only. No GPU queries,
UEFI execution/build, downloaded executable, asset generation, USB update,
boot change, or native adapter was performed/prepared.

## Conclusion

The ASUS report is relevant evidence of one successful portable-PC UEFI to
Windows path. It does **not** qualify this implementation for the MSI. The
tag contains several failure paths that contradict the retention, ownership
and single-reset requirements of our host model. Do not transplant the loader
or replace our final compound commit with its reconnect/chainload path.

At the audit reference commit our DMA inventory was aggregate bookkeeping.
The subsequent [host DMA model](../windows/ad103m-msi-ge78hx/port/README.md#per-resource-dma-model--2026-10-28)
adds per-resource states and partial-error tests. Our model still remains
incomplete: its atomic commit is a RAM mock and its pre-reset firmware step is synthetic.
This audit closes none of the native qualification gates T2/T5–T9 or V1–V4.

## Provenance and confidence

- Repository: [physicsdolphin/nv-uefi-unlocker](https://github.com/physicsdolphin/nv-uefi-unlocker).
- [Tag v0.2.0](https://github.com/physicsdolphin/nv-uefi-unlocker/tree/d82bb38c09709be40d106d9569740b457692f873)
  resolves to commit `d82bb38c09709be40d106d9569740b457692f873`.
- [Release](https://github.com/physicsdolphin/nv-uefi-unlocker/releases/tag/v0.2.0)
  published 2026-10-27 07:59:22 UTC. GitHub asset metadata records 215,552 bytes
  and SHA256 `6788aa5a799f8c931714aecd3e86e93a8e33525059edf82cdde00f603e99a321`
  for `BOOTX64.EFI`, matching the hash reported in [ASUS issue #99](https://github.com/JustAResearcher/gpu-unlock-ada/issues/99).
- That issue describes an ASUS TUF A15 FA507NV, `10de:28e0` / `1043:19dd`,
  driver 617.14, dGPU-only, successful Windows handoff and an absolute FP16
  measurement. Its matched stock measurement was still pending when inspected.
- Public text sources were obtained from the immutable commit into ignored
  `build/audits/nv-uefi-v0.2.0/`; Git blob IDs were independently checked.
  The adjacent [source manifest](evidence/nv-uefi-v020-audit/source-manifest.json)
  records Git blob IDs and local SHA256 values.
- This establishes which **source** was reviewed and a matching **reported
  release digest**. It is not a reproducible source-to-EFI build verification,
  binary audit, authenticity attestation, or independent repeat of the ASUS run.
  No release executable was downloaded. `loader/build.rs` also embeds a build
  timestamp; tag identity alone is insufficient for byte-for-byte provenance.

Line references below apply to the pinned commit, not current `main`. Findings
are source-level counterexamples; none was reproduced on physical firmware.

## Comparison by subsystem

| Subsystem | v0.2.0 implementation | MSI host model | Consequence |
| --- | --- | --- | --- |
| GOP | Enumerates and disconnects every GOP, then target PCI; logs protocol counts | Five target edges, three exact stages, boundary/console/log identities checked | Keep targeted release; do not copy global disconnect |
| Exclusive claim | Local `ScopedProtocol` closes before transaction continues | Exclusive graph state required through final commit preparation | Native claim lifetime must be explicit |
| DMA | Explicit buffers without Drop, but release failures ignored and no persistent failure inventory | Sticky uncertainty and no release before final commit | Preserve retention; implement per-resource native inventory later |
| Reset | Normal, experimental pre-FLR, and emergency FLR paths without shared single-use gate | Backend single-use gate plus one session reset owner | Do not introduce emergency retry |
| Windows | Reconnects, then searches volumes and loads OS after GPU mutation | Image staged before mutation; guarded final proof and transfer contract | Preserve pinning and prove legal native sequencing |
| Failure | Most transaction errors reset then continue toward OS; entry returns success | Terminal result and retained resources after uncertain mutation | Host terminal flag still needs a nonreturning native implementation |

## Findings blocking direct reuse

### A1 — P1: exclusive ownership is not held for the transaction

[`tx_detach.rs:113–162`](https://github.com/physicsdolphin/nv-uefi-unlocker/blob/d82bb38c09709be40d106d9569740b457692f873/loader/src/tx_detach.rs#L113)
opens an exclusive `ScopedProtocol` in `detach_and_claim`, but returns only a
`Vec<u32>`. The guard drops on function exit. The pinned `uefi` 0.41.0 dependency
[`ScopedProtocol::drop`](https://github.com/rust-osdev/uefi-rs/blob/973f08de315f7d8ade4ee9c0fdcfbc4fa9f8b43a/uefi/src/boot.rs#L1841)
calls `CloseProtocol`. The caller (`tx.rs:851`) discards even the returned config
baseline and continues using the earlier GetProtocol interface from `main.rs`.
No exclusive owner survives into FWSEC/booter/reset/cleanup.

MSI: `session.c` requires exclusive ownership snapshots until commit; a real
retained protocol handle/interface and close-failure ledger remain T6/T7 work.

### A2 — P1: GOP release is global and verification is not an open-edge proof

[`tx_detach.rs:18–23,76–112`](https://github.com/physicsdolphin/nv-uefi-unlocker/blob/d82bb38c09709be40d106d9569740b457692f873/loader/src/tx_detach.rs#L18)
uses `protocols_per_handle` to count installed protocol GUIDs, not open-protocol
agent/controller edges. Disconnect status/counts are logged but not acceptance
conditions. Every GOP is disconnected without binding it to the chosen GPU.
On a hybrid system this can affect another GPU/console. Neither a count change
nor the later temporary exclusive open proves the required release graph.

The logger continues `with_stdout` for every message (`logging.rs:51–55`),
including inside disconnect/reset paths. There is no explicit console-silencing
phase or demonstrated callback exclusion in this path.

MSI: retain the exact graph method in `ownership.c`, console mutation tracking
and final callbacks checks. Actual callback identities/external owners remain P2.

### A3 — P1: PCI I/O Attributes has the wrong ABI, operation and bit mask

[`pci.rs:104–105,186–196`](https://github.com/physicsdolphin/nv-uefi-unlocker/blob/d82bb38c09709be40d106d9569740b457692f873/loader/src/pci.rs#L104)
declares five arguments, treats a fifth output as a status, passes operation 1
as Enable and receives mask `0x0007` from `tx_detach.rs:141`.

The [EDK2 definition](https://github.com/tianocore/edk2/blob/7835e5802a2a70984333c054002e449bfd1f0a0d/MdePkg/Include/Protocol/PciIo.h#L435)
has four arguments: `(This, Operation, Attributes, UINT64 *Result)`, with status
as the return value. Get/Set/Enable are 0/1/2, respectively. Protocol IO/MEM/BME
are `0x100/0x200/0x400`, not command-register `1/2/4`. The actual return is
ignored while the local fifth-argument status remains initialized to SUCCESS.
The raw COMMAND fallback may hide the failed/misdirected protocol call.
Exact effects of the invalid declaration are firmware/ABI dependent; no effect
or memory-corruption outcome was measured here.

MSI: P3 explicitly requires protocol attributes and PCI command to be distinct.
No native binding exists yet; do not copy this wrapper.

### A4 — P1: failed Unmap still leads to FreeBuffer and a false released state

[`pci.rs:377–390`](https://github.com/physicsdolphin/nv-uefi-unlocker/blob/d82bb38c09709be40d106d9569740b457692f873/loader/src/pci.rs#L377)
sets `released=true` before attempting Unmap and FreeBuffer. FreeBuffer is called
even when Unmap fails. A second release then returns true without retrying or
reporting unresolved ownership. Callers discard the first result, including
`tx.rs:1224–1231`, and announce all DMA released.

Counterexample: Unmap fails, FreeBuffer succeeds → mapping uncertainty plus
freed backing memory, then successful transaction return and OS chainload.
FreeBuffer failure likewise leaves an allocation while the object says released.

MSI at the audit reference: mock aggregate release only. The subsequent host
model records individual partial Unmap/FreeBuffer outcomes, stops on failure and
retains uncertain records. It still does not test real firmware services or prove
the final compound action implementable. T6 remains open; no native safety claim.

### A5 — P1: early DMA failures lose bookkeeping before emergency handoff

`DmaBuffer` has no Drop cleanup (intentional, `pci.rs:358–370`). After allocation,
many `?` exits in `tx.rs:950–1203` and `tx_fwsec.rs:476–587` return without a
session-owned list of the remaining allocations/mappings. This is not an
immediate RAII free: buffers remain allocated, but their records disappear.
The caller takes the emergency FLR/OS path without a clean scoped inventory.
Lost BootServicesData cannot be treated as permanently reserved after OS takeover.

`dma_alloc` (`pci.rs:278–334`) also does not verify the returned mapped byte
count covers the requested range, and ignores failure of the rollback FreeBuffer
when Map fails. Whether shortened common-buffer mapping can occur on a given
firmware is not established; admission must validate the returned contract.

MSI: lifecycle/readability checks prevent this transition in the CPU model.
Native address, length, mapping-token, allocation and cleanup state remain T6.

### A6 — P1: file lengths are not checked before unsafe DMA copies

[`tx.rs:754–798,948–986`](https://github.com/physicsdolphin/nv-uefi-unlocker/blob/d82bb38c09709be40d106d9569740b457692f873/loader/src/tx.rs#L754)
returns `Some(out)` after early EOF. A present V67 file is then copied using
the fixed 64,000-byte length without checking `v67.len()`. A truncated file
causes an out-of-bounds CPU read. Stock signature bytes are copied with their
file length into a fixed 4,096-byte allocation; oversized input overflows it.
The helper `booter_patch_v67` has a length check but is not used by this copy.
The inspected loader has no runtime asset digest gate protecting these copies.
Build-time maker verification does not protect against later USB truncation.

MSI: P5 asset qualification remains open; the synthetic `assets_verified` flag
does not implement runtime bounds/digests or make a future copy safe.

### A7 — P1: reset can proceed without proven pending-clear/readiness

[`tx_booter.rs:794–885`](https://github.com/physicsdolphin/nv-uefi-unlocker/blob/d82bb38c09709be40d106d9569740b457692f873/loader/src/tx_booter.rs#L794)
and `tx.rs:1259–1309` hardcode PCIe capability `0x78`. Pending loops treat
`0xffff` as a reason to break and do not reject exhaustion. Readiness loops
also fall through after exhaustion and accept any NVIDIA vendor identity.
Writes discard transport status. Saved config is taken after transaction changes,
not compared with an immutable preflight reference. Post-FLR GFW completion is
ignored; missing GSP HALT is logged and tolerated. The effective rate readout is
not an acceptance condition in `flr_phase`.

The low-level config/MMIO helpers (`pci.rs:127–170`) initialize output to zero,
log a failed status and still return the value. `TxGpu::read32` only rejects
sentinel values, so it cannot distinguish a failed read leaving zero from a
valid zero. In particular a failed WPR2_HI read can resemble the required
cleared state. Transport success must be checked separately from register bits.

In `flr_phase`, `info!("PCIe FLR triggered")` occurs before the 100 ms stall.
Logging invokes console/file callbacks, so the claimed quiet interval is not
protected from callback GPU access. A later 100 ms delay does not establish
that the earlier callback interval was quiet.

MSI: failure-aware reads/writes, strict identity/capabilities/config, pending
timeout refusal, immediate quiet period and readout checks already exist in
`reset.c`/`readbacks.c`. Native timing failures and full post-FLR engine sequence
remain T5/T8; a void simulated stall is not native timing proof.

### A8 — P1: no single reset owner; failed recovery does not stop OS transfer

[`main.rs:181–187`](https://github.com/physicsdolphin/nv-uefi-unlocker/blob/d82bb38c09709be40d106d9569740b457692f873/loader/src/main.rs#L181)
calls emergency FLR for generic transaction errors. If normal FLR already ran
and its postcheck fails, this issues another reset. An optional pre-FLR path
(`tx.rs:698–745`) adds another independent reset. There is no shared spent gate.
`emergency_flr` returns `void`; failure to open the device only logs and returns,
after which the caller still reaches handoff and OS chainload.

MSI: preserve the backend `MsiResetGate` and one scheduling owner. Uncertain
mutation requires terminal handling/retention, not an automatic recovery retry.
Native nonreturning containment is still T9, not implemented by our result flag.

### A9 — P1: callbacks and OS discovery follow the last GPU checks

[`tx.rs:1207–1232`](https://github.com/physicsdolphin/nv-uefi-unlocker/blob/d82bb38c09709be40d106d9569740b457692f873/loader/src/tx.rs#L1207)
reconnects the GPU before releasing DMA. `BridgeMaster` may also restore parent
bridge BME during Drop (`pci.rs:580–596`). `handoff_out` logs values without
enforcing them. Then
[`chainload.rs:53–161`](https://github.com/physicsdolphin/nv-uefi-unlocker/blob/d82bb38c09709be40d106d9569740b457692f873/loader/src/chainload.rs#L53)
recursively connects **all PCI handles**, scans filesystems and searches several
OS paths, including a generic fallback. No final ownership/quiescence/readout
proof covers those driver callbacks or the log close.

`start_loader` loads the OS only at this late stage (`chainload.rs:193–234`),
does not pin a reviewed ESP/image digest beforehand, logs the StartImage result
and returns `Ok(())` even for an error. The entry point eventually returns
`Status::SUCCESS` with a stale read-only/safe-reboot message even in tx builds.

MSI: keep staging/pinning before mutations and checks after ordinary callbacks.
The compound release/transfer contract has no demonstrated legal UEFI
implementation; do not assume RaiseTPL can make these services atomic. If the
contract is infeasible, redesign it explicitly under T7 rather than silently
weakening checks to match this implementation.

### A10 — P1: no affirmative timeout consent; this MSI ID is not admitted

`main.rs:40–65` proceeds after any key **or a 30-second timeout**, also in tx
builds. A present V67 file selects unlock. The comments promising timeout abort
are not implemented. A directory containing a payload is not our explicit
authorization for a reviewed hardware transaction.

`main.rs:106–117` maps `0x2757` as a 4090 Laptop but **not our `0x2717`**.
`pci::enumerate` picks the first NVIDIA function 0 without enforcing the
documented display class. No MSI subsystem/VBIOS/PROM profile is admitted here.
Do not infer that adding the missing ID makes the loader compatible.

## Useful mechanisms to study, without transplanting the failure policy

- Physical PROM extraction, FWSEC geometry/fuse-version selection and WPR2
  validation provide concrete implementation references beyond our parsed ROM.
- Normal Ada booter execution waits for SEC2 HALT (`tx_booter.rs:417–521`),
  checks marker and required permissions, verifies selector writes, restores
  stock signature metadata, and checks selector survival after FLR.
- BME is cleared for normal FLR and BAR restoration uses decode-off/high-before-low
  ordering. Those intentions agree with our method; timeout/status handling differs.
- The parent-bridge BME diagnosis is useful **evidence to inspect**, not authority
  to change the MSI root port. Its automatic Drop restoration and global handle
  enumeration are outside our currently authorized/simulated mutation scope.
- Firmware loading uses register portals as well as Falcon reset/start authority
  probes, scrubber handling and experimental marker switches. This is not simply
  the unchanged `unlock_610.c` wired into a UEFI wrapper. Missing V67 selects
  firmware replay, which still writes/resets the GPU and is **not read-only**.

The code also restricts FBPA to two values, while the Linux reference and our
model require accessibility without pinning its exact value. Do not narrow our
gate based on this port's observed values, or confuse its log wording "four PLMs
full" with four mandatory all-ones values.

## Tests and evidence limits

The external `sim` crate implements a separate transaction with an unconditional
simulated unwind (`sim/src/transaction.rs:325–369`). Its integer DMA counters
cannot verify the loader's `DmaBuffer::release`, ScopedProtocol lifetime,
Attributes ABI, failed firmware writes, global GOP operations or chainloader.
No Rust toolchain was present in this session; the external tests were not run.
Neither their published count nor our suite is evidence that those native paths
are covered. No third-party build script was executed.

Revalidated the unchanged MSI model with:

```powershell
pwsh -NoProfile -File windows/ad103m-msi-ge78hx/port/Build-Offline.ps1
```

Result: **410 workflow scenarios / 191,252 assertions PASS**, including the
negative firmware build guards. This validates our existing CPU model, not
the ASUS implementation or an MSI firmware adapter. The diagnostic EFI was
read/hash checked only and remains SHA256
`18efed6c4a60ce0ae682fb9c9d165f425c280a2159e32f57124f8cf3c41703f9`.

## Next offline work, before preparing a native adapter

1. Specify legal per-call UEFI TPL/event/reentry rules and the final transfer
   sequence. Decide whether the current abstract compound commit is implementable.
2. Per-buffer allocation/mapping and partial Map/Unmap/FreeBuffer outcomes are
   now covered in the subsequent host model, including invalid-lease combinations,
   pending calls and no retry/rearm. Real service binding, quiescence/exclusion
   and claim-close partial outcomes remain unqualified.
3. Add pre-copy asset length/digest admission and typed, bounded firmware phases
   (GFW, FWSEC, payload/HALT, metadata restoration, selector publication, post-FLR).
4. Specify and test failure-aware timing and a terminal policy independent of
   console/log availability. Preserve the exact target graph and single reset.
5. Only after independent review of these contracts, consider an adapter and
   a separately authorized stock/no-unlock staged hardware validation packet.

No code from the external loader has been integrated. No native gate is marked
complete on the strength of the ASUS success report.
