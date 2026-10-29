/* SPDX-License-Identifier: GPL-2.0-only
 * Native transaction ownership for the observed MSI Hybrid topology.
 * The Intel device/GOP/console are observed, never stopped or reconnected.
 * Unlike T's temporary raw PCI command test, U uses managed PCI attributes
 * so the retained transaction's BME accounting sees coherent decode state. */
#include "hybrid_native.h"
#include "profile.h"
#include "checks.h"
#include "hybrid_capture_pin.h"
static struct {
    EFI_SYSTEM_TABLE *table;EFI_HANDLE image,target;
    UINT32 original[64],enabled[64];UINT64 attributes;
    BOOLEAN ready,claimed,decode_attempted,mutated,final_verified,finished;
} Native;
static BOOLEAN NativeConfig(const UINT32 c[64],const UINT32 expected[64]) {
    for(UINTN i=0;i<64;i++) {
        UINT32 drift=c[i]^expected[i];if(!drift)continue;
        /* The same exact PCIe sticky-status clearing accepted by the tested
         * dedicated path; no new bits and no general configuration mask. */
        if(Gu.flrs==1&&i==0x80/4&&expected[0x78/4]==0x0012b410&&
           !(c[i]&~expected[i])&&!(drift&~0x00090000u))continue;
        return FALSE;
    }
    return TRUE;
}
static EFI_STATUS NativeRead(UINT32 config[64]) {
    return Gu.pci->Pci.Read(Gu.pci,EfiPciIoWidthUint32,0,64,config);
}
static BOOLEAN NativeIntel(VOID) {
    UINT32 config[64];EFI_PCI_IO_PROTOCOL *pci=NULL;EFI_GRAPHICS_OUTPUT_PROTOCOL *g=NULL;
    EFI_SYSTEM_TABLE *t=Native.table;EFI_BOOT_SERVICES *bs=t->BootServices;
    if(!HybridIntel.complete||t->ConsoleOutHandle!=HybridIntel.console||t->ConOut!=HybridIntel.conout)return FALSE;
    EFI_STATUS s=bs->HandleProtocol(HybridIntel.handle,&PciGuid,(VOID **)&pci);
    if(s!=EFI_SUCCESS||pci!=HybridIntel.pci)return FALSE;
    s=pci->Pci.Read(pci,EfiPciIoWidthUint32,0,64,config);
    if(s!=EFI_SUCCESS||!BytesEqual(config,HybridIntel.config,sizeof(config)))return FALSE;
    s=bs->HandleProtocol(HybridIntel.gop_handle,&GopGuid,(VOID **)&g);
    if(s!=EFI_SUCCESS||g!=HybridIntel.gop||!g->Mode||!g->Mode->Info||
       !BytesEqual(g->Mode,&HybridIntel.mode,sizeof(HybridIntel.mode))||
       !BytesEqual(g->Mode->Info,&HybridIntel.info,sizeof(HybridIntel.info)))return FALSE;
    /* Re-enumerate to reject a new NVIDIA or ambiguous physical GOP. */
    EFI_HANDLE *handles=NULL;UINTN count=0;
    s=bs->LocateHandleBuffer(ByProtocol,&GopGuid,NULL,&count,&handles);
    BOOLEAN ok=(BOOLEAN)(s==EFI_SUCCESS&&count==1&&handles&&handles[0]==HybridIntel.gop_handle);
    if(handles&&bs->FreePool(handles)!=EFI_SUCCESS)ok=FALSE;
    return ok;
}
static BOOLEAN NativeConsumers(BOOLEAN claimed) {
    EFI_BOOT_SERVICES *bs=Native.table->BootServices;
    EFI_GUID **protocols=NULL;UINTN count=0,own=0;BOOLEAN ok=TRUE;
    EFI_STATUS s=bs->ProtocolsPerHandle(Native.target,&protocols,&count);
    if(s!=EFI_SUCCESS||!protocols||count==0||count>64){if(protocols)bs->FreePool(protocols);return FALSE;}
    for(UINTN i=0;i<count&&ok;i++) {
        EFI_OPEN_PROTOCOL_INFORMATION_ENTRY *entries=NULL;UINTN n=0;
        if(!protocols[i]){ok=FALSE;break;}
        s=bs->OpenProtocolInformation(Native.target,protocols[i],&entries,&n);
        if(s!=EFI_SUCCESS||n>64||(n&&!entries)){if(entries)bs->FreePool(entries);ok=FALSE;break;}
        for(UINTN j=0;j<n;j++) {
            EFI_OPEN_PROTOCOL_INFORMATION_ENTRY *e=&entries[j];
            /* UEFI 2.10 7.3.9: these exact modes look up an interface or test
             * its presence; they do not establish a driver/child/exclusive
             * ownership relationship. ControllerHandle may be non-NULL
             * (the specification's GET/TEST examples use the controller).
             * Do not accept combined flags or a malformed zero open count. */
            BOOLEAN passive=(BOOLEAN)(e->Attributes==EFI_OPEN_PROTOCOL_BY_HANDLE_PROTOCOL||
                e->Attributes==EFI_OPEN_PROTOCOL_GET_PROTOCOL||e->Attributes==EFI_OPEN_PROTOCOL_TEST_PROTOCOL);
            if(passive&&e->OpenCount>0)continue;
            if(claimed&&BytesEqual(protocols[i],&PciGuid,16)&&e->AgentHandle==Native.image&&
               e->ControllerHandle==Native.target&&e->Attributes==(EFI_OPEN_PROTOCOL_BY_DRIVER|EFI_OPEN_PROTOCOL_EXCLUSIVE)&&e->OpenCount==1)own++;
            else {
                GuLog("HYBRID NVIDIA consumer: protocol=%08x agent=%#llx controller=%#llx attributes=%#x opens=%u expected_claim=%u\n",protocols[i]->Data1,(unsigned long long)(UINTN)e->AgentHandle,(unsigned long long)(UINTN)e->ControllerHandle,e->Attributes,e->OpenCount,claimed);
                ok=FALSE;break;
            }
        }
        if(entries&&bs->FreePool(entries)!=EFI_SUCCESS)ok=FALSE;
    }
    if(bs->FreePool(protocols)!=EFI_SUCCESS)ok=FALSE;
    return (BOOLEAN)(ok&&own==(claimed?1u:0u));
}
static BOOLEAN NativeBar(VOID) {
    VOID *resource=NULL;UINT64 supports=0,length=0;
    EFI_STATUS s=Gu.pci->GetBarAttributes(Gu.pci,0,&supports,&resource);
    BOOLEAN ok=(BOOLEAN)(s==EFI_SUCCESS&&ResourceMatches(resource,0x5e000000,0x5e000000,&length)&&length==0x1000000);
    if(resource&&Gu.bs->FreePool(resource)!=EFI_SUCCESS)ok=FALSE;
    return ok;
}
EFI_STATUS GuHybridObserve(EFI_SYSTEM_TABLE *t,EFI_HANDLE image) {
    if(Native.table||!t||!image||!t->BootServices)return EFI_INVALID_PARAMETER;
    Native.table=t;Native.image=image;EFI_BOOT_SERVICES *bs=t->BootServices;
    if(!bs->ProtocolsPerHandle||!bs->OpenProtocolInformation||!bs->OpenProtocol||!bs->CloseProtocol)return EFI_UNSUPPORTED;
    TEXT report;TextInit(&report,Report,sizeof(Report));EnumerateDisplays(bs,&report);Gops(t,&report);HybridBegin(t,&report);
    UINTN count=0;DISPLAY *nv=NULL;
    for(UINTN i=0;i<DisplayCount;i++)if(ExactTarget(Displays[i].config,64)){count++;nv=&Displays[i];}
    GuLog("HYBRID inventory: complete=%u targets=%zu Intel_snapshot=%u report_failed=%u\n",EnumerationComplete,(size_t)count,HybridIntel.complete,report.failed);
    if(Gu.fault)return EFI_DEVICE_ERROR;
    if(report.failed||!EnumerationComplete||count!=1||!nv||!nv->location_valid||nv->segment||nv->bus!=1||nv->device||nv->function||
       !HybridIntel.complete||!nv->pci->Attributes||!nv->pci->GetBarAttributes||!nv->pci->Pci.Write)return EFI_UNSUPPORTED;
    GuInit(bs,nv->pci);Gu.image=image;Gu.handle=nv->handle;Gu.bar_base=GU_BAR0_BASE;Gu.bar_size=0x1000000;
    Native.target=nv->handle;
    EFI_STATUS s=NativeRead(Native.original);if(s!=EFI_SUCCESS)return s;
    if(!BytesEqual(Native.original,HybridCapturedHeader,64))return EFI_UNSUPPORTED;
    HYBRID_PCI_STATE state=HybridPciState((UINT8 *)Native.original);
    GuLog("HYBRID PCI: command=%#x pmcsr=%#x pm=%#x exact=%u caps=%u reason=%s\n",state.command,state.pmcsr,state.pm_offset,state.exact,state.capabilities_valid,state.reason);
    if(!state.exact||!state.capabilities_valid||!state.pm_present||(state.pmcsr&3)||state.command!=0)return EFI_NOT_READY;
    for(UINTN i=0;i<64;i++)Native.enabled[i]=Native.original[i];Native.enabled[1]|=2;
    GU_CAPS caps;
    if(!GuCheckConfig(Native.enabled,0,1,0,0,&caps))return EFI_UNSUPPORTED;
    Gu.pm_cap=caps.pm;Gu.pcie_cap=caps.pcie;Gu.pcie_version=caps.pcie_version;Gu.msi_cap=caps.msi;Gu.msix_cap=caps.msix;
    s=Gu.pci->Attributes(Gu.pci,EfiPciIoAttributeOperationGet,0,&Native.attributes);if(s!=EFI_SUCCESS)return s;
    BOOLEAN attributes_ok=(BOOLEAN)((Native.attributes&EFI_PCI_DEVICE_ENABLE)==0);
    BOOLEAN bar_ok=NativeBar(),intel_ok=NativeIntel(),consumers_ok=NativeConsumers(FALSE);
    GuLog("HYBRID gates: attributes=%#llx decode_bits=%#llx attributes_ok=%u BAR0_ok=%u Intel_ok=%u NVIDIA_consumers_ok=%u\n",(unsigned long long)Native.attributes,(unsigned long long)(Native.attributes&EFI_PCI_DEVICE_ENABLE),attributes_ok,bar_ok,intel_ok,consumers_ok);
    if(!attributes_ok||!bar_ok||!intel_ok||!consumers_ok)return EFI_NOT_READY;
    Native.ready=TRUE;
    HybridEnd(t,&report);if(report.failed)return EFI_BUFFER_TOO_SMALL;
    GuLog("HYBRID U6 preflight: Intel 8086:a788 GOP/console preserved; NVIDIA 2717 unbound D0 command=0 attributes=%#llx; passive BY_HANDLE/GET/TEST permitted, active consumers refused; no mutation\n",(unsigned long long)Native.attributes);
    return Gu.fault?EFI_DEVICE_ERROR:EFI_SUCCESS;
}
EFI_STATUS GuHybridAcquire(VOID) {
    UINT32 config[64];UINT64 supported=0,attributes=0;EFI_PCI_IO_PROTOCOL *pci=NULL;
    if(!Native.ready||Native.mutated||Gu.owned||Gu.started||Gu.armed||Gu.fault)return EFI_NOT_READY;
    if(!NativeIntel()||!NativeConsumers(FALSE)||NativeRead(config)!=EFI_SUCCESS||!NativeConfig(config,Native.original))return EFI_NOT_READY;
    EFI_STATUS s=Gu.pci->Attributes(Gu.pci,EfiPciIoAttributeOperationSupported,0,&supported);
    if(s!=EFI_SUCCESS||(supported&(EFI_PCI_IO_ATTRIBUTE_MEMORY|EFI_PCI_IO_ATTRIBUTE_BUS_MASTER))!=(EFI_PCI_IO_ATTRIBUTE_MEMORY|EFI_PCI_IO_ATTRIBUTE_BUS_MASTER))return EFI_UNSUPPORTED;
    s=Gu.bs->OpenProtocol(Native.target,&PciGuid,(VOID **)&pci,Native.image,Native.target,EFI_OPEN_PROTOCOL_BY_DRIVER|EFI_OPEN_PROTOCOL_EXCLUSIVE);
    if(s!=EFI_SUCCESS)return s;
    Native.claimed=TRUE;Gu.owned=TRUE;Native.mutated=TRUE;
    if(pci!=Gu.pci||!NativeConsumers(TRUE)||!NativeIntel()||NativeRead(config)!=EFI_SUCCESS||!NativeConfig(config,Native.original))return EFI_NOT_READY;
    s=pci->Attributes(pci,EfiPciIoAttributeOperationGet,0,&attributes);
    if(s!=EFI_SUCCESS||attributes!=Native.attributes)return EFI_NOT_READY;
    /* Record before the API call, including errors after a partial change. */
    Native.decode_attempted=TRUE;
    s=pci->Attributes(pci,EfiPciIoAttributeOperationEnable,EFI_PCI_IO_ATTRIBUTE_MEMORY,NULL);
    if(s!=EFI_SUCCESS)return s;
    if(!GuHybridVerify())return EFI_NOT_READY;
    GuLog("HYBRID acquire OK: exclusive NVIDIA, managed memory decode=on; Intel GOP untouched; no driver disconnect\n");
    return Gu.fault?EFI_DEVICE_ERROR:EFI_SUCCESS;
}
BOOLEAN GuHybridVerify(VOID) {
    UINT32 config[64];UINT64 attributes=0;
    return (BOOLEAN)(Native.ready&&Native.claimed&&Native.decode_attempted&&!Native.finished&&Gu.owned&&!Gu.fault&&
        NativeRead(config)==EFI_SUCCESS&&NativeConfig(config,Native.enabled)&&
        Gu.pci->Attributes(Gu.pci,EfiPciIoAttributeOperationGet,0,&attributes)==EFI_SUCCESS&&
        attributes==(Native.attributes|EFI_PCI_IO_ATTRIBUTE_MEMORY)&&NativeBar()&&NativeIntel()&&NativeConsumers(TRUE));
}
static EFI_STATUS NativeRestore(VOID) {
    UINT32 config[64];UINT64 attributes=0;
    if(Native.decode_attempted) {
        EFI_STATUS s=Gu.pci->Attributes(Gu.pci,EfiPciIoAttributeOperationDisable,EFI_PCI_IO_ATTRIBUTE_MEMORY,NULL);
        EFI_STATUS read=NativeRead(config);
        EFI_STATUS attrs=Gu.pci->Attributes(Gu.pci,EfiPciIoAttributeOperationGet,0,&attributes);
        if(s!=EFI_SUCCESS||read!=EFI_SUCCESS||attrs!=EFI_SUCCESS||attributes!=Native.attributes||!NativeConfig(config,Native.original))return EFI_DEVICE_ERROR;
        Native.decode_attempted=FALSE;
    }
    if(Native.claimed) {
        EFI_STATUS s=Gu.bs->CloseProtocol(Native.target,&PciGuid,Native.image,Native.target);
        if(s!=EFI_SUCCESS)return s;
        Native.claimed=FALSE;Gu.owned=FALSE;
    }
    if(!NativeIntel()||!NativeConsumers(FALSE))return EFI_NOT_READY;
    return EFI_SUCCESS;
}
EFI_STATUS GuHybridAbort(VOID) {
    if(!Native.mutated)return EFI_SUCCESS;
    if(Gu.started||Gu.mmio_writes||!GuSafeToReturn())return EFI_ACCESS_DENIED;
    return NativeRestore();
}
EFI_STATUS GuHybridFinish(VOID) {
    if(!GuSafeToReturn()||!Gu.quiesced||Gu.flrs!=1||!GuHybridVerify())return EFI_NOT_READY;
    UINT32 chip=GuRead32(0),cpu=GuRead32(0x110100),hi=GuRead32(0x1fa828);
    UINT32 ss0=GuRead32(0x82381c),ss1=GuRead32(0x823820),rate=GuRead32(0x823818),plm=GuRead32(0x823804);
    if(Gu.fault||chip!=0x193000a1||cpu!=0x10||(hi>>4)||ss0!=0x88888888||ss1!=8||rate!=0||plm!=0xffffffff)return EFI_DEVICE_ERROR;
    Native.final_verified=TRUE;
    GuLog("HYBRID post-core: selector/readout full, FWSEC/payload/FLR complete; Intel PCI/GOP/console unchanged; keeping NVIDIA memory decode ON, BME OFF\n");
    if(Gu.fault)return EFI_DEVICE_ERROR;
    /* U6 changes only the successful handoff: leave managed MEMORY enabled.
     * The core has already cleared BME and released every DMA allocation.
     * Pre-firmware rollback still uses NativeRestore and restores decode OFF. */
    EFI_STATUS s=Gu.bs->CloseProtocol(Native.target,&PciGuid,Native.image,Native.target);
    if(s!=EFI_SUCCESS)return s;
    Native.claimed=FALSE;Gu.owned=FALSE;
    Native.finished=TRUE;
    if(!GuHybridReadyForWindows())return EFI_NOT_READY;
    GuLog("HYBRID finish OK: NVIDIA command=2 managed MEMORY=on BME=off, exclusive claim closed, Intel unchanged; Windows benchmark required\n");
    return Gu.fault?EFI_DEVICE_ERROR:EFI_SUCCESS;
}
BOOLEAN GuHybridReadyForWindows(VOID) {
    UINT32 config[64];UINT64 attributes=0;
    return (BOOLEAN)(Native.finished&&Native.final_verified&&!Native.claimed&&Native.decode_attempted&&!Gu.owned&&!Gu.fault&&
        Gu.quiesced&&Gu.flrs==1&&GuSafeToReturn()&&NativeIntel()&&NativeConsumers(FALSE)&&
        NativeRead(config)==EFI_SUCCESS&&NativeConfig(config,Native.enabled)&&NativeBar()&&
        Gu.pci->Attributes(Gu.pci,EfiPciIoAttributeOperationGet,0,&attributes)==EFI_SUCCESS&&attributes==(Native.attributes|EFI_PCI_IO_ATTRIBUTE_MEMORY));
}
BOOLEAN GuHybridMutated(VOID){return Native.mutated;}
