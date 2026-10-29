/* SPDX-License-Identifier: MIT
 * T is an explicitly selected, reversible PCI-command test, not an unlock.
 * Only two 16-bit PCI command writes exist: memory decode on, original off.
 * No Attributes/ACPI calls, BME, reset, detach, DMA or MMIO writes. */
#include "hybrid_capture_pin.h"
static BOOLEAN HybridDecodeRequested;
static struct {
    EFI_PCI_IO_PROTOCOL *pci;EFI_HANDLE handle,image;
    UINT32 config[64];UINT16 command;
    BOOLEAN claimed,write_attempted,enabled,restored,closed;
    EFI_STATUS begin_status,restore_status,close_status;
    const CHAR8 *reason;
} Decode;
VOID HybridRequestDecodeTest(VOID){HybridDecodeRequested=TRUE;}
static BOOLEAN HybridIntelStable(EFI_SYSTEM_TABLE *st) {
    UINT32 c[64];EFI_GRAPHICS_OUTPUT_PROTOCOL *g=NULL;EFI_PCI_IO_PROTOCOL *pci=NULL;
    if(!HybridIntel.complete||st->ConsoleOutHandle!=HybridIntel.console||st->ConOut!=HybridIntel.conout)return FALSE;
    EFI_STATUS s=st->BootServices->HandleProtocol(HybridIntel.handle,&PciGuid,(VOID **)&pci);
    if(EFI_ERROR(s)||pci!=HybridIntel.pci)return FALSE;
    s=pci->Pci.Read(pci,EfiPciIoWidthUint32,0,64,c);
    if(EFI_ERROR(s)||!BytesEqual(c,HybridIntel.config,sizeof(c)))return FALSE;
    s=st->BootServices->HandleProtocol(HybridIntel.gop_handle,&GopGuid,(VOID **)&g);
    return (BOOLEAN)(!EFI_ERROR(s)&&g==HybridIntel.gop&&g->Mode&&g->Mode->Info&&
        BytesEqual(g->Mode,&HybridIntel.mode,sizeof(HybridIntel.mode))&&BytesEqual(g->Mode->Info,&HybridIntel.info,sizeof(HybridIntel.info)));
}
static EFI_STATUS HybridDecodeStart(EFI_SYSTEM_TABLE *st,EFI_HANDLE image) {
    UINTN targets=0;DISPLAY *nv=NULL;EFI_BOOT_SERVICES *bs=st->BootServices;
    Decode.reason="not_requested";Decode.begin_status=EFI_NOT_STARTED;
    if(!HybridDecodeRequested)return EFI_NOT_STARTED;
    HybridMmioAllowed=FALSE;Decode.reason="intel_not_stable";
    if(!EnumerationComplete||!HybridIntelStable(st))return EFI_NOT_READY;
    for(UINTN i=0;i<DisplayCount;i++)if(ExactTarget(Displays[i].config,64)){nv=&Displays[i];targets++;}
    Decode.reason="target_or_protocol_unavailable";
    if(targets!=1||!nv||!nv->location_valid||nv->segment||nv->bus!=1||nv->device||nv->function||
       !nv->pci->Pci.Write||!bs->OpenProtocol||!bs->CloseProtocol)return EFI_UNSUPPORTED;
    Decode.pci=nv->pci;Decode.handle=nv->handle;Decode.image=image;
    EFI_STATUS s=nv->pci->Pci.Read(nv->pci,EfiPciIoWidthUint32,0,64,Decode.config);
    Decode.reason="capture2_profile_mismatch";
    /* Exact captured fixed PCI header, including disabled command and BARs.
     * Later config bytes are latched and must be restored, not guessed. */
    if(EFI_ERROR(s)||!BytesEqual(Decode.config,HybridCapturedHeader,64))return EFI_UNSUPPORTED;
    HYBRID_PCI_STATE policy=HybridPciState((UINT8 *)Decode.config);
    if(!policy.exact||!policy.capabilities_valid||!policy.pm_present||(policy.pmcsr&3)||policy.command!=0)return EFI_NOT_READY;
    UINT64 supports=0,length=0;VOID *resource=NULL;
    s=nv->pci->GetBarAttributes?nv->pci->GetBarAttributes(nv->pci,0,&supports,&resource):EFI_UNSUPPORTED;
    BOOLEAN bar=(BOOLEAN)(!EFI_ERROR(s)&&ResourceMatches(resource,0x5e000000,0x5e000000,&length)&&length==0x1000000);
    if(resource)bs->FreePool(resource);
    Decode.reason="bar0_resource_mismatch";if(!bar)return EFI_UNSUPPORTED;
    EFI_PCI_IO_PROTOCOL *claimed=NULL;Decode.reason="exclusive_claim_refused_no_detach";
    s=bs->OpenProtocol(nv->handle,&PciGuid,(VOID **)&claimed,image,nv->handle,EFI_OPEN_PROTOCOL_BY_DRIVER|EFI_OPEN_PROTOCOL_EXCLUSIVE);
    if(EFI_ERROR(s))return s;
    Decode.claimed=TRUE;
    if(claimed!=nv->pci)return EFI_DEVICE_ERROR;
    UINT32 current[64];s=claimed->Pci.Read(claimed,EfiPciIoWidthUint32,0,64,current);
    Decode.reason="prewrite_state_drift";
    if(EFI_ERROR(s)||!BytesEqual(current,Decode.config,sizeof(current))||!HybridIntelStable(st))return EFI_NOT_READY;
    Decode.command=2;Decode.write_attempted=TRUE;Decode.reason="memory_decode_write_failed";
    s=claimed->Pci.Write(claimed,EfiPciIoWidthUint16,4,1,&Decode.command);
    if(EFI_ERROR(s))return s;
    s=claimed->Pci.Read(claimed,EfiPciIoWidthUint32,0,64,current);
    Decode.reason="enabled_readback_or_intel_drift";
    UINT32 expected[64];for(UINTN i=0;i<64;i++)expected[i]=Decode.config[i];expected[1]|=2;
    if(EFI_ERROR(s)||!BytesEqual(current,expected,sizeof(current))||!HybridIntelStable(st))return EFI_DEVICE_ERROR;
    Decode.enabled=TRUE;HybridMmioAllowed=TRUE;Decode.reason="memory_decode_enabled_for_read_only_sampling";
    return EFI_SUCCESS;
}
static void HybridDecodeBegin(EFI_SYSTEM_TABLE *st,EFI_HANDLE image,TEXT *t) {
    /* Firmware loads a fresh application for each test; initialize all state. */
    Decode.pci=NULL;Decode.handle=NULL;Decode.claimed=Decode.write_attempted=Decode.enabled=Decode.restored=Decode.closed=FALSE;
    Decode.restore_status=Decode.close_status=EFI_NOT_STARTED;
    Decode.begin_status=HybridDecodeStart(st,image);
    TextPut(t,",\"decode_test_entry\":{\"requested\":");Bool(t,HybridDecodeRequested);
    TextPut(t,",\"status\":");Status(t,Decode.begin_status);
    TextPut(t,",\"reason\":\"");TextPut(t,Decode.reason);TextPut(t,"\",\"pci_command_write_attempted\":");Bool(t,Decode.write_attempted);
    TextPut(t,",\"memory_decode_enabled\":");Bool(t,Decode.enabled);
    TextPut(t,",\"gpu_mmio_writes\":false,\"firmware_started\":false,\"unlock_attempted\":false}");
}
static void HybridDecodeEnd(EFI_SYSTEM_TABLE *st,TEXT *t) {
    if(Decode.write_attempted) {
        /* Always attempt restore even if the enable call reported failure. */
        UINT16 command=Read16((UINT8 *)Decode.config+4);UINT32 current[64];
        Decode.restore_status=Decode.pci->Pci.Write(Decode.pci,EfiPciIoWidthUint16,4,1,&command);
        EFI_STATUS s=Decode.pci->Pci.Read(Decode.pci,EfiPciIoWidthUint32,0,64,current);
        Decode.restored=(BOOLEAN)(!EFI_ERROR(Decode.restore_status)&&!EFI_ERROR(s)&&BytesEqual(current,Decode.config,sizeof(current)));
    }
    if(Decode.claimed) {
        Decode.close_status=st->BootServices->CloseProtocol(Decode.handle,&PciGuid,Decode.image,Decode.handle);
        Decode.closed=(BOOLEAN)!EFI_ERROR(Decode.close_status);
    }
    TextPut(t,",\"decode_test_cleanup\":{\"restore_status\":");Status(t,Decode.restore_status);
    TextPut(t,",\"original_config_restored\":");Bool(t,Decode.restored);
    TextPut(t,",\"exclusive_claim_closed\":");Bool(t,Decode.closed);
    TextPut(t,",\"intel_still_unchanged\":");Bool(t,HybridIntelStable(st));
    TextPut(t,",\"safe_to_return\":");Bool(t,(BOOLEAN)((!Decode.write_attempted||Decode.restored)&&(!Decode.claimed||Decode.closed)));
    TextPut(t,"}");
}
static BOOLEAN HybridDecodeSafe(VOID){return (BOOLEAN)((!Decode.write_attempted||Decode.restored)&&(!Decode.claimed||Decode.closed));}
static void HybridDecodeContain(EFI_SYSTEM_TABLE *st) {
    if(HybridDecodeSafe())return;
    Message(st,L"\r\nPCI restoration not verified. Stop. Power off; preserve USB log.\r\n");
    for(;;)st->BootServices->Stall(1000000);
}
