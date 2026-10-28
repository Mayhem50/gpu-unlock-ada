# Capture 6: corrected passive observation succeeds

2026-10-28. Following the operator's reboot report, `CAP0006.JSON` was read
from the Kingston, validated and archived privately with its exact 0.4.4 USB
manifest. No GPU query, EFI launch, reset/unlock, USB rewrite or protection
change was performed during this review.

Capture SHA256:
`1ee966fb7f30b6009639d4629d975ebefd13c6c9be293e4f5c6980b0d133d6c7`.
Version 0.4.4 and build ID
`5be4190808ab2fd9c320ec7018b066f5d75922de4df2fd093f189f9adf72afc3`
match the deployed packet. Archive: ignored `captures/2026-10-28-run6/`.
These remain operator-provided firmware observations, not authenticated logs.
The reboot report alone does not establish a cold GPU rail power cycle.

## Result

The normal, unchanged analyzer/comparator accepts the new dependency observation:

| Check | Capture 6 |
| --- | --- |
| Image-range coverage | complete |
| Registered images | 264 |
| Large unrelated images | 1 |
| Image scan issues / truncated issues | 0 / false |
| Required callback entries | 24, resolved |
| Callback implementation images | 2, verified non-relocated code |
| Source USB/log binding | matched |
| GPU-source ownership against capture 5 | matched |
| Three direct external boundary owners against capture 5 | matched |
| Physical GOP / GPU driver code / inventory against capture 5 | matched |
| Storage ownership / quiescence / release permission | unproved / unproved / false |

This supplies hardware observation that the numeric-range/code-reading
classification correction works on this run. No incomplete flag was overridden.
Capture 5 retains its original incomplete result, so an independently complete
callback-to-callback repeated observation is not claimed across the pair.
The two recorded callback code digests nevertheless match the raw image records
in capture 5. No inference about safe Stop behavior or event exclusion follows.

The PROM SHA256 remains
`24b6d455544615dd3185e6c42512c104add2322b8c10053f6384566729ce7076`.
The USB path ancestor candidates are snapshot-local handles 139, 384, 414 and
415. These are collection candidates, not proven storage driver identities or
safe-detach permissions. Unrelated unresolved global edges are not promoted to
GPU blockers merely because they exist elsewhere in the graph.

## SS0 changes again; effective readout does not

Only SS0 differs among the 22 compared register sample values:

| Observation | Capture 5 | Capture 6 |
| --- | --- | --- |
| SS0 | 0x04203025 | 0x04203065 |
| SS1 | 0 | 0 |
| FEAT_READOUT_1 | 0x00410080 | 0x00410080 |

The XOR is 0x00000040: bit 6 sets, changing nibble 1 from 2 to 6.
The nine effective readout fields remain 0, 0, 2, 0, 0, 2, 0, 2, 0;
the repository's full-readout predicate remains false. This does not establish
cause, throughput or a unit-specific rate meaning. Capture 6's fourth raw SS0
value is outside the strict host profile's original two-value allowlist; no
admission mask/value was changed. It is not evidence of an unlock or of damage.

## Next scope

The collector correction is observed working, and external boundary owner
identities now repeat across captures 5/6. Remaining work is offline review of
the identified console/file implementations, relevant USB/storage ownership and
notification/service behavior, alongside the still-unimplemented native
transaction/reset/DMA/terminal contracts. P2/T7 and unlock/recovery gates remain
open; another reboot is not automatically requested or needed to acknowledge
this result. Windows NVIDIA driver/workload recovery is not independently tested
by reading this log. No new probe or unlock is prepared/launched by this review.
