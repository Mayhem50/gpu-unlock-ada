#include "../app.c"
#include <assert.h>
#include <stdio.h>
#include <string.h>
GU_PLATFORM Gu;
static unsigned Scenario,Assets,Acquire,Prepare,Core,Finish,Abort,Windows,Clear,Halt,Menus,Observations;
static BOOLEAN LogOpen,Changed;
static EFI_STATUS EFIAPI Variable(CHAR16 *name,EFI_GUID *guid,UINT32 *attrs,UINTN *bytes,VOID *out) {
    (void)name;(void)guid;(void)attrs;assert(*bytes==1);*(UINT8 *)out=Scenario==3;return EFI_SUCCESS;
}
EFI_STATUS GuBootOpen(EFI_HANDLE h,EFI_SYSTEM_TABLE *t){(void)h;(void)t;LogOpen=TRUE;return EFI_SUCCESS;}
VOID GuInit(EFI_BOOT_SERVICES *bs,EFI_PCI_IO_PROTOCOL *pci){Gu.bs=bs;Gu.pci=pci;}
static EFI_STATUS EFIAPI TestStall(UINTN us){assert(us==8000000);return EFI_SUCCESS;}
BOOLEAN GuBootLogOpen(VOID){return LogOpen;}
EFI_STATUS GuBootClose(VOID){LogOpen=FALSE;return EFI_SUCCESS;}
VOID GuLog(const char *fmt,...){(void)fmt;assert(LogOpen);}
VOID GuBootSay(const CHAR16 *s){(void)s;}
VOID GuBootSilence(VOID){}
VOID GuBootHalt(VOID){Halt++;}
EFI_STATUS GuBootChoose(BOOLEAN *u){Menus++;assert(Observations==0);*u=(BOOLEAN)(Scenario!=1&&Scenario!=2);return EFI_SUCCESS;}
BOOLEAN GuBootDryRunRequested(VOID){return Scenario==1;}
EFI_STATUS GuBootLoadWindows(VOID){return Scenario==10?EFI_CRC_ERROR:EFI_SUCCESS;}
EFI_STATUS GuBootLoadAssets(VOID){Assets++;return Scenario==4?EFI_CRC_ERROR:EFI_SUCCESS;}
EFI_STATUS GuBootWindows(VOID){Windows++;assert(!Changed);return EFI_ACCESS_DENIED;}
EFI_STATUS GuHybridObserve(EFI_SYSTEM_TABLE *t,EFI_HANDLE h){(void)t;(void)h;Observations++;return Scenario==11?EFI_NOT_READY:EFI_SUCCESS;}
EFI_STATUS GuHybridAcquire(VOID){Acquire++;Changed=TRUE;return Scenario==5?EFI_DEVICE_ERROR:EFI_SUCCESS;}
BOOLEAN GuHybridVerify(VOID){return TRUE;}
EFI_STATUS GuAd104Prepare(VOID){Prepare++;return Scenario==6?EFI_CRC_ERROR:EFI_SUCCESS;}
BOOLEAN GuAd104ColdEntry(VOID){return TRUE;}
BOOLEAN GuSafeToReturn(VOID){return !Gu.retained;}
EFI_STATUS GuClearMaster(VOID){Clear++;return EFI_SUCCESS;}
BOOLEAN GuHybridMutated(VOID){return Changed;}
EFI_STATUS GuHybridAbort(VOID){Abort++;Changed=FALSE;return EFI_SUCCESS;}
EFI_STATUS GuHybridFinish(VOID){Finish++;if(Scenario==8)return EFI_DEVICE_ERROR;Changed=FALSE;return EFI_SUCCESS;}
BOOLEAN GuHybridReadyForWindows(VOID){return Scenario!=9;}
int gpu_unlock_610_load_shared(struct pci_dev *p,struct gpu_unlock_610_shared *s){(void)p;(void)s;return 0;}
void gpu_unlock_610_release_shared(struct gpu_unlock_610_shared *s){(void)s;}
int gpu_unlock_610_unlock(struct pci_dev *p,const struct gpu_unlock_610_shared *s){(void)p;(void)s;Core++;Gu.started=TRUE;Gu.quiesced=TRUE;Gu.flrs=1;if(Scenario==7){Gu.retained=TRUE;return -1;}return 0;}
int main(void) {
    EFI_RUNTIME_SERVICES rs={0};EFI_BOOT_SERVICES bs={0};EFI_SYSTEM_TABLE st={0};
    rs.GetVariable=Variable;bs.Stall=TestStall;st.RuntimeServices=&rs;st.BootServices=&bs;
    for(Scenario=0;Scenario<12;Scenario++) {
        memset(&Gu,0,sizeof(Gu));Assets=Acquire=Prepare=Core=Finish=Abort=Windows=Clear=Halt=Menus=Observations=0;LogOpen=Changed=FALSE;
        (void)UefiMain((EFI_HANDLE)(UINTN)1,&st);
        if(Scenario==0)assert(Assets==1&&Acquire==1&&Prepare==1&&Core==1&&Finish==1&&Windows==1&&Halt==1);
        if(Scenario==1)assert(Assets==0&&Acquire==0&&Windows==0&&Halt==0);
        if(Scenario==2)assert(Assets==0&&Acquire==0&&Windows==1);
        if(Scenario==3)assert(!LogOpen&&Acquire==0&&Windows==0);
        if(Scenario==4)assert(Assets==1&&Acquire==0&&Windows==0&&Abort==1);
        if(Scenario==5)assert(Acquire==1&&Prepare==0&&Core==0&&Abort==1&&Halt==0);
        if(Scenario==6)assert(Prepare==1&&Core==0&&Abort==1&&Halt==0);
        if(Scenario==7)assert(Core==1&&Finish==0&&Windows==0&&Halt==1&&Clear==1&&Abort==0);
        if(Scenario==8)assert(Finish==1&&Windows==0&&Halt==1&&Abort==0);
        if(Scenario==9)assert(Finish==1&&Windows==0&&Halt==1&&!LogOpen);
        if(Scenario==10)assert(Assets==0&&Acquire==0&&Windows==0);
        if(Scenario==11)assert(Menus==1&&Observations==1&&Assets==0&&Acquire==0&&Core==0&&Abort==1&&Halt==0);
        if(Scenario==2)assert(Menus==1&&Observations==0);
    }
    puts("PASS: actual hybrid U application 12 flow scenarios; menu precedes refused GPU preflight; stock/D do not acquire");return 0;
}
