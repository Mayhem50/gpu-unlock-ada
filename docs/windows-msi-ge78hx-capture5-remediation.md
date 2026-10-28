# Capture 5 classification correction and SS0 analysis

2026-10-28. Source changes and CPU validation only. No GPU access, firmware
execution, native EFI rebuild, USB update, reset/unlock or protection change.
The captured JSON and deployed 0.4.3 image are not modified.

## Image-range classification

The old collector applied the 16 MiB code-reading cap before determining
whether an image contained any captured callback. The probe's own 68,214,784
byte loaded-image geometry therefore made it an unrelated large-image refusal.
Capture 5 lacks rejected-image details, so this explains a reproducible defect,
not a proof that it was the only reason for that capture's incomplete flag.

The corrected collector separates numeric range coverage from code reading:

| Case | Classification and action |
| --- | --- |
| Valid unrelated image, including the 68 MiB self image | Cover its numeric range; no PE, code or FilePath read |
| Relevant image within 16 MiB | Preserve bounded PE/code checks and digest evidence |
| Relevant image over 16 MiB | Retain its range, emit no path/code bytes, report `matched_image_exceeds_code_limit`; observation remains incomplete |
| Query error/warning or null returned protocol | Explicit query/null-protocol issue; incomplete range coverage |
| Null/zero/wrapping image geometry | `invalid_image_range`; no PE read; incomplete coverage |
| More than 32 matched images | `matched_image_limit`; incomplete coverage |

The 16 MiB image code-reading cap and 512 KiB executable-section cap are not
widened. An `image_scan` summary distinguishes range completeness, registered
images, unrelated large images and explicit per-image issues/statuses. Issue
storage is bounded at 32 records; the total count and truncation flag remain
visible. The offline comparator refuses nonzero/incomplete scan evidence even
if the aggregate observation flag is incorrectly set true. Older captures with
no scan summary keep their existing interpretation. In particular, capture 5
still yields unknown; no synthetic corrected capture is created.

Regressions use the exact self-image size with deliberately unreadable unrelated
base/path addresses, a large overlapping image, wrapping ranges, null protocol,
query failure/warning and matched-image exhaustion. The large unrelated-image
C output passes normal Python verification, without forcing its complete flag.
Existing ambiguity/non-executable/code-digest checks remain active.

## SS0: raw findings and limits

`selector_analysis.py` computes these values from the archived captures only.
It rejects missing/duplicate samples, wrong register offsets, transport errors
and inaccessible sentinels rather than replacing them with zeros.

| Observation | Capture 3 | Capture 4 | Capture 5 |
| --- | --- | --- | --- |
| SS0, 0x0082381c | 0x04013065 | 0x04003065 | 0x04203025 |
| SS1, 0x00823820 | 0 | 0 | 0 |
| FEAT_READOUT_1, 0x00823818 | 0x00410080 | 0x00410080 | 0x00410080 |
| FEAT_READOUT_0 | 0x00000233 | 0x00000233 | 0x00000233 |
| FEAT_PLM | 0xffffff8f | 0xffffff8f | 0xffffff8f |

Capture 4 XOR capture 5 is **0x00200040**: bit 6 clears and bit 21 sets.
With the repository's eight 4-bit SS0 fields, numbered from least significant:

| Field | Bits | Capture 4 | Capture 5 |
| --- | --- | --- | --- |
| 1 | 7:4 | 6 | 2 |
| 5 | 23:20 | 0 | 2 |

All other SS0 fields are unchanged. The rest of the 22 recorded register sample
values also match capture 4. PROM bytes and observed GPU-source graph remain
equal, as recorded in the original capture 5 review.

The [upstream transaction](../kernel/gpu_unlock/unlock_610.c) defines SS0 as
eight 4-bit override fields and uses SS0=0x88888888 / SS1=8 for its final selector
readbacks. Its `ad102_readout` independently examines nine 3-bit fields of
FEAT_READOUT_1 with mask 0x07ffffff. The [Windows reference core](../windows/ad104-msi/vendor/unlock_610.c)
keeps these separate override/readout checks. For all three MSI captures the
raw effective fields are **0, 0, 2, 0, 0, 2, 0, 2, 0**, so this independent
full-readout predicate is false and unchanged.

These facts do not establish what changed SS0, which instruction unit each
changed nibble controls on AD103M, or the meaning of nibble values 6 versus 2.
In particular, subtracting these numbers is not a throughput/rate calculation.
No gain, degradation, unlock, permanent fuse change or thermal/power cause can
be inferred from these three snapshots alone. Boot/firmware timing/state is a
possible category of explanation, not a demonstrated cause. There is no ordered
trace or controlled comparison to distinguish causes offline.

The Linux transaction's general entry method is not a universal two-value SS0
allowlist. The narrower MSI host admission is our additional unqualified-port
policy. It remains unchanged: a new regression explicitly refuses 0x04203025
with the rest of the profile valid. This refusal is not a finding that the new
stock value is inherently dangerous. Do not widen a mask or claim compatibility
without separately reviewing the admission/readback contract and remaining gates.

## Validation and artifact state

- Diagnostic CPU suites: **7,925 application / 722 platform checks**, **68 Python tests**;
  pure identity and read-only previous-PE/source checks pass.
- MSI host port: **500 scenarios / 243,628 assertions**, including the new
  capture-5 SS0 refusal at profile and workflow entry, before console silence,
  driver detach, claim, transaction, reset or transfer; existing 45 DMA-ledger cases remain included.
- Admission adapter: **148 cases / 3,211 assertions**, passing.

The tracked `Test-Host.ps1` runner compiled/ran hosted programs only. No corrected
EFI was built or put on USB. The existing sealed 0.4.3 packet preserves the old
source hashes: it is now intentionally stale relative to these modified sources
and staging verification must refuse it as a current-source build. Rebuilding,
versioning and USB deployment are separate future work, not implied by tests.
The historical native binary remains useful as capture-5 provenance, not as
evidence that these corrections ran in firmware. No unlock/reset gate is closed.
