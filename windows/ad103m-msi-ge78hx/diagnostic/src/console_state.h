/* SPDX-License-Identifier: MIT */
#pragma once
#include "platform.h"
#define CS_MAP_CAP 65536u
#define CS_SLOT_COUNT 22u
typedef struct {UINT32 rva;UINT8 bytes;const CHAR8 *name;} CS_SLOT;
extern const CS_SLOT CsSlots[CS_SLOT_COUNT];
typedef union {UINT64 alignment;UINT8 bytes[CS_MAP_CAP];} CS_MAP;
typedef struct {
    BOOLEAN entered,violation,pinned,slots_complete,slots_equal,map_equal,final_equal;
    BOOLEAN binding_verified,map_called[2];EFI_STATUS map_status[2];
    BOOLEAN interfaces_revalidated;
    const CHAR8 *reason;
    CS_MAP maps[2];UINTN sizes[2],strides[2],keys[2];UINT32 versions[2];
    UINT8 slots[2][CS_SLOT_COUNT][8];
    UINTN slot_masks[2],interface_masks[2],queried_mask;
    EFI_STATUS interface_status[4];UINTN interfaces[4],words[2][4][6],word_count[4];
    EFI_STATUS revalidated_status[4];UINTN revalidated_interfaces[4],revalidated_mask;
    BOOLEAN interface_read[4],interface_equal[4];
    UINTN image_base,image_bytes;EFI_HANDLE image_handle;
    UINTN outputs[2],candidate_count,ram_candidates,path_matches,code_matches;
    EFI_HANDLE roots[3];
} CS_CAPTURE;
BOOLEAN CsReadable(const UINT8 *,UINTN,UINTN,UINT32,UINTN,UINTN);
void CsCapture(CS_CAPTURE *,EFI_SYSTEM_TABLE *,EFI_HANDLE *,UINTN);
void CsJson(TEXT *,const CS_CAPTURE *);
