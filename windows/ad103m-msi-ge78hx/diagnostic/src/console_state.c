/* SPDX-License-Identifier: MIT
 * No source writes, callback invocations, pool allocation, list following or
 * private EFI_EVENT inspection. This is observation, never a release permit. */
#include "console_state.h"
#include <Protocol/LoadedImage.h>
#include <Protocol/SimpleTextInEx.h>
typedef CHAR8 CsX64Only[(sizeof(UINTN)==8)?1:-1];
typedef CHAR8 CsInputLayout[(sizeof(EFI_SIMPLE_TEXT_INPUT_PROTOCOL)==24&&sizeof(EFI_SIMPLE_TEXT_INPUT_EX_PROTOCOL)==48)?1:-1];
#if defined(CS_REAL_PROFILE) == defined(CS_SYNTHETIC_PROFILE)
#error Select exactly one real or synthetic console profile
#endif
#ifdef GU_UEFI_PORT
#error Read-only console collection cannot be bound to an unlock build
#endif
const CS_SLOT CsSlots[CS_SLOT_COUNT]={
 {0x5344,4,"mode_raw"},{0x5348,4,"attribute_raw"},{0x534c,4,"column_raw"},{0x5350,4,"row_raw"},{0x5358,1,"cursor_flag_raw"},
 {0x55c0,1,"pointer_gate_raw"},{0x55c1,1,"input_gate_raw"},{0x55c2,1,"pointer_stop_raw"},{0x55d8,1,"input_depth_raw"},{0x55d9,1,"buffer_flag_raw"},{0x55ec,1,"input_stop_raw"},
 {0x55f0,8,"expected_child_pointer"},{0x5638,8,"buffer_limit_pointer"},{0x5640,4,"columns_raw"},{0x5648,8,"text_buffer_pointer"},{0x5650,8,"mode_table_pointer"},{0x5658,8,"attribute_buffer_pointer"},
 {0x5728,8,"input_list_pointer"},{0x5760,8,"output_anchor_pointer"},{0x5768,8,"output_list_pointer"},{0x5780,8,"pointer_anchor_pointer"},{0x5788,8,"pointer_list_pointer"}
};
#ifdef CS_REAL_PROFILE
static const UINT8 CodeHash[]={0xc1,0x4e,0x51,0x70,0xfd,0xa8,0xbd,0x25,0x29,0x02,0x8c,0x82,0x48,0xd7,0xe6,0xcf,0x95,0x18,0xcb,0xac,0x49,0x8a,0xf7,0xf6,0x9f,0xd6,0xcd,0xc8,0xe9,0x92,0x97,0x66};
#else
/* Filled from the deterministic byte fixture, independently SHA256 checked. */
static const UINT8 CodeHash[32]={0x99,0x5e,0xaa,0xde,0xf1,0xb7,0x4e,0xce,0xf7,0x67,0x4b,0xda,0x35,0x86,0xfc,0x40,0x9f,0xad,0xdd,0xdb,0x9f,0x64,0x1e,0xb4,0x42,0x5b,0x7c,0x8f,0x48,0xb4,0xec,0x65};
#endif
BOOLEAN CsReadable(const UINT8 *map,UINTN bytes,UINTN stride,UINT32 version,UINTN address,UINTN length) {
    UINT64 at=address,end;UINTN records;
    if(!map||!length||!address||address>MAX_UINTN-length||version!=EFI_MEMORY_DESCRIPTOR_VERSION||
       bytes>CS_MAP_CAP||stride<sizeof(EFI_MEMORY_DESCRIPTOR)||stride>256||!bytes||bytes%stride)return FALSE;
    records=bytes/stride;if(records>1024)return FALSE;end=(UINT64)address+length;
    while(at<end) {
        UINT64 next=0;unsigned covering=0;
        for(UINTN i=0;i<records;i++) {
            const UINT8 *d=map+i*stride;UINT32 type=Read32(d);UINT64 base=Read64(d+8),pages=Read64(d+24),attrs=Read64(d+32),limit;
            if(!pages||pages>MAX_UINT64/4096||base>MAX_UINT64-pages*4096||(base&4095))return FALSE;
            limit=base+pages*4096;
            if(at>=base&&at<limit) {
                if(++covering!=1||!(type==EfiBootServicesCode||type==EfiBootServicesData)||
                   (attrs&(EFI_MEMORY_UC|EFI_MEMORY_WC|EFI_MEMORY_WT|EFI_MEMORY_WB|EFI_MEMORY_UCE))!=EFI_MEMORY_WB||
                   (attrs&EFI_MEMORY_RP))return FALSE;
                next=limit;
            }
            /* Reject even a partial overlapping descriptor in the read span. */
            if(base>at&&base<end&&next>base)return FALSE;
        }
        if(covering!=1||next<=at)return FALSE;
        /* All descriptors are re-scanned for overlap, independent of ordering. */
        for(UINTN i=0;i<records;i++) {
            const UINT8 *d=map+i*stride;UINT64 base=Read64(d+8);
            if(base>at&&base<next&&base<end)return FALSE;
        }
        at=next<end?next:end;
    }
    return TRUE;
}
/* Borrowed protocol object lifetime is not guaranteed across UEFI calls.
 * Fail closed until a separately reviewed lifetime/exclusion mechanism exists. */
void CsCapture(CS_CAPTURE *c,EFI_SYSTEM_TABLE *st,EFI_HANDLE *handles,UINTN count) {
    (void)st;(void)handles;(void)count;
    if(!c)return;
    if(c->entered){c->violation=TRUE;c->slots_complete=FALSE;c->reason="reentry_refused";return;}
    c->entered=TRUE;
    c->reason="interface_lifetime_unproven";
}
void CsJson(TEXT *t,const CS_CAPTURE *c) {
    static const CHAR8 *names[]={"simple_input","extended_input","simple_pointer_raw","absolute_pointer_raw"};
    TextPut(t,"{\"schema\":1,\"reason\":\"");TextPut(t,c->reason?c->reason:"not_collected");
    TextPut(t,"\",\"profile\":\"");
#ifdef CS_REAL_PROFILE
    TextPut(t,"msi-capture6-console-v1");
#else
    TextPut(t,"synthetic-test-only");
#endif
    TextPut(t,"\",\"profile_code_sha256\":");TextBytes(t,CodeHash,32);
    TextPut(t,",\"image_base\":");TextHex(t,c->image_base,16);
    TextPut(t,",\"image_bytes\":");TextDec(t,c->image_bytes);
    TextPut(t,",\"image_pinned\":");TextPut(t,c->pinned?"true":"false");
    TextPut(t,",\"active_console_binding_verified\":");TextPut(t,c->binding_verified?"true":"false");
    TextPut(t,",\"input_interfaces_revalidated\":");TextPut(t,c->interfaces_revalidated?"true":"false");
    TextPut(t,",\"candidate_diagnostics\":{\"candidates\":");TextDec(t,c->candidate_count);TextPut(t,",\"ram_readable\":");TextDec(t,c->ram_candidates);TextPut(t,",\"path_matches\":");TextDec(t,c->path_matches);TextPut(t,",\"code_matches\":");TextDec(t,c->code_matches);TextPut(t,"}");
    TextPut(t,",\"memory_maps\":[");
    for(unsigned i=0;i<2;i++) {
        UINT8 digest[32];
        if(i)TextPut(t,",");TextPut(t,"{\"status\":");if(c->map_called[i])TextHex(t,c->map_status[i],16);else TextPut(t,"null");
        TextPut(t,",\"bytes\":");TextDec(t,c->sizes[i]);TextPut(t,",\"descriptor_size\":");TextDec(t,c->strides[i]);TextPut(t,",\"version\":");TextDec(t,c->versions[i]);
        TextPut(t,",\"key\":");if(c->map_called[i]&&c->map_status[i]==EFI_SUCCESS)TextHex(t,c->keys[i],16);else TextPut(t,"null");
        TextPut(t,",\"sha256\":");if(c->map_called[i]&&c->map_status[i]==EFI_SUCCESS&&c->sizes[i]<=CS_MAP_CAP){Sha256(c->maps[i].bytes,c->sizes[i],digest);TextBytes(t,digest,32);}else TextPut(t,"null");TextPut(t,"}");
    }
    TextPut(t,"]");
    TextPut(t,",\"slots_complete\":");TextPut(t,c->slots_complete?"true":"false");
    TextPut(t,",\"single_use_violation\":");TextPut(t,c->violation?"true":"false");
    TextPut(t,",\"memory_maps_equal\":");TextPut(t,c->map_equal?"true":"false");
    TextPut(t,",\"post_service_state_equal\":");TextPut(t,c->final_equal?"true":"false");
    TextPut(t,",\"slots\":[");
    if(c->pinned)for(UINTN i=0;i<CS_SLOT_COUNT;i++) {
        if(i)TextPut(t,",");TextPut(t,"{\"name\":\"");TextPut(t,CsSlots[i].name);TextPut(t,"\",\"rva\":");TextHex(t,CsSlots[i].rva,8);
        TextPut(t,",\"bytes\":");TextDec(t,CsSlots[i].bytes);TextPut(t,",\"first\":");if(c->slot_masks[0]&((UINTN)1<<i))TextBytes(t,c->slots[0][i],CsSlots[i].bytes);else TextPut(t,"null");
        TextPut(t,",\"second\":");if(c->slot_masks[1]&((UINTN)1<<i))TextBytes(t,c->slots[1][i],CsSlots[i].bytes);else TextPut(t,"null");TextPut(t,"}");
    }
    TextPut(t,"],\"input_interfaces\":[");
    for(UINTN i=0;i<4;i++) {
        if(i)TextPut(t,",");TextPut(t,"{\"kind\":\"");TextPut(t,names[i]);TextPut(t,"\",\"status\":");if(c->queried_mask&((UINTN)1<<i))TextHex(t,c->interface_status[i],16);else TextPut(t,"null");
        TextPut(t,",\"interface\":");TextHex(t,c->interfaces[i],16);
        TextPut(t,",\"revalidated_status\":");if(c->revalidated_mask&((UINTN)1<<i))TextHex(t,c->revalidated_status[i],16);else TextPut(t,"null");
        TextPut(t,",\"revalidated_interface\":");if(c->revalidated_mask&((UINTN)1<<i))TextHex(t,c->revalidated_interfaces[i],16);else TextPut(t,"null");
        TextPut(t,",\"ram_read\":");TextPut(t,c->interface_read[i]?"true":"false");
        TextPut(t,",\"two_reads_equal\":");TextPut(t,c->interface_equal[i]?"true":"false");
        TextPut(t,",\"raw_words\":[");if(c->interface_read[i])for(UINTN j=0;j<c->word_count[i];j++){if(j)TextPut(t,",");TextHex(t,c->words[0][i][j],16);}TextPut(t,"]");
        TextPut(t,",\"second_raw_words\":[");if(c->interface_masks[1]&((UINTN)1<<i))for(UINTN j=0;j<c->word_count[i];j++){if(j)TextPut(t,",");TextHex(t,c->words[1][i][j],16);}TextPut(t,"]");
        TextPut(t,",\"public_wait_token\":");if(i<2&&c->interface_read[i])TextHex(t,c->words[0][i][2],16);else TextPut(t,"null");
        TextPut(t,",\"pointer_layout_decoded\":false}");
    }
    TextPut(t,"],\"interfaces_current_known\":false,\"heap_lists_followed\":false,\"event_objects_dereferenced\":false,\"event_contexts_known\":false,\"pending_events_known\":false,\"callbacks_invoked\":false,\"atomic_snapshot\":false,\"release_authorized\":false}");
}
