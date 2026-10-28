/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef MSI_ADAPTER_ADMISSION_H
#define MSI_ADAPTER_ADMISSION_H
#include <Uefi.h>
#include <Protocol/PciIo.h>
/* Preparation only. No firmware entry point or native execution is provided. */
#if !defined(MSI_ADAPTER_HOST_TEST) || defined(GU_UEFI_PORT)
#error Only fake-service host tests are supported by this adapter foundation
#endif
typedef struct {
    BOOLEAN entered, violation, observed;
    UINTN attempted, completed;
    EFI_STATUS status[8];
    UINTN location[2][4];
    UINT64 supported[2], attributes[2];
    UINT32 config[2][64];
} MSI_ADMISSION;
/* Observation is NEVER an ownership, reset, DMA or unlock permit. Initialize
 * once to zero; retain for the entire session; no retry on the same object. */
BOOLEAN MsiObservePci(EFI_PCI_IO_PROTOCOL *, MSI_ADMISSION *);
#endif
