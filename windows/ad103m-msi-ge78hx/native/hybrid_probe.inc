/* SPDX-License-Identifier: MIT
 * Included only in the isolated, diagnostic-source hybrid build. No detach,
 * power transitions, GPU writes, firmware, DMA or unlock library is linked. */
#include "hybrid_policy.h"
typedef struct {
    EFI_HANDLE handle;EFI_PCI_IO_PROTOCOL *pci;UINT32 config[64];
    EFI_HANDLE gop_handle;EFI_GRAPHICS_OUTPUT_PROTOCOL *gop;
    EFI_GRAPHICS_OUTPUT_PROTOCOL_MODE mode;EFI_GRAPHICS_OUTPUT_MODE_INFORMATION info;
    EFI_HANDLE console;EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL *conout;
    BOOLEAN pci_valid,gop_valid,complete;
} HYBRID_INTEL;
static HYBRID_INTEL HybridIntel;
static BOOLEAN HybridMmioAllowed;
static void HybridBegin(EFI_SYSTEM_TABLE *st,TEXT *t) {
    UINTN intel_count=0,nvidia_count=0;DISPLAY *intel=NULL,*nv=NULL;
    EFI_HANDLE *handles=NULL;UINTN count=0;EFI_STATUS status;
    HYBRID_INTEL empty={0};HybridIntel=empty;HybridMmioAllowed=FALSE;
    HybridIntel.console=st->ConsoleOutHandle;HybridIntel.conout=st->ConOut;
    for(UINTN i=0;i<DisplayCount;i++) {
        DISPLAY *d=&Displays[i];
        if(Read32(d->config)==0xa7888086u&&Read32(d->config+44)==0x13cd1462u&&
           d->location_valid&&d->segment==0&&d->bus==0&&d->device==2&&d->function==0) {intel=d;intel_count++;}
        if(ExactTarget(d->config,64)){nv=d;nvidia_count++;}
    }
    TextPut(t,",\"hybrid_entry\":{\"route\":\"intel-preserved-diagnostic\",\"unlock_enabled\":false,\"intel_matches\":");TextDec(t,intel_count);
    TextPut(t,",\"nvidia_matches\":");TextDec(t,nvidia_count);
    TextPut(t,",\"nvidia_config256\":");
    if(EnumerationComplete&&nvidia_count==1&&nv) {
        UINT32 config[64];status=nv->pci->Pci.Read(nv->pci,EfiPciIoWidthUint32,0,64,config);
        if(!EFI_ERROR(status)&&BytesEqual(config,nv->config,64)) {
            HYBRID_PCI_STATE state=HybridPciState((UINT8 *)config);
            TextBytes(t,(UINT8 *)config,sizeof(config));
            TextPut(t,",\"pci_command\":");TextHex(t,state.command,4);
            TextPut(t,",\"pm_capability\":");TextHex(t,state.pm_offset,2);
            TextPut(t,",\"pmcsr\":");if(state.pm_present)TextHex(t,state.pmcsr,4);else TextPut(t,"null");
            HybridMmioAllowed=state.mmio_read_allowed;
            TextPut(t,",\"mmio_policy\":\"");TextPut(t,state.reason);TextPut(t,"\"");
        } else TextPut(t,"null");
    } else TextPut(t,"null");
    if(EnumerationComplete&&intel_count==1&&intel&&intel->path&&!intel->multi) {
        HybridIntel.handle=intel->handle;HybridIntel.pci=intel->pci;
        status=intel->pci->Pci.Read(intel->pci,EfiPciIoWidthUint32,0,64,HybridIntel.config);
        HybridIntel.pci_valid=(BOOLEAN)(!EFI_ERROR(status)&&BytesEqual(HybridIntel.config,intel->config,64));
        status=st->BootServices->LocateHandleBuffer(ByProtocol,&GopGuid,NULL,&count,&handles);
        BOOLEAN complete=(BOOLEAN)(!EFI_ERROR(status)&&count>0&&count<=MAX_GOP_HANDLES&&handles);
        UINTN matches=0;
        if(complete)for(UINTN i=0;i<count;i++) {
            UINTN length=0;BOOLEAN multi=FALSE;UINT8 *path=PathFor(st->BootServices,handles[i],&length,&multi);
            INTN parent=path&&!multi?ParentIndex(path,length):-1;
            if(parent<0){complete=FALSE;continue;}
            if(&Displays[parent]!=intel)continue;
            EFI_GRAPHICS_OUTPUT_PROTOCOL *g=NULL;matches++;
            status=st->BootServices->HandleProtocol(handles[i],&GopGuid,(VOID **)&g);
            if(EFI_ERROR(status)||!g||!g->Mode||!g->Mode->Info||g->Mode->SizeOfInfo<sizeof(HybridIntel.info)) {complete=FALSE;continue;}
            HybridIntel.gop_handle=handles[i];HybridIntel.gop=g;HybridIntel.mode=*g->Mode;HybridIntel.info=*g->Mode->Info;
        }
        if(handles)st->BootServices->FreePool(handles);
        HybridIntel.gop_valid=(BOOLEAN)(complete&&matches==1);
    }
    HybridIntel.complete=(BOOLEAN)(HybridIntel.pci_valid&&HybridIntel.gop_valid&&HybridIntel.console&&HybridIntel.conout);
    TextPut(t,",\"intel_config256\":");if(HybridIntel.pci_valid)TextBytes(t,(UINT8 *)HybridIntel.config,sizeof(HybridIntel.config));else TextPut(t,"null");
    TextPut(t,",\"intel_snapshot_complete\":");Bool(t,HybridIntel.complete);
    TextPut(t,",\"nvidia_mmio_read_allowed\":");Bool(t,HybridMmioAllowed);
    TextPut(t,",\"power_or_decode_writes\":false,\"detach_allowed\":false}");
}
static void HybridEnd(EFI_SYSTEM_TABLE *st,TEXT *t) {
    UINT32 config[64];EFI_GRAPHICS_OUTPUT_PROTOCOL *g=NULL;EFI_PCI_IO_PROTOCOL *pci=NULL;
    BOOLEAN config_equal=FALSE,gop_equal=FALSE,console_equal=FALSE;
    if(HybridIntel.complete) {
        EFI_STATUS s=st->BootServices->HandleProtocol(HybridIntel.handle,&PciGuid,(VOID **)&pci);
        if(!EFI_ERROR(s)&&pci==HybridIntel.pci) {
            s=pci->Pci.Read(pci,EfiPciIoWidthUint32,0,64,config);
            config_equal=(BOOLEAN)(!EFI_ERROR(s)&&BytesEqual(config,HybridIntel.config,sizeof(config)));
        }
        s=st->BootServices->HandleProtocol(HybridIntel.gop_handle,&GopGuid,(VOID **)&g);
        gop_equal=(BOOLEAN)(!EFI_ERROR(s)&&g==HybridIntel.gop&&g->Mode&&g->Mode->Info&&
            BytesEqual(g->Mode,&HybridIntel.mode,sizeof(HybridIntel.mode))&&BytesEqual(g->Mode->Info,&HybridIntel.info,sizeof(HybridIntel.info)));
        console_equal=(BOOLEAN)(st->ConsoleOutHandle==HybridIntel.console&&st->ConOut==HybridIntel.conout);
    }
    TextPut(t,",\"intel_preservation\":{\"snapshot_complete\":");Bool(t,HybridIntel.complete);
    TextPut(t,",\"pci_config_unchanged\":");Bool(t,config_equal);
    TextPut(t,",\"gop_interface_and_mode_unchanged\":");Bool(t,gop_equal);
    TextPut(t,",\"console_interface_unchanged\":");Bool(t,console_equal);
    TextPut(t,",\"observed_unchanged\":");Bool(t,(BOOLEAN)(HybridIntel.complete&&config_equal&&gop_equal&&console_equal));
    TextPut(t,",\"display_driver_callbacks_invoked\":false,\"unlock_authorized\":false}");
}
