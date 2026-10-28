/* SPDX-License-Identifier: MIT */
#pragma once
#include "core.h"
#include <Protocol/SimpleFileSystem.h>
typedef struct {UINT32 rva,bytes,relocations;} CODE_VIEW;
typedef struct {EFI_HANDLE volume;UINT8 path_sha256[32];BOOLEAN opened;} LOG_SOURCE;
BOOLEAN LoadedCodeView(const UINT8 *,UINTN,CODE_VIEW *);
void SamplePlatform(TEXT *,EFI_SYSTEM_TABLE *,EFI_HANDLE,EFI_HANDLE,EFI_FILE_PROTOCOL *,const LOG_SOURCE *);
