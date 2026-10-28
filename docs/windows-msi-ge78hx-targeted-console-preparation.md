# Targeted console/event evidence collection — offline preparation

2026-10-28. This change prepares diagnostic source and regression tests only.
No GPU access, EFI build/launch, unlock, Kingston access/update or protection
change occurred. The deployed 0.4.4 does not contain this new collector.

## Collected scope

The new `console_state` report supplements the existing platform graph and
callback records. Selection is semantic, never snapshot-local image number 320:

1. Require the exact FV file path, 24,256-byte BootServicesCode image, one
   non-relocated executable section at RVA 0x2a0 of 19,648 bytes and captured
   SHA256 `c14e5170fda8bd2529028c8248d7e6cf9518cbac498af7f69fd6cdc8e9929766`.
   Refuse duplicate matching images. At most four size/type candidates are held.
2. Bind the registered SimpleTextIn, ConOut and StdErr interfaces to the current
   system-table pointers. Require the exact sampled Reset/ReadKeyStroke RVAs and
   all nine output-method RVAs in that same pinned image. A matching unloaded or
   inactive lookalike is insufficient. Recheck bindings after the final service.
3. Read only the 22 fixed in-image slots justified by the capture-6 disassembly:
   mode/cursor raw state, input/pointer flags and counters, expected-child,
   buffer and list-head/anchor pointer values. Widths total **107 bytes**. No
   screen, text buffer, keystroke content, list node or pointed-to object is read.
4. Record registered input interfaces on the console input root: SimpleTextIn,
   SimpleTextInputEx and raw four-word views of SimplePointer/AbsolutePointer
   where present. No method on these interfaces is invoked.
   All four protocol statuses and pointers are queried again before the final
   memory map; replacement, removal or appearance makes the result incomplete.
   The JSON retains both sets of statuses/pointers for offline cross-checking.
   A successful query with a null interface is an explicit refusal, including
   for optional protocols and for the second query.

Slot meanings remain audit labels for raw bytes, not a certified driver
structure or a source-patching recipe. Null/zero is retained when read; an
unread slot is null, not invented zero data. First/second values, interface
statuses/addresses, candidate-filter counters and memory-map statuses/geometry
are recorded so refusals can be distinguished without relaxing the policy.

## RAM bounds and observation timing

The collector uses two static 64 KiB memory-map buffers, with no pool/page
allocation, allocation retry or firmware file activity. Each GetMemoryMap must
succeed exactly, have the supported descriptor version/stride and stay within
the buffer. Requested ranges require contiguous non-overlapping, page-aligned
BootServicesCode/Data descriptors with WB-only cache attributes and no read
protection. MMIO, conventional/reserved/runtime memory, gaps, overlap, malformed
sizes and arithmetic wrap refuse. No memory-map attribute is modified.

Metadata interfaces/LoadedImage records returned by firmware are still trusted
as readable metadata, as in the existing diagnostic. The map guards the newly
introduced image/state/protocol-table reads. It is not a sandbox against lying
firmware or a guarantee against concurrent platform mutation.

After gathering metadata, the first map bounds code/path verification and all
new source reads. Two volatile CPU copies record the slots and interface words.
The code digest is rechecked, a second map must match the first including key,
then active bindings, code and current slot/interface values are rechecked
without another firmware service call. Map/code/binding/state drift is retained
as incomplete. These checks detect observed drift; they are **not an atomic
snapshot, a lock or a proof of future lifetime**.
The second protocol query precedes the final GetMemoryMap; a callback in that
service could still change an interface afterward. Re-querying in a loop cannot
establish continuous exclusion. Event and release qualification remain open.
Accordingly `interfaces_current_known=false` is explicit and mandatory: saved
wait tokens describe the earlier observations, not current registrations.
The regression reproduces an interface replacement inside the last map call
while the old protocol memory and memory map remain unchanged.

Each collection context is single-use, consumed before service callbacks.
Recursive/repeated entry latches a violation and cannot produce a complete
result. Refusal never retries, follows a pointer, changes TPL or mutates source
memory. Source checking forbids event activation and RaiseTPL/RestoreTPL calls.

## Events: exposed tokens versus unavailable internals

The pinned existing EDK2 headers supply the SimpleTextIn and InputEx layouts.
Their WaitForKey/WaitForKeyEx values are recorded as **opaque public tokens**.
Tokens can be non-null while their notification routine, context, queue status
or signal state remains unknown. They are never dereferenced or passed to
CheckEvent, WaitForEvent, SignalEvent, SetTimer, CreateEvent or CloseEvent.
RegisterKeyNotify/UnregisterKeyNotify addresses are just raw protocol words;
neither is called. Missing InputEx is recorded, not fabricated as quiescent.

The current vendored package lacks the SimplePointer/AbsolutePointer headers.
Those interfaces therefore remain raw, bounded four-word views. The report
does **not** decode their third word as a verified event token or dereference
their mode pointers. A separately pinned ABI interpretation can be added
offline; no guessed function prototype is invoked in this preparation.

There is no portable public event-introspection mechanism in this implementation
to enumerate private notification contexts/queues. It intentionally emits
`event_contexts_known=false` and `pending_events_known=false`. Heap-list heads
are only saved values; following their contents would require separately
reviewed layout, lifetime, readable bounds and traversal budgets. No private
EFI_EVENT layout, firmware-global scan, event hook or driver patch is supplied.

Thus this evidence can reduce the audit gaps without pretending to settle T7.
Queued callbacks and survival of data after Stop still require a reviewed native
ordering/quiescence argument. The [release audit](windows-msi-ge78hx-capture6-release-audit.md)
and its proposal to finish owned USB/log work before the first detach remain
applicable. No release/reset adapter is enabled by the collector or its parser.

## Regression validation

`Test-Host.ps1` now includes the dedicated real-source collector harness. Its
synthetic profile is a separate compile-time choice, rejected by the normal
offline parser and incompatible with GU_UEFI_PORT. Missing or mixed profile
selection also fails compilation. The future native Build.ps1 selects the real
profile, but **was not run** in this preparation.

CPU results:

- **32 collector cases / 112 checks**, including exact profile/path/code rejection,
  duplicate image, map-buffer/stride/type/cache/read-protection failures,
  descriptor overlap/gap/wrap, reentry/replay, final map/code/state/binding drift,
  missing optional InputEx and opaque tokens deliberately equal to unreadable
  addresses. No token or captured method is executed.
- **75 Python tests**, including every missing/partial/mutated raw slot,
  incorrect wait cookie, pointer-layout overinterpretation, profile/flag drift,
  old captures without the field and rejection of actual synthetic C JSON.
- Existing **7,925 application / 722 platform** checks, pure identity and
  previous-PE/source checks pass under the CPU runner.

The parser distinguishes raw state observation from event knowledge and always
keeps release/quiescence permission false. Historical captures are not modified
or filled with synthesized new state. A report for a refusal may contain partial
bytes with explicit validity; it is not a usable full-state observation.
It independently compares map status/geometry/key/digest, candidate counts,
the verified dependency image and callback RVAs, input binding and the two
protocol query results. The regression fixture uses private capture 6 when
available and is explicitly skipped when that capture is absent; synthetic
samples do not authenticate a hardware capture.

## Artifact boundary

The local native 0.4.4 binary SHA256 remains
`a97ebe9678e0d786ec12fdf32885415d51ed62f63e3243504ab2b38b999328f2`.
The Kingston was neither read nor modified for this work. No 0.4.5 or other new
EFI/packet was built. The old sealed packet's source hashes are now historical;
current-source staging verification must refuse it until a separately versioned
and validated build is requested. No fresh boot or hardware action is requested.

Reproduce from the repository root:

```powershell
pwsh -NoProfile -File windows/ad103m-msi-ge78hx/diagnostic/Test-Host.ps1
```

This compiles and executes hosted mocks only. The supplementary PE verifier
reads the previous native EFI; it does not execute it or prove it contains the
new collector. Before any later capture, review the exact future build and
memory/layout/protocol assumptions rather than reusing the old artifact hash.
The enclosing diagnostic still has its existing bounded PCI/MMIO/PROM reads
at runtime; this source preparation adds only RAM/protocol-table observations.
It is not authorization to execute the enclosing diagnostic now.
