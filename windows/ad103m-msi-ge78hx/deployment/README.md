# Diagnostic 0.4.4 USB staging

Stage-Diagnostic.ps1 installs only the exact reviewed 0.4.4 version/build/hash
and requires the current USB fallback to be the reviewed 0.4.3. It never runs
an EFI, reboots, touches the GPU, formats storage or changes boot protections.

The actual fallback and the old USB manifest/instructions are copied to a new
local backup and verified before replacement. The previous Stage A backup and
capture files remain untouched. New metadata is stored in a fresh SHA256-named
USB directory. Media identity and bytes are rechecked before replacing the
fallback and afterward. Disk numbers are not identity; E: must match the private
Kingston serial/size/partition GUID, USB bus, FAT32 and label. F: is not accessed.

Test-Policy.ps1 passes 20 RAM-only cases for media/artifact mismatch. It performs
no device access or file mutations. Staging stores its own script/policy hashes,
identity, backup and progression in diagnostic/build/deployment-044.json.
The sealed packet remains immutable; its deployed=false field is build-time
state, while the separate deployment journal records actual USB staging.

On 2026-10-28 staging finished with status=verified, matching destination/backup
hashes and firmware_executed=false. Do not blindly rerun this one-step migration:
it requires 0.4.3 and refuses any different current fallback, including 0.4.4.
Restore/replacement using the verified backup is a separate reviewed operation.

The next deliberate boot from the Kingston starts diagnostic v0.4.4 directly,
without Stage A's W prompt. It includes bounded PCI/MMIO/PROM reads and passive
callback sampling; it writes a new capture log to its own USB. No unlock/reset
or driver detach is included. This preparation did not execute it or change any
protection. Check current security/recovery, idle workloads, saved work and AC
before a separate manual trial. See docs/windows-msi-ge78hx-diagnostic-v044-deployment.md.
