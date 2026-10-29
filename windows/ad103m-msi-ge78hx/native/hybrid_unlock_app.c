/* SPDX-License-Identifier: GPL-2.0-only */
#include "port.h"
#include "profile.h"
#include "boot_io.h"
#include "hybrid_native.h"
#include "compat/compat.h"
#include "vendor/unlock_610.h"
static EFI_GUID GlobalGuid={0x8be4df61,0x93ca,0x11d2,{0xaa,0x0d,0,0xe0,0x98,0x03,0x2b,0x8c}};
EFI_STATUS EFIAPI UefiMain(EFI_HANDLE image,EFI_SYSTEM_TABLE *t) {
    if(!image||!t||!t->BootServices||!t->RuntimeServices||!t->RuntimeServices->GetVariable)return EFI_INVALID_PARAMETER;
    UINT8 secure=1;UINTN n=1;UINT32 attrs=0;BOOLEAN unlock=FALSE;
    EFI_STATUS s=t->RuntimeServices->GetVariable(L"SecureBoot",&GlobalGuid,&attrs,&n,&secure);
    if(s!=EFI_SUCCESS||n!=1||secure)return EFI_SECURITY_VIOLATION;
    GuInit(t->BootServices,NULL);
    s=GuBootOpen(image,t);if(s!=EFI_SUCCESS)return s;
    const char *stage="manual-choice";
    GuLog("HYBRID U6 entry: menu before GPU preflight; no mutation\n");
    s=GuBootChoose(&unlock);if(s!=EFI_SUCCESS||Gu.fault)goto failed;
    if(!unlock&&!GuBootDryRunRequested()) {
        stage="stage-stock-windows";s=GuBootLoadWindows();if(s!=EFI_SUCCESS||Gu.fault)goto failed;
        s=GuBootClose();if(s!=EFI_SUCCESS||Gu.fault)return EFI_DEVICE_ERROR;
        return GuBootWindows();
    }
    stage="hybrid-topology";
    s=GuHybridObserve(t,image);if(s!=EFI_SUCCESS||Gu.fault)goto failed;
    if(GuBootDryRunRequested()) {
        GuLog("HYBRID D: Intel topology and NVIDIA original PCI state checked; no decode enable, no claim, no payload\n");
        GuBootSay(L"\r\nD complete. No unlock. Return via Windows Boot Manager.\r\n");
        return GuBootClose();
    }
    stage="stage-windows-image";s=GuBootLoadWindows();if(s!=EFI_SUCCESS||Gu.fault)goto failed;
    if(!unlock) {
        s=GuBootClose();if(s!=EFI_SUCCESS||Gu.fault)return EFI_DEVICE_ERROR;
        return GuBootWindows();
    }
    stage="five-firmware-assets";s=GuBootLoadAssets();if(s!=EFI_SUCCESS||Gu.fault)goto failed;
    GuLog("HYBRID operator U: one attempt; preserve Intel, exclusive NVIDIA only; managed decode; no controller disconnect\n");
    if(Gu.fault){s=EFI_DEVICE_ERROR;goto failed;}
    stage="hybrid-exclusive-decode";s=GuHybridAcquire();if(s!=EFI_SUCCESS||Gu.fault)goto failed;
    stage="cold-PROM-profile";s=GuAd104Prepare();if(s!=EFI_SUCCESS||Gu.fault)goto failed;
    if(!GuAd104ColdEntry()||!GuHybridVerify()){s=EFI_NOT_READY;goto failed;}
    Gu.display_check=GuHybridVerify;Gu.display_ok=TRUE;Gu.armed=TRUE;
    GuBootSay(L"\r\nU: NVIDIA transaction starts. Intel display remains attached. Wait for Windows.\r\n");
    GuBootSilence();
    UINT64 mask=0xffffffffffffULL;
    struct pci_dev dev={.vendor=0x10de,.device=0x2717,.dev={.dma_mask=&mask,.coherent_dma_mask=0xfffffffffULL},.current_state=PCI_D0,.pm_cap=Gu.pm_cap,.refs=1};
    struct gpu_unlock_610_shared shared={0};
    stage="shared-assets";int result=gpu_unlock_610_load_shared(&dev,&shared);
    if(!result){stage="firmware-transaction";result=gpu_unlock_610_unlock(&dev,&shared);}
    gpu_unlock_610_release_shared(&shared);
    GuLog("HYBRID transaction result=%d safe=%u retained=%u started=%u quiesced=%u flrs=%u DMA_live=%u\n",result,GuSafeToReturn(),Gu.retained,Gu.started,Gu.quiesced,Gu.flrs,Gu.dma_count);
    if(result||Gu.fault||!GuSafeToReturn()){s=EFI_DEVICE_ERROR;goto failed;}
    stage="hybrid-finish";s=GuHybridFinish();if(s!=EFI_SUCCESS||Gu.fault)goto failed;
    stage="close-USB";s=GuBootClose();if(s!=EFI_SUCCESS||Gu.fault)goto failed;
    stage="final-Intel-Windows-check";if(!GuHybridReadyForWindows()){s=EFI_NOT_READY;goto failed;}
    s=GuBootWindows();GuBootHalt();return s==EFI_SUCCESS?EFI_ABORTED:s;
failed:
    if(s==EFI_SUCCESS)s=EFI_DEVICE_ERROR;
    if(GuBootLogOpen())GuLog("HYBRID STOP: stage=%s status=%#llx mutated=%u started=%u retained=%u fault=%u\n",stage,(unsigned long long)s,GuHybridMutated(),Gu.started,Gu.retained,Gu.fault);
    if(Gu.started||Gu.mmio_writes||Gu.retained||!GuSafeToReturn()) {
        if(Gu.started||Gu.master_attributes_pending)(VOID)GuClearMaster();
        (VOID)GuBootClose();GuBootHalt();return EFI_ABORTED;
    }
    EFI_STATUS restored=GuHybridAbort();
    if(GuBootLogOpen())GuLog("HYBRID pre-firmware rollback: status=%#llx no payload started\n",(unsigned long long)restored);
    (VOID)GuBootClose();
    if(restored!=EFI_SUCCESS){GuBootHalt();return EFI_ABORTED;}
    GuBootSay(L"\r\nU refused before firmware. NVIDIA restored. Use Windows Boot Manager; preserve USB log.\r\n");
    t->BootServices->Stall(8000000);
    return s;
}
