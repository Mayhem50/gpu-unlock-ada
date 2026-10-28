# MSI diagnostic 0.4.3 local packet — 2026-10-28

The boundary-owner and passive console/filesystem/log capture extensions are
now compiled into a separate unsigned diagnostic. Preparation and validation
were offline. No GPU query, EFI execution, USB staging, reset, release, unlock,
firmware flash or boot-protection change was performed.

| Artifact | Verified value |
| --- | --- |
| Version | 0.4.3 |
| Binary bytes | 37,888 |
| SHA256 | `2463cfd10c1524f04b4b4c91b87412d483fa2dccc254d991f07a892647e6cb6c` |
| Build ID | `e704259c189d3cd164212337e4df016ea32f43da0ee416e96cc824e6805435ab` |
| Local directory | `windows/ad103m-msi-ge78hx/diagnostic/build/packets/diagnostic-0.4.3-2463cfd10c1524f0` |
| Deployment / firmware execution | false / false |

The ignored local directory contains `AD103M-PROBE.EFI`, its exact sealed
manifest and `LIRE-AVANT-TEST.txt`. No fallback boot directory or automatic
installer is included. A repeated create refuses to replace the packet.

The previous 0.4.2 binary and original manifest/qualification/build ID were
copied to ignored `diagnostic/build/archive/20261028T1652230297108Z/` before
rebuild. Its verified SHA256 remains
`18efed6c4a60ce0ae682fb9c9d165f425c280a2159e32f57124f8cf3c41703f9`.
The local Stage A binary remains
`8ee3689bae43e8edd760612c358d059c3b2bc570712ecc0d844dee9170a7b1d0`;
no USB access or replacement occurred during this preparation.

## Evidence and scope

Native x64 compilation/linking passed /W4 /WX and deterministic PE linking.
The PE verifier confirms EFI application format, executable entry, relocations
and absent imports/TLS/CLR. The full CPU diagnostic suite passed 7,925
application checks, 605 platform checks and 62 Python tests, plus pure identity
tests. Historical captures 3/4 still pass their own manifest/compatibility checks.
The 0.4.3 version regression requires the existing strict BAR and SEC2 rules;
updating the version cannot bypass those guards.

Build invalidates passing state before compilation, snapshots source/tests/tools
and pinned headers, then rejects input drift during validation. The sealed
manifest includes generated build files and compiler/linker/setup/Python paths,
versions where available and SHA256 values. Packet verification recomputes
source/build identity, checks the embedded version/build ID, binary bytes,
generated provenance and tool digests against current local files. Tests cover
stale/missing provenance, altered binary/tool/build identity, failed PE validation
and refusal to overwrite. This is not a hermetic compiler trust proof.

Recheck from the repository root without GPU access or EFI execution:

```powershell
python windows/ad103m-msi-ge78hx/diagnostic/tools/packet.py verify
```

## Before a later capture

The packet is prepared for review, not approved/executed. Its manifest retains
`execution_approved=false`, `deployed=false`, `firmware_executed=false` and
`hardware_tested=false`. This diagnostic contains no unlock or GPU reset/release
route. At runtime it retains the previous bounded PCI/MMIO/PROM reads and writes
a new capture log to its own USB; it is not a zero-GPU-access EFI.

A future identity-checked USB operation must preserve and verify the currently
installed Stage A first. Review the exact package/hash and current security,
recovery, saved-work, power and workload prerequisites before a separate manual
launch. No partition formatting, boot-variable installation or protection change
is required by the package preparation. The diagnostic starts directly if
installed/selected as USB fallback; it does not use Stage A's W confirmation.

The new capture has no predetermined number. Keep it with this manifest and
the reported conditions/last screen; never assign this build ID to old captures.
The pending evidence remains real boundary/callback identities, storage driver
ownership and event/service behavior. Matching callback code alone cannot close
P2/T7 or qualify detach, reset, Windows driver recovery or unlock.
