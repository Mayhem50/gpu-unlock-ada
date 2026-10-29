/* SPDX-License-Identifier: MIT */
#pragma once
#include "core.h"
/* Read-only policy shared by the hybrid observer and a future release route.
 * This is never an authorization to write, power up or claim a GPU. */
typedef struct {
    BOOLEAN exact,capabilities_valid,pm_present,mmio_read_allowed;
    UINT16 command,pm_offset,pmcsr;
    const CHAR8 *reason;
} HYBRID_PCI_STATE;
HYBRID_PCI_STATE HybridPciState(const UINT8 config[256]);
