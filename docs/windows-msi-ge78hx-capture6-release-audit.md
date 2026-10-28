# Capture 6: storage and callback audit before a release-only test

2026-10-28. Offline analysis of the archived capture and its own manifest.
**A GPU release test is not ready yet.** The capture establishes useful
identities and topology, but not the runtime callback state or a native safe
release/handoff implementation. No GPU query, EFI execution, unlock, reset,
USB update or protection change was made for this audit.

## Evidence and method

Input is the preserved 0.4.4 CAP0006, SHA256
`1ee966fb7f30b6009639d4629d975ebefd13c6c9be293e4f5c6980b0d133d6c7`.
Its own manifest/build ID and normal analyzer/callback checks pass. Scope
selection follows device-path ancestors and reverse BY_CHILD_CONTROLLER edges;
it does not include every sibling device of a shared PCI/USB controller.
Five CPU-only tests cover sibling exclusion, a pathless parent, missing agent,
duplicate handle and the actual private capture.

Five captured executable sections were SHA256 checked, wrapped as inert COFF
data objects, and disassembled using the installed Microsoft dumpbin 14.44.35228
(valid Authenticode signature). No captured code was executed or linked as EFI.
The actual pinned EDK2 headers were compiled in a tiny CPU program to confirm
ABI offsets used in interpreting indirect calls. Private bytes/disassembly stay
under ignored `build/audits/capture6-dependencies/`; public provenance is in
[the evidence manifest](evidence/msi-capture6-release-audit/manifest.json).
This is bounded static inspection, not a whole-driver correctness proof.

## Storage: separate branch, shared parent

The observed source path gives this chain (all numbers are snapshot-local IDs,
never an admission identity):

    PCI root 139
      +-- PCI(0x01,0) -> NVIDIA GPU 377 -> GOP 408
      +-- PCI(0x14,0), controller 384 -> USB device 414 -> partition 415

The log and filesystem are on volume 415. The protected storage scope is
139/384/414/415, with **13 active ownership edges and seven agents**. The GPU
and USB have different branches but share the root-bridge owner. Targeting the
root/bus driver would be broader than GPU release.

| Agent | Observed protocol role | Evidence still missing |
| --- | --- | --- |
| 345 | root-bridge owner; children include both GPU and USB host | identity/code captured; preserve its root/USB relationships |
| 90 | PCI I/O + device path on USB host 384 | DriverBinding/image/Stop provenance |
| 92 | USB2 host-controller owner and USB child relationships | DriverBinding/image/Stop provenance |
| 94 | USB I/O consumer on storage device 414 | DriverBinding/image/Stop provenance |
| 327 | Block I/O consumer on device and partition | DriverBinding/image/Stop provenance |
| 328 | Disk I/O/device-path consumer and partition child relationship | DriverBinding/image/Stop provenance |
| 354 | Disk I/O consumer on volume 415 | callback code captured, but DriverBinding/image relation and Stop not captured |

These role descriptions follow protocol GUIDs, not guessed driver names.
The filesystem/file callbacks map to image 354, whose FV file GUID matches
the official EDK2 [Fat module definition](https://github.com/tianocore/edk2/blob/7af9d049380a791f843458d0c85157e3f0d9f4f6/FatPkg/EnhancedFatDxe/Fat.inf).
That supports a FAT-family identification; it does not prove byte-equivalence
to that upstream revision or validate this image's DriverBinding state.

The proposed display-stop agents are 321/326/407, disjoint from these seven
storage agents. This is evidence for a *narrowly targeted* design, not proof
that it is safe. Open lists omit non-owning accesses and hidden data/callback
dependencies. Six storage-agent implementations remain unverified if the design
relies on USB I/O during/after release. Closing all owned log/filesystem objects
before the first detach and then avoiding their callbacks reduces that scope;
it does not by itself prove absence of queued/background work.
The root-bridge relationship to the GPU (139/345 -> 377) must also remain intact
as a separate protected GPU boundary edge. Its exclusion from the 13-edge USB
subset is not permission to remove it; the existing rooted boundary also
preserves the keyboard and pointer relationships to the virtual console.

## Console: preserved input shares the output-driver image

All ConOut/StdErr methods and both sampled ConIn methods map to image 320.
The output driver to stop is agent 321, while keyboard/pointer consumers to
preserve are agents 322/323 in **that same image**. The input source paths are
on a different ACPI branch behind PCI(0x1f,0), not the Kingston USB path. This
does not establish independent usable input after a stop.

Therefore unload-by-image is unsuitable: stopping an exact DriverBinding child
relationship must not unload the image or remove the other instances. An
unchanged code hash does not establish the instance globals or child lists.

Concrete observations in the saved image-320 code:

| Entry/callsite RVA | Observation | Remaining dependency |
| --- | --- | --- |
| OutputString 0x4218; call 0x4395 | walks a list rooted at image RVA 0x5768 and calls a child method at interface +8 | list entries, live child interfaces, flags and buffer pointers are not captured |
| Reset 0x4188; call 0x41d1 | traverses the same list and invokes a child method | wrapper identity is not the child's implementation/ownership proof |
| ReadKeyStroke 0x32d4 | reads/modifies globals at 0x55d8/0x55ec and calls internal helpers | input state, event context and lower input interfaces are unknown |
| output Stop 0x1008 | branches on child count; zero children returns success, one-child path checks the expected child and edits lists/closes protocols/frees storage | a successful Stop return alone cannot prove the intended edges vanished |

The sampled executable section ends at 0x4f60. The referenced mutable state is
outside it; code-only capture intentionally does not supply those bytes. The
ABI check confirms +8 is SimpleTextOut.OutputString; the child pointer's actual
target remains unknown. The wrapper can also touch backing buffers before its
empty-list path, so retaining the wrapper alone is not a proof of usable output.

WaitForKey event identity/context, SimpleTextInputEx, SimplePointer callbacks
and pending notifications are not included in the 24 entries. None is silently
treated as quiesced. A future design may avoid application input/output after
the first detach, but must still address firmware/Windows image callbacks.

## USB log: synchronous entry does not mean a callback-free boundary

The 24 entries reduce to **15 distinct function addresses** (ConOut and StdErr
share nine). Four entries are in image 354:

| Function RVA | Static observation |
| --- | --- |
| OpenVolume 0x1e54 | helper calls and an indirect service call at 0x1ed2 |
| Write 0x28d4 | synchronous thunk passes a null async token to common routine 0x264c |
| Flush 0x1ffc | thunk clears the token then jumps to 0x1ee4 |
| Close 0x2004 | helper/cleanup calls followed by indirect service at 0x2058 |

Initialization at 0x0362/0x0380 stores the system table's BootServices pointer
at image RVA 0x6310. Helper 0x0ebc calls its +0x18 service, and calls at
0x1f8d/0x2058 use +0x20: the checked ABI identifies RaiseTPL/RestoreTPL. The
actual TPL argument and saved state are in uncaptured data. Inference is based
on static initialization/ABI, not a dump of the live service-table global.

The same image also contains an optional event-creation path: 0x0e15 selects
notify routine 0x0cb8, 0x0e34 loads TPL_NOTIFY (16), and 0x0e5a calls CreateEvent
with type 0x200. **This does not show that synchronous log Write/Flush created
that event**, nor that it is pending: those entry thunks pass null tokens.
Conversely, no event inventory or live queue state proves it absent.

The [official FAT reference](https://github.com/tianocore/edk2/blob/7af9d049380a791f843458d0c85157e3f0d9f4f6/FatPkg/EnhancedFatDxe/Flush.c)
also uses locking and cleanup around flush. It is contextual evidence, not the
source of the MSI binary. Per the already reviewed [UEFI TPL rules](windows-msi-ge78hx-uefi-handoff-timing.md),
lowering TPL can dispatch queued notifications. Log close/flush must therefore
finish before the final GPU exclusion interval, or its full callback path must
be explicitly qualified. Adding one last GPU read after Close is not exclusion.

## Actual Stop paths still need qualification

GraphicsConsole agent 326's Stop at image-325 RVA 0x0b5c calls protocol services
and frees instance storage. NVIDIA agent 407's Stop at image-407 RVA 0x06ec
dispatches to different routines for zero/nonzero child counts (0x209c/0x148c).
This inspection does not prove their transitive GPU I/O, event cleanup or DMA
effects. A test with no *application-issued FLR* must not be described as a
guarantee that driver Stop performs no internal hardware reset/write.

The repository's AD104 implementation supplies the intended order: targeted
physical TextOut consumer, GraphicsConsole, then NVIDIA GOP; it verifies graph
changes between stages. Its board/code profiles and hardware result are not
MSI admission evidence and its raised-TPL intervals do not close our T7 gap.
The native MSI foundation currently performs read-only admission only.

## What blocks Stage B, specifically

| Before a release-only binary/test | Missing concrete work |
| --- | --- |
| Protect storage/input while detaching only GPU consumers | semantic native pins and preservation checks for these 13 storage edges, input instances and shared image; no root/bus stop or image unload |
| Establish post-Stop callback lifetime | bounded read-only evidence for relevant globals/child interfaces/event contexts or an independently reviewed source/behavior argument; do not patch guessed globals |
| Choose callback/exclusion ordering | preload/pin Windows; complete log/FS work before detach where possible; legal per-service TPL and a source-specific strategy across Stop, claim/release and StartImage |
| Implement native release-only path | exact targeted agent/controller calls; verify edge changes and PCI/attribute state; OpenProtocol(EXCLUSIVE) may itself disconnect a remaining driver, so verify the graph before using it |
| Define stock success and failure | separate no-payload/no-application-FLR mode, reviewed handling of stock SS0 variations, no unlocked-selector expectations, single cleanup owner and nonreturning terminal behavior after uncertain partial detach |
| Make the test reviewable/recoverable | exact binary approval, independent stock boot/recovery access, saved work/idle GPU/AC/current protections, observable last stage, actual Windows display/input/driver recovery criteria |

Early targeted TextOut-only, then cumulative graphics/NVIDIA-release trials can
be designed independently; each is a separately reviewed GPU/platform mutation.
No automatic reconnect, retry, implicit EFI return or emergency FLR is justified
by this snapshot. The failure policy must cover Stop partially succeeding and
then failing; two GPUs and Stage A success do not prove recovery from that state.

The FWSEC/payload transaction, unlocked PLMs and post-FLR checks are later C/D
work, **not prerequisites to manufacture an unlock for Stage B**. There is no
reason to flash VBIOS or run a payload to answer the remaining ownership questions.
Another identical broad capture would not supply the missing live contexts or
qualify Stop automatically. First define the bounded missing evidence and native
ordering/failure contract; only then prepare a new diagnostic or active test.

The minimum next evidence should be scoped, rather than a whole firmware dump:

- If live console state is needed, pin image 320's exact code identity first,
  then review only the in-image state referenced by the inspected methods:
  mode/buffer slots near 0x5344/0x5638–0x5658, input flags/counters near 0x55c0,
  expected child at 0x55f0, and list-head slots 0x5728/0x5760/0x5768/0x5788.
  These are audit leads, **not permission to patch or blindly dereference them**.
  Each offset/width needs a separately checked structure and image bound;
  heap child lists require bounded readable-memory and lifetime validation.
- Identify only relevant registered lower interfaces and event/context origins
  for the chosen release ordering. WaitForKey and any callback relied upon by
  Windows/firmware must be accounted for; pointer equality alone is insufficient.
- Decide whether any USB callback is actually needed after the first detach.
  If none, close/flush/stage earlier and define the protected graph and pending
  work policy. If yes, the missing lower storage implementations become part of
  the critical callback review. A new snapshot alone cannot prove future absence
  of notifications or substitute for the native ordering contract.

## Reproduction and unchanged artifacts

Run from the repository root, on archived files only:

```powershell
python -m unittest discover -s windows/ad103m-msi-ge78hx/analysis -p 'test_*.py'
python windows/ad103m-msi-ge78hx/analysis/release_dependencies.py windows/ad103m-msi-ge78hx/captures/2026-10-28-run6/CAP0006.JSON windows/ad103m-msi-ge78hx/captures/2026-10-28-run6/MANIFEST.json
```

The optional `disassemble_callbacks.py` takes those same inputs and a new local
output directory. It validates code digests and uses dumpbin on data objects.
Both analysis tools keep release/ownership/quiescence permission false. No
native sources, EFI binary, sealed packet, USB files or admission masks were
modified by this audit. Test count here is five new analysis tests; earlier
full native/host suites are not claimed rerun for a report-only review.
