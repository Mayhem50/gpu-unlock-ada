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
static const UINT8 ConsoleFile[]={4,6,20,0,0x7d,0x49,0x8a,0x62,0xf6,0x2b,0x64,0x42,0x87,0x41,6,0x9d,0xbd,0x33,0x99,0xd6,127,255,4,0};
#ifdef CS_REAL_PROFILE
static const UINT8 CodeHash[]={0xc1,0x4e,0x51,0x70,0xfd,0xa8,0xbd,0x25,0x29,0x02,0x8c,0x82,0x48,0xd7,0xe6,0xcf,0x95,0x18,0xcb,0xac,0x49,0x8a,0xf7,0xf6,0x9f,0xd6,0xcd,0xc8,0xe9,0x92,0x97,0x66};
#else
/* Filled from the deterministic byte fixture, independently SHA256 checked. */
static const UINT8 CodeHash[32]={0x99,0x5e,0xaa,0xde,0xf1,0xb7,0x4e,0xce,0xf7,0x67,0x4b,0xda,0x35,0x86,0xfc,0x40,0x9f,0xad,0xdd,0xdb,0x9f,0x64,0x1e,0xb4,0x42,0x5b,0x7c,0x8f,0x48,0xb4,0xec,0x65};
#endif
static void CopyRead(UINT8 *out,UINTN address,UINTN bytes) {
    const volatile UINT8 *p=(const volatile UINT8 *)address;
    for(UINTN i=0;i<bytes;i++)out[i]=p[i];
}
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
static BOOLEAN Readable(CS_CAPTURE *c,UINTN p,UINTN n) {
    return CsReadable(c->maps[0].bytes,c->sizes[0],c->strides[0],c->versions[0],p,n);
}
static BOOLEAN Map(CS_CAPTURE *c,EFI_BOOT_SERVICES *bs,unsigned i) {
    EFI_STATUS s;c->sizes[i]=CS_MAP_CAP;c->strides[i]=0;c->versions[i]=0;c->keys[i]=0;
    c->map_called[i]=TRUE;s=bs->GetMemoryMap(&c->sizes[i],(EFI_MEMORY_DESCRIPTOR *)c->maps[i].bytes,&c->keys[i],&c->strides[i],&c->versions[i]);c->map_status[i]=s;
    return !c->violation&&s==EFI_SUCCESS&&c->sizes[i]<=CS_MAP_CAP&&c->strides[i]>=sizeof(EFI_MEMORY_DESCRIPTOR)&&
           c->strides[i]<=256&&c->sizes[i]&&c->sizes[i]%c->strides[i]==0&&c->versions[i]==EFI_MEMORY_DESCRIPTOR_VERSION;
}
static BOOLEAN Binding(CS_CAPTURE *c,EFI_SYSTEM_TABLE *st) {
    static const UINTN out_rva[]={0x4188,0x4218,0x43dc,0x444c,0x44a8,0x4590,0x461c,0x4694,0x472c};
    UINTN words[9];
    if(c->roots[0]!=st->ConsoleInHandle||c->roots[1]!=st->ConsoleOutHandle||c->roots[2]!=st->StandardErrorHandle||
       !c->interfaces[0]||c->interfaces[0]!=(UINTN)st->ConIn||
       c->outputs[0]!=(UINTN)st->ConOut||c->outputs[1]!=(UINTN)st->StdErr||
       !Readable(c,c->interfaces[0],sizeof(EFI_SIMPLE_TEXT_INPUT_PROTOCOL)))return FALSE;
    CopyRead((UINT8 *)words,c->interfaces[0],16);
    if(words[0]!=c->image_base+0x3230||words[1]!=c->image_base+0x32d4)return FALSE;
    for(unsigned i=0;i<2;i++) {
        if(!Readable(c,c->outputs[i],sizeof(EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL)))return FALSE;
        CopyRead((UINT8 *)words,c->outputs[i],sizeof(words));
        for(unsigned j=0;j<9;j++)if(words[j]!=c->image_base+out_rva[j])return FALSE;
    }
    return TRUE;
}
void CsCapture(CS_CAPTURE *c,EFI_SYSTEM_TABLE *st,EFI_HANDLE *handles,UINTN count) {
    EFI_GUID loaded_id=EFI_LOADED_IMAGE_PROTOCOL_GUID,out_id=EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL_GUID;
    EFI_GUID ids[4]={EFI_SIMPLE_TEXT_INPUT_PROTOCOL_GUID,EFI_SIMPLE_TEXT_INPUT_EX_PROTOCOL_GUID,
        {0x31878c87,0x0b75,0x11d5,{0x9a,0x4f,0,0x90,0x27,0x3f,0xc1,0x4d}},
        {0x8d59d32b,0xc655,0x4ae9,{0x9b,0x15,0xf2,0x59,4,0x99,0x2a,0x43}}};
    EFI_LOADED_IMAGE_PROTOCOL candidates[4];EFI_HANDLE candidate_handles[4];UINTN candidates_n=0,selected=MAX_UINTN;
    EFI_BOOT_SERVICES *bs=st?st->BootServices:NULL;UINT8 hash[32];CODE_VIEW view;
    if(!c)return;
    if(c->entered){c->violation=TRUE;c->slots_complete=FALSE;c->reason="reentry_refused";return;}
    c->entered=TRUE;c->reason="required_services_missing";
    if(!bs||!bs->GetMemoryMap||!bs->HandleProtocol||!handles||!count||count>4096)return;
    c->roots[0]=st->ConsoleInHandle;c->roots[1]=st->ConsoleOutHandle;c->roots[2]=st->StandardErrorHandle;
    for(UINTN i=0;i<count;i++) {
        EFI_LOADED_IMAGE_PROTOCOL *image=NULL;EFI_STATUS s=bs->HandleProtocol(handles[i],&loaded_id,(VOID **)&image);
        if(c->violation)return;
        if(s==EFI_UNSUPPORTED)continue;
        if(s!=EFI_SUCCESS||!image){c->reason="image_enumeration_failed";return;}
        if(image->ImageSize==24256&&image->ImageCodeType==EfiBootServicesCode) {
            if(candidates_n==4){c->reason="candidate_limit";return;}
            candidates[candidates_n]=*image;candidate_handles[candidates_n++]=handles[i];
        }
    }
    for(UINTN i=0;i<4;i++) {
        VOID *p=NULL;c->interface_status[i]=bs->HandleProtocol(c->roots[0],&ids[i],&p);
        c->queried_mask|=(UINTN)1<<i;
        if(c->violation)return;
        c->interfaces[i]=c->interface_status[i]==EFI_SUCCESS?(UINTN)p:0;
        c->word_count[i]=i==0?3:i==1?6:4;
    }
    if(!c->interfaces[0]||c->interfaces[0]!=(UINTN)st->ConIn){c->reason="system_input_binding_mismatch";return;}
    for(unsigned i=0;i<2;i++) {
        VOID *p=NULL;EFI_HANDLE h=c->roots[i?2:1];
        EFI_STATUS s=bs->HandleProtocol(h,&out_id,&p);
        if(c->violation)return;
        if(s!=EFI_SUCCESS||!p||p!=(i?(VOID *)st->StdErr:(VOID *)st->ConOut)){c->reason="system_output_binding_mismatch";return;}
        c->outputs[i]=(UINTN)p;
    }
    if(!Map(c,bs,0)){c->reason="memory_map_unavailable";return;}
    for(UINTN i=0;i<candidates_n;i++) {
        EFI_LOADED_IMAGE_PROTOCOL *im=&candidates[i];UINTN base=(UINTN)im->ImageBase;
        c->candidate_count++;
        if(!Readable(c,base,24256))continue;c->ram_candidates++;
        if(!Readable(c,(UINTN)im->FilePath,sizeof(ConsoleFile))||!BytesEqual(im->FilePath,ConsoleFile,sizeof(ConsoleFile)))continue;c->path_matches++;
        if(!LoadedCodeView((UINT8 *)base,24256,&view)||view.rva!=0x2a0||view.bytes!=19648||view.relocations)continue;
        Sha256((UINT8 *)base+view.rva,view.bytes,hash);if(!BytesEqual(hash,CodeHash,32))continue;
        c->code_matches++;if(selected!=MAX_UINTN){c->reason="ambiguous_console_image";return;}selected=i;
    }
    if(selected==MAX_UINTN){c->reason="console_image_not_pinned";return;}
    c->image_base=(UINTN)candidates[selected].ImageBase;c->image_bytes=24256;c->image_handle=candidate_handles[selected];c->pinned=TRUE;
    if(!Binding(c,st)){c->reason="active_console_methods_mismatch";return;}c->binding_verified=TRUE;
    for(unsigned pass=0;pass<2;pass++) {
        for(UINTN i=0;i<CS_SLOT_COUNT;i++) {
            const CS_SLOT *slot=&CsSlots[i];
            if(slot->rva>24256u-slot->bytes||!Readable(c,c->image_base+slot->rva,slot->bytes)){c->reason="slot_unreadable";return;}
            CopyRead(c->slots[pass][i],c->image_base+slot->rva,slot->bytes);
            c->slot_masks[pass]|=(UINTN)1<<i;
        }
        for(UINTN i=0;i<4;i++) {
            if(c->interfaces[i]&&Readable(c,c->interfaces[i],c->word_count[i]*8)) {
                CopyRead((UINT8 *)c->words[pass][i],c->interfaces[i],c->word_count[i]*8);c->interface_read[i]=TRUE;
                c->interface_masks[pass]|=(UINTN)1<<i;
            }
        }
    }
    c->slots_equal=BytesEqual(c->slots[0],c->slots[1],sizeof(c->slots[0]));
    for(UINTN i=0;i<4;i++)c->interface_equal[i]=(BOOLEAN)((c->interface_masks[0]&((UINTN)1<<i))&&(c->interface_masks[1]&((UINTN)1<<i))&&BytesEqual(c->words[0][i],c->words[1][i],c->word_count[i]*8));
    Sha256((UINT8 *)c->image_base+0x2a0,19648,hash);
    if(!BytesEqual(hash,CodeHash,32)){c->reason="console_code_changed";return;}
    /* Re-query all optional protocols before the final map. A failed query,
     * replacement, removal or new appearance invalidates the observation. */
    for(UINTN i=0;i<4;i++) {
        VOID *p=NULL;EFI_STATUS s=bs->HandleProtocol(c->roots[0],&ids[i],&p);
        c->revalidated_mask|=(UINTN)1<<i;c->revalidated_status[i]=s;
        c->revalidated_interfaces[i]=s==EFI_SUCCESS?(UINTN)p:0;
        if(c->violation){c->reason="reentry_refused";return;}
        if(s!=c->interface_status[i] || (s==EFI_SUCCESS&&(UINTN)p!=c->interfaces[i])) {
            c->reason="input_protocol_changed";return;
        }
    }
    c->interfaces_revalidated=TRUE;
    if(!Map(c,bs,1)){c->reason="final_memory_map_failed";return;}
    c->map_equal=(BOOLEAN)(c->keys[0]==c->keys[1]&&c->sizes[0]==c->sizes[1]&&c->strides[0]==c->strides[1]&&
        BytesEqual(c->maps[0].bytes,c->maps[1].bytes,c->sizes[0]));
    if(!c->map_equal){c->reason="memory_map_changed";return;}
    if(!Binding(c,st)){c->binding_verified=FALSE;c->reason="active_console_binding_changed";return;}
    Sha256((UINT8 *)c->image_base+0x2a0,19648,hash);
    if(!BytesEqual(hash,CodeHash,32)){c->reason="console_code_changed";return;}
    c->final_equal=TRUE;
    for(UINTN i=0;i<CS_SLOT_COUNT;i++) {
        UINT8 last[8];CopyRead(last,c->image_base+CsSlots[i].rva,CsSlots[i].bytes);
        if(!BytesEqual(last,c->slots[1][i],CsSlots[i].bytes))c->final_equal=FALSE;
    }
    for(UINTN i=0;i<4;i++)if(c->interface_read[i]) {
        UINT8 last[48];CopyRead(last,c->interfaces[i],c->word_count[i]*8);
        if(!BytesEqual(last,c->words[1][i],c->word_count[i]*8))c->interface_equal[i]=FALSE;
    }
    c->slots_complete=(BOOLEAN)(c->map_equal&&c->slots_equal&&c->final_equal&&!c->violation);
    c->reason=c->slots_complete?"observed_only":!c->map_equal?"memory_map_changed":"state_changed";
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
    TextPut(t,"],\"heap_lists_followed\":false,\"event_objects_dereferenced\":false,\"event_contexts_known\":false,\"pending_events_known\":false,\"callbacks_invoked\":false,\"atomic_snapshot\":false,\"release_authorized\":false}");
}
