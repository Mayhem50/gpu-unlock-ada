# MSI capture 5 offline review — 2026-10-28

After the operator reported reboot completion, `CAP0005.JSON` was found on the
Kingston and archived privately with the exact USB packet manifest. No GPU
query, probe launch, reset/unlock, USB rewrite or protection change was made
during this review.

Capture SHA256:
`11b769ca3df823495cb2aaabf8e3de71a74f064907a6a2f6e76d1b69f2359cea`.
The JSON passes the 0.4.3 analyzer with build ID
`e704259c189d3cd164212337e4df016ea32f43da0ee416e96cc824e6805435ab`.
The USB fallback still hashes to the reviewed 0.4.3 binary. These are
operator-provided firmware observations, not independently authenticated logs.
The local archive is ignored under `captures/2026-10-28-run5/`.

## New evidence

- Exact target identity remains observed. Physical GOP, GPU-source ownership
  and GPU driver code compare equal to capture 4. PROM SHA256 is unchanged:
  `24b6d455544615dd3185e6c42512c104add2322b8c10053f6384566729ce7076`.
- All five GPU-source edges resolve. All three direct external boundary edges
  now resolve within this capture, without rejected driver identities. Their
  identities were absent in captures 3/4, so cross-capture boundary agreement
  remains unknown; missing old data cannot be filled retroactively.
- The root-bridge owner has its own image. Keyboard and pointer owners are
  distinct DriverBinding instances/Stop RVAs in the same console-driver image.
  Snapshot-local handle numbers are not used as identity.
- All 24 callback addresses are non-null. The console protocol interfaces
  match the system table, ConOut/StdErr alias, and the captured source USB
  volume/path digest matches the actual opened-log journal. Two callback image
  records have non-relocated executable code and complete digest-verified bytes.
- Firmware-reported SecureBoot=0 and SetupMode=0 are present in the capture.
  This does not establish current BitLocker or Windows security state.

## Incomplete observation: keep the refusal

`dependencies.observation_complete=false`; the normal comparator therefore
refuses to qualify callback provenance. The capture does not record per-image
enumeration rejection reasons, so the exact full set of causes cannot be
recovered from this JSON alone.

One concrete collector defect is visible offline: it marks any registered
image over 16 MiB as incomplete, even if no captured callback points into it.
The probe's own compiled SizeOfImage is **68,214,784 bytes** because of the
report buffer, although its file is only 37,888 bytes. Its normal loaded image
therefore exceeds that cutoff. This supplies a reproducible candidate for the
incomplete result; the capture does not record its loaded range to establish
that it was the only rejection.

As an isolated diagnostic experiment on an in-memory copy only, changing just
the completeness flag lets the existing callback code/range checks pass. The
real capture and normal verdict were not changed. This experiment establishes
that no *additional recorded callback-field* rejection is reached; it does not
prove complete image enumeration or authorize ignoring the flag.

The correction needs separate numeric image-range coverage and bounded code
reading. A large unrelated image should not require parsing/hashing its code;
an overlapping/relevant large image must remain unverified without unsafe
reads. Regression cases must include the real-size self image, large relevant
images, overlapping ranges, missing metadata and explicit rejection reasons.
Do not globally widen the 16 MiB code-reading cap or mark this capture complete.

## New stock selector value

The compared register samples differ only at SS0:

| Capture | SS0 |
| --- | --- |
| 4 | `0x04003065` |
| 5 | `0x04203025` |

This third value is outside the two historical values admitted by the strict
host profile. It is not automatically an error or evidence of unlock, but the
current profile must refuse it. No mask or expected value was relaxed. The
reason/conditions of this variation require offline interpretation and later
explicit validation. Neither the unchanged PROM nor boundary identification
proves a fully qualified transaction state.

## Status

The reboot produced a valid new diagnostic log; the operator is back in the
chat. NVIDIA driver/display/workload recovery was not independently tested by
this review. P2 still needs repeated identities and complete callback/storage
evidence; T7 event/service exclusion and native transaction/recovery gates stay
open. No new image is built/staged as a consequence of this report, and no
unlock/reset test is ready.
