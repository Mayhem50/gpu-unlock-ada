# MSI host prototype: alignment with the repository method

Initial review: 2026-10-27, implementation `f3a7795`; table updated after host
FBPA/readout and lifecycle-ownership remediation. Scope: local source,
existing private capture-derived fixtures and CPU tests. No GPU query, EFI,
USB deployment or security modification. Sources below are this checkout,
not live re-verification of external owner reports or issue #76.

## Conclusion

The host orchestration follows the reference's safety intent: exact admission,
captured consumers, exclusive claim, narrow decode restoration, completion
proof, one FLR, retention on uncertainty and stock Windows after checks.
It does **not** implement or validate the full repository transaction. Its
guarded compound commit is a new CPU abstraction, not a demonstrated UEFI
property or a feature tested on this MSI.

The [checklist](windows-msi-ge78hx-launch-checklist.md) now records evidence
and open gates separately. Coverage of known requirements is not proof that
all future hazards are known or all gates passed.

## Traceability to local reference source

| Repository method / source | MSI prototype and status | Gate |
|---|---|---|
| Exact target and board pins: root README; AD104 profile.c/checks.c | Independent 2717 pins, no Linux admission widened; system/OS/boot profile incomplete | P1–P5 |
| Consumer/callback identity: AD104 msi_release.c | Five semantic edges; hashes do not resolve three boundary owners/callback identities | P2/P3 |
| TextOut, GraphicsConsole, NVIDIA detach; exclusive claim; IO/MEM-only restore | Same modeled order, no code/global patches; native PCI I/O attributes absent | P3 |
| Pinned five AD102 assets: firmware/README.md; AD104 profile.c/boot_io.c | PROM/FWSEC parsing only; no generated-asset/signature qualification | P5/T2 |
| GFW/FWSEC/signature/boot/restore/selectors/reset/unwind: AD104 vendor/unlock_610.c | Typed retained pre-reset step; sole scheduler/private reset; single-use backend gate blocks raw replay before effects; full core boundary refused | T1 host complete after F3; T2/T6/T7 open |
| Four sampled PLMs, three required values: ad102_sample_permissions | All four sampled, FBPA accessibility only, independent PCI/chip proof and raw failure evidence | T3 host complete |
| Effective readout at selectors/pre-FLR/post-FLR: ad102_readout | Pre-FLR check repeated just before FLR; existing post-FLR check retained. Actual selector writes unimplemented | T4 host readbacks complete; T2 open |
| Pre-BME saved state; BME off; one FLR; quiet period; decode-off restore: AD104 port.c | Private reference, 100 ms quiet, strict compare before decode; pending budget 100 ms versus reference 1 s | T8 |
| Post-FLR GFW and SEC2 reset release: reference reset path | Selected registers checked; SEC2 choreography absent | T5 |
| DMA ledger, partial errors, terminal halt: AD104 port.c/app.c | Flags/mock guard only; native resource/timing/terminal handling absent | T6–T9 |
| DMA release, claim close, USB close, final check, StartImage: AD104 app.c | Keeps DMA/claim through callbacks, then abstract compound commit; intentional ordering divergence | T7 |
| Private Windows image/path, consent/skip: AD104 boot_io.c | Staging/choice modeled; no real image pins, UI or loader | P4/V |
| Host, hardware and workload evidence separate: docs/testing.md | CPU results only; no application-gain or recovery claim | V/W |

The pending-poll budget difference is a conservative refusal risk, not evidence
that the laptop needs the reference timing. Exact config comparison is also
stricter than the reference's narrowly reviewed status-clear exception. Neither
should be relaxed just to make a native trial pass.

## Integration hazards

1. The reference gpu_unlock_610_unlock already calls reset and unwind, owning
   buffer lifetime. The old generic transaction slot has been replaced by a
   typed pre-reset/retained boundary, with a single orchestrator for reset and
   guarded cleanup. A full-core declaration is refused before mutation and its
   signature cannot bind implicitly. Never wrap/cast the complete entry or merely
   retarget its AD104 PCI ID. Native enforcement remains an open gate.
2. The reference checks signature phase counts and ordered selector updates.
   MsiProof booleans/dma_live are not equivalent. GFW, FRTS selection, stock
   signature restoration and partial unwind are not exercised by this mock.
3. FBPA is sampled for accessibility but is not a mandatory all-ones value.
   The wrapper now samples it and verifies the full effective readout before FLR,
   including a repeated check after pending polling. These host parity gaps are
   closed; native firmware execution is not qualified by that result.
4. The atomic final operation is sequential RAM changes in the mock. Native
   cleanup/StartImage legality, error handling and exclusion are unproved.
   Do not hold a guessed raised TPL across firmware services or treat a guard
   boolean as an implementation.

## Checklist/documentation repairs

The old checklist still described the diagnostic as undeployed and captures as
future work. It now records user-executed captures 3/4 historically and the last
user-reported Secure Boot/BitLocker state, without asserting current state or
changing it. It separates the diagnostic, host prototype and absent native port.

Open gates now explicitly cover reset/unwind ownership, real transaction phases,
PLM/readout semantics, SEC2 recovery, native guard feasibility, partial DMA,
timing, terminal behavior, stock/no-unlock reset, Windows identity/recovery,
matched correctness/performance, publication hygiene and security closure.
The design and workspace entry point are updated accordingly. The initial review
changed no code; the subsequent remediation adds host checks and regressions.
No adapter is prepared.

Current validation after remediation: 410 host scenarios / 191,252 assertions
and 39 Python tests pass. Local documentation links and whitespace checks pass;
the existing diagnostic EFI hash is unchanged. This does not include a rerun of
the AD104 full core or Linux test-full suites and supplies no native MSI evidence.

The [subsequent review and remediation](windows-msi-ge78hx-pre-flr-review.md)
records F3/F4 and their integrated rejection regressions. Raw reset now uses a
spent backend gate independent of counters, and stock commit revalidates clean
scoped lifecycle at the action point. Both remain trusted host contracts,
separate from native qualification.

The [F3/F4 follow-up review and remediation](windows-msi-ge78hx-f3-f4-review.md)
confirms the reset gate cases and records F5, now covered by integrated combined
fault regressions. Stock resource uncertainty is classified independently before
lease/log/state/replay exits, and retention remains sticky after journal repair.
V0 is complete as a host contract only; native qualification is still open.
