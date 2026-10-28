# Diagnostic 0.4.3 USB staging

This installer only copies reviewed diagnostic files. It never runs an EFI,
reboots, touches the GPU, formats storage, writes an internal ESP/boot variable
or changes protections. It requires PowerShell 7, the exact sealed local packet
and the private reviewed Kingston identity from the Stage A profile.

`Stage-Diagnostic.ps1` rechecks the packet/build provenance, fixed reviewed
version/build ID/hash/size and current E: USB identity. Disk numbers are not
identity: the same Kingston was observed as disk 3 in this session. Serial,
size, partition GUID, USB bus, FAT32 and volume label must all match. Redirected
paths and an unexpected existing fallback refuse. F: is never accessed.

The accepted prior fallback is the reviewed Stage A image. It is copied to a
new local ignored backup directory and hashed before USB writes. A versioned
USB metadata directory receives the diagnostic and exact sealed manifest and
instructions without overwriting existing metadata. A temporary boot image is
hashed, media identity/current fallback rechecked, and then the fallback is
replaced. The final bytes are checked again. No capture file is overwritten.

`Test-Policy.ps1` exercises 20 RAM-only cases covering mismatched media fields,
wrong packet flags/version/build/hash and altered byte count. It never accesses
devices or writes files. Staging records its own script/policy hashes, backup,
identity and progression in a separate local deployment journal. The sealed
build/packet manifest's `deployed=false` remains a build-time fact; actual media
state is recorded by `diagnostic/build/deployment-043.json`. It never records
firmware execution as having happened.

On 2026-10-28 staging completed and both destination and backup hashes matched.
The exact record and private paths are in that journal. Do not blindly rerun:
this installer requires Stage A as the current fallback and refuses a different
image, even the already installed diagnostic. Recovery/replacement is a separate
reviewed operation using the verified backup, not an automatic failure action.

The next manual boot from this USB starts diagnostic v0.4.3 directly, unlike
Stage A's W prompt. It performs the existing bounded PCI/MMIO/PROM reads and
passive dependency sampling and writes a new log to its own USB. It contains
no unlock, GPU reset or driver detach. Check then-current security/recovery,
saved work, idle workload and AC prerequisites before a separate trial.
