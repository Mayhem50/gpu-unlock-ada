/* SPDX-License-Identifier: GPL-2.0-only */
#include "stock.h"
#include "profile.h"
#include <intrin.h>
#ifndef STOCK_EFI_BUILD
#error Native entry is excluded from host tests
#endif
static STOCK_STATE State;
static STOCK_STATE *volatile StateAddress=&State;
EFI_STATUS EFIAPI UefiMain(EFI_HANDLE image,EFI_SYSTEM_TABLE *table) {
    STOCK_STATE *state=StateAddress;
    EFI_STATUS result=StockRun(image,table,&NativeProfile,state);
    if(state->terminal)for(;;)_mm_pause(); /* RAM-only, no firmware callback */
    return result;
}
