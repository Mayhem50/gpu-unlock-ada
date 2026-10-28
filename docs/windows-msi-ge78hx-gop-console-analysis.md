# MSI GE78HX: GOP/console observations in CAP0003 and CAP0004

Analysis performed offline on the preserved captures and their own build
manifests. No GPU query, EFI execution or driver release was performed.
These are sequential, non-atomic observations; no transaction is authorized.

## What matches

Both captures contain 453 handles, 101 active ownership relationships and
one physical GOP belonging to the NVIDIA device. Counts alone were not used
as proof of identity. Device paths, verified loaded driver code and rooted
console relationships establish the following observations:

| Component | Comparison |
|---|---|
| Physical NVIDIA GOP and displayed mode | Match |
| ConOut and ConIn variable paths | Match |
| ConOut/ConIn/StdErr aliasing the same virtual console | Match |
| Three captured GPU-related driver code identities | Match |
| Five relationships whose source is GPU/GOP | Match |
| Three external relationships touching GPU/console | Unknown owner identities |
| Entire firmware ownership graph | Unknown |
| Virtual console callback implementation and release/recovery behavior | Not captured / not tested |

## The five GPU/GOP relationships

    NVIDIA PCI device
      +-- NVIDIA GOP driver opens PciIo BY_DRIVER
      +-- NVIDIA GOP driver opens DevicePath BY_CHILD_CONTROLLER -> physical GOP

    Physical NVIDIA GOP
      +-- TextOut driver opens SimpleTextOut BY_DRIVER
      +-- TextOut driver opens SimpleTextOut BY_CHILD_CONTROLLER -> virtual console
      +-- GraphicsConsole driver opens GOP BY_DRIVER

The three driver agents have no device paths, but each has a unique captured
image/file path, verified code digest, valid binding identity and Stop offset
inside the captured non-relocated code. The comparator uses those attributes
instead of firmware handle numbers. Reassignment to another driver, changed
code, duplicate identities, changed edge count or altered root aliasing cannot
silently match.

The shared virtual console also has no device path. Its observation can be
matched by the rooted ConOut/ConIn/StdErr roles and incoming relationships.
That does not identify its function pointers, code, globals or stop behavior.
The old generic topology_matches remains unknown when console handle paths
are unavailable, while the physical and rooted comparisons are explicit.

## Remaining ambiguities

Three boundary relationships have an identified source/controller but an
uncaptured, pathless agent:

- PCI root-bridge ownership leading to the NVIDIA PCI device.
- Keyboard SimpleTextIn relationship leading to the virtual console.
- Pointer relationship leading to that same console.

Snapshot-local agent IDs are 345, 322 and 323 in both captures, respectively.
Their equal numbers are not used as evidence that they identify the same
implementation. The current probe captured code only for the GPU-related
agents, so it cannot establish these three identities.

Globally, 75 handles have unique paths, 3 additional GPU agents are identified
by verified code/file metadata, and 1 virtual console is represented by its
root roles. There are 378 pathless handles in total, many without active
relationships. Sixteen unlabelled handles occur in active relationships:
8 global edges can be normalized, 93 remain unresolved. All five GPU-source
edges are among the normalized set.

## Comparator and test changes

- Distinct global, GPU-source and external-boundary scopes.
- Verified code anchors for pathless GPU agents; explicit console role anchors.
- Reasons for every unresolved edge endpoint and unidentified active handle.
- Physical GOP, console-variable and console-alias comparisons are independent.
- Independent manifests for different builds; optional new private JSON report.
- Tests renumber handles, swap driver ownership, change console aliases or open
  counts, remove code, introduce relocations/duplicate identities/duplicate
  paths and resolve an external owner. Unknown is never converted to pass.
- Optional integration test checks the actual private CAP0003/CAP0004 pair;
  it skips explicitly when the operator captures are unavailable.

All 39 Python tests pass locally (38 portable tests and the private pair
integration test). No EFI source or deployed USB binary was changed by this
comparator update.

## What remains before a release adapter can be qualified

Capture and verify identities of the three boundary owners and the virtual
console callbacks; distinguish USB/logging dependencies before stopping any
graphics consumer. Build a dedicated MSI ownership policy and simulate its
release/cleanup failures. Only a separately approved hardware trial could
establish actual driver detach, FLR and Windows recovery.

The current comparison proves agreement of observed GPU-source relationships,
not isolation, safe release, or unlock support.

## Read-only boundary collection preparation — 2026-10-28

The existing captures were revalidated offline against their individual build
IDs. Their five GPU-source relationships still compare equal, and the three
boundary identities remain unknown. No additional identity can be recovered
from their existing contents by changing a comparison rule.

The diagnostic source now also selects agents of incoming active relationships
whose controller is the target PCI handle or one of the non-null system console
roots. These are the three missing relationships in the actual captures. It
records their existing DriverBinding/LoadedImage metadata and bounded code
sample in an optional `boundary_agents` array. Selection is based on raw live
relationships, not hard-coded capture handle numbers. It invokes no driver
callback and does not stop a driver or claim a protocol. Unrelated external
agents are excluded; repeated agents are captured once, including deduplication
against GPU agents. At most 32 boundary agents are selected. GPU identities are
serialized first; the original aggregate 1 MiB code budget is shared. Exhausted
budget or missing/invalid binding/image/code leaves identity unverified, not
approved. Graph completeness does not imply complete identity capture.

The comparator validates these optional identity records by the same code,
digest, binding and ambiguity rules as GPU records. It keeps the original
direct-boundary scope: identifying a root-bridge driver does not automatically
pull all its unrelated children into the GPU boundary. Boundary code changes
can fail their own comparison even when GPU driver code still matches. Older
captures without the new array retain their previous unknown result.

Validation: 7,925 application checks, 309 platform PE/ownership checks and 44
Python tests pass. New cases cover target/console incoming owners, unrelated
owners, null controller/console handles, shared/repeated agents and unavailable
bindings; comparator tests cover handle permutation, corrupt/relocated/unbound
code, duplicate identities and independent boundary drift. Pure identity tests
and the supplementary read-only PE/source verifier also pass.

**Source preparation only:** no new diagnostic EFI was built, staged or run.
The existing diagnostic remains SHA256
`18efed6c4a60ce0ae682fb9c9d165f425c280a2159e32f57124f8cf3c41703f9`;
it does not contain this extension. Its old manifest must not be presented as
provenance for the changed sources. The separate Stage A binary is unchanged.

This extension alone is not a complete next capture packet. Virtual console
function-pointer/code provenance, USB/logging dependencies and relevant service
callbacks are still missing. No Stop behavior, notification exclusion, reset
or recovery is established. P2 and T7 remain open; no release adapter is enabled.
