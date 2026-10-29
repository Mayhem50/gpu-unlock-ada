#include "core.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
EFI_STATUS EFIAPI UefiMain(EFI_HANDLE,EFI_SYSTEM_TABLE *);
static EFI_SYSTEM_TABLE St;static EFI_BOOT_SERVICES Bs;static EFI_SIMPLE_TEXT_INPUT_PROTOCOL Input;
static unsigned Captures,Reads,Stalls,Scenario,DecodeRequests;
VOID HybridRequestDecodeTest(VOID){DecodeRequests++;}
EFI_STATUS EFIAPI HybridCaptureMain(EFI_HANDLE image,EFI_SYSTEM_TABLE *st){assert(image&&st==&St);Captures++;return EFI_SUCCESS;}
static EFI_STATUS EFIAPI Read(EFI_SIMPLE_TEXT_INPUT_PROTOCOL *self,EFI_INPUT_KEY *key) {
    assert(self==&Input);Reads++;
    if(Scenario==0)key->UnicodeChar='D';
    if(Scenario==1)key->UnicodeChar='\r';
    if(Scenario==2)key->UnicodeChar=Reads==1?'U':'d';
    if(Scenario==3)key->ScanCode=0x17;
    if(Scenario==4)return EFI_NOT_READY;
    if(Scenario==5)return EFI_DEVICE_ERROR;
    if(Scenario==6)return EFI_NOT_READY;
    if(Scenario==7)key->UnicodeChar='T';
    return EFI_SUCCESS;
}
static EFI_STATUS EFIAPI Stall(UINTN us){assert(us==100000);Stalls++;return Scenario==6?EFI_DEVICE_ERROR:EFI_SUCCESS;}
int main(void) {
    for(Scenario=0;Scenario<8;Scenario++) {
        memset(&St,0,sizeof(St));memset(&Bs,0,sizeof(Bs));memset(&Input,0,sizeof(Input));
        Captures=Reads=Stalls=DecodeRequests=0;St.BootServices=&Bs;St.ConIn=&Input;Input.ReadKeyStroke=Read;Bs.Stall=Stall;
        EFI_STATUS status=UefiMain((EFI_HANDLE)(UINTN)1,&St);
        if(Scenario<=2)assert(status==EFI_SUCCESS&&Captures==1);
        if(Scenario==2)assert(Reads==2&&Stalls==1);
        if(Scenario==3)assert(status==EFI_ABORTED&&Captures==0);
        if(Scenario==4)assert(status==EFI_TIMEOUT&&Reads==300&&Captures==0);
        if(Scenario==5||Scenario==6)assert(status==EFI_DEVICE_ERROR&&Captures==0);
        if(Scenario==7)assert(status==EFI_SUCCESS&&Captures==1&&DecodeRequests==1);
        else assert(DecodeRequests==0);
    }
    assert(UefiMain(NULL,&St)==EFI_INVALID_PARAMETER);
    puts("PASS: hybrid entry 9 scenarios; only explicit T requests PCI decode; U never authorizes an unlock");return 0;
}
