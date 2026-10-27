/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef MSI_RESET_INTERNAL_H
#define MSI_RESET_INTERNAL_H
#include "port.h"
/* Session orchestration alone grants MSI_RESET_READY; not part of the
 * pre-reset callback API. Retained failures have no reset/cleanup retry. */
bool msi_reset_owned(const MsiOps *,MsiSession *,const uint32_t reference[64],unsigned live_dma);
bool msi_lifecycle_expected(const MsiOps *,unsigned reset_count,unsigned live_dma);
#endif
