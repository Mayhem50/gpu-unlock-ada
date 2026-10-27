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
