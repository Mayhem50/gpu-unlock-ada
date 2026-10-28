/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef MSI_STOCK_HANDOFF_H
#define MSI_STOCK_HANDOFF_H
#include <Uefi.h>
#include <Protocol/LoadedImage.h>
#include <Protocol/SimpleFileSystem.h>
#include <Protocol/DevicePath.h>
#include <Guid/FileInfo.h>
#if defined(STOCK_HOST_TEST) == defined(STOCK_EFI_BUILD)
#error Select exactly one stock-only host or EFI build
#endif
#ifdef GU_UEFI_PORT
#error This is not the GPU unlock adapter
#endif
typedef struct {
    UINT8 partition[16],digest[32];
    UINT32 partition_number;
    UINT64 start_lba,sectors;
    UINTN image_bytes;
} STOCK_PROFILE;
typedef struct {
    BOOLEAN entered,start_attempted,terminal,child_verified;
    unsigned stage;
    EFI_HANDLE child;
    EFI_HANDLE *handles;
    EFI_FILE_PROTOCOL *root,*file;
    VOID *bytes,*path;
} STOCK_STATE;
/* Stage A only: no PCI/GOP protocols, driver release, GPU DMA or reset. */
EFI_STATUS StockRun(EFI_HANDLE,EFI_SYSTEM_TABLE *,const STOCK_PROFILE *,STOCK_STATE *);
BOOLEAN StockPath(const UINT8 *,UINTN,const STOCK_PROFILE *,UINTN *,BOOLEAN *,BOOLEAN *);
#endif
