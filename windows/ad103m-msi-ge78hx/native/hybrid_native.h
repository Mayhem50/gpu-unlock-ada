/* SPDX-License-Identifier: GPL-2.0-only */
#pragma once
#include "port.h"
EFI_STATUS GuHybridObserve(EFI_SYSTEM_TABLE *,EFI_HANDLE);
EFI_STATUS GuHybridAcquire(VOID);
BOOLEAN GuHybridVerify(VOID);
EFI_STATUS GuHybridFinish(VOID);
EFI_STATUS GuHybridAbort(VOID);
BOOLEAN GuHybridReadyForWindows(VOID);
BOOLEAN GuHybridMutated(VOID);
