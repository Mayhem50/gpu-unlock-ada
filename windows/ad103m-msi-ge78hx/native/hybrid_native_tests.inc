/* Real native ownership backend, Intel preservation and managed attribute API. */
#include "hybrid_config.h"
static unsigned Mode,Claims,Closes,Enables,Disables;static BOOLEAN Claimed,FinalRegisters;
static UINT64 Attributes;
VOID GuLog(const char *format,...){(void)format;}
static EFI_STATUS EFIAPI NativeMockLocate(EFI_LOCATE_SEARCH_TYPE kind,EFI_GUID *guid,VOID *key,UINTN *count,EFI_HANDLE **handles) {
    if(BytesEqual(guid,&GopGuid,16)){
        CHECK(kind==ByProtocol);(void)key;*count=1;*handles=malloc(sizeof(EFI_HANDLE));CHECK(*handles);(*handles)[0]=(EFI_HANDLE)(UINTN)4;return EFI_SUCCESS;
    }
    return Locate(kind,guid,key,count,handles);
}
static EFI_STATUS EFIAPI NativeMockProtocols(EFI_HANDLE h,EFI_GUID ***out,UINTN *n) {
    CHECK((UINTN)h==1);*n=2;*out=malloc(2*sizeof(EFI_GUID *));CHECK(*out);
    (*out)[0]=&PciGuid;(*out)[1]=&DpGuid;return EFI_SUCCESS;
}
static EFI_STATUS EFIAPI NativeMockOpens(EFI_HANDLE h,EFI_GUID *guid,EFI_OPEN_PROTOCOL_INFORMATION_ENTRY **out,UINTN *n) {
    CHECK((UINTN)h==1);*out=NULL;*n=0;
    if((Claimed&&BytesEqual(guid,&PciGuid,16))||Mode==9||(Mode==24&&Closes>0)) {
        *n=1;*out=calloc(1,sizeof(**out));CHECK(*out);
        (*out)->AgentHandle=(Mode==9||(Mode==24&&Closes>0))?(EFI_HANDLE)(UINTN)123:Image;
        (*out)->ControllerHandle=h;(*out)->OpenCount=1;(*out)->Attributes=EFI_OPEN_PROTOCOL_BY_DRIVER|EFI_OPEN_PROTOCOL_EXCLUSIVE;
    } else if(Mode>=10&&BytesEqual(guid,&DpGuid,16)) {
        *n=1;*out=calloc(1,sizeof(**out));CHECK(*out);
        (*out)->AgentHandle=(EFI_HANDLE)(UINTN)123;
        (*out)->Attributes=EFI_OPEN_PROTOCOL_BY_HANDLE_PROTOCOL;(*out)->OpenCount=91;
        if(Mode==11)(*out)->Attributes=EFI_OPEN_PROTOCOL_BY_DRIVER;
        if(Mode==12)(*out)->Attributes=EFI_OPEN_PROTOCOL_BY_CHILD_CONTROLLER;
        if(Mode==13)(*out)->Attributes=EFI_OPEN_PROTOCOL_BY_DRIVER|EFI_OPEN_PROTOCOL_BY_HANDLE_PROTOCOL;
        if(Mode==14)(*out)->Attributes=EFI_OPEN_PROTOCOL_GET_PROTOCOL;
        if(Mode==15)(*out)->OpenCount=0;
        if(Mode==16)(*out)->ControllerHandle=h;
        if(Mode==17){(*out)->Attributes=EFI_OPEN_PROTOCOL_GET_PROTOCOL;(*out)->ControllerHandle=h;}
        if(Mode==18){(*out)->Attributes=EFI_OPEN_PROTOCOL_TEST_PROTOCOL;(*out)->ControllerHandle=h;(*out)->OpenCount=2;}
        if(Mode==19)(*out)->Attributes=EFI_OPEN_PROTOCOL_TEST_PROTOCOL|EFI_OPEN_PROTOCOL_BY_DRIVER;
        if(Mode==20)(*out)->Attributes=EFI_OPEN_PROTOCOL_BY_HANDLE_PROTOCOL|EFI_OPEN_PROTOCOL_GET_PROTOCOL;
    }
    return EFI_SUCCESS;
}
static EFI_STATUS EFIAPI NativeMockOpen(EFI_HANDLE h,EFI_GUID *guid,VOID **out,EFI_HANDLE image,EFI_HANDLE controller,UINT32 attrs) {
    CHECK((UINTN)h==1&&BytesEqual(guid,&PciGuid,16)&&image==Image&&controller==h);
    CHECK(attrs==(EFI_OPEN_PROTOCOL_BY_DRIVER|EFI_OPEN_PROTOCOL_EXCLUSIVE));Claims++;
    if(Mode==1)return EFI_ACCESS_DENIED;
    Claimed=TRUE;*out=&MockPci[0];return EFI_SUCCESS;
}
static EFI_STATUS EFIAPI NativeMockClose(EFI_HANDLE h,EFI_GUID *guid,EFI_HANDLE image,EFI_HANDLE controller) {
    CHECK((UINTN)h==1&&BytesEqual(guid,&PciGuid,16)&&image==Image&&controller==h);Closes++;
    if(Mode==5)return EFI_DEVICE_ERROR;
    Claimed=FALSE;
    if(Mode==21)Config[0][4]&=(UINT8)~2u;
    if(Mode==22)Attributes=0;
    if(Mode==23)Config[1][4]^=2;
    if(Mode==29){Config[0][4]|=4;Attributes|=EFI_PCI_IO_ATTRIBUTE_BUS_MASTER;}
    return EFI_SUCCESS;
}
static EFI_STATUS EFIAPI NativeMockAttributes(EFI_PCI_IO_PROTOCOL *pci,EFI_PCI_IO_PROTOCOL_ATTRIBUTE_OPERATION op,UINT64 mask,UINT64 *out) {
    CHECK(pci==&MockPci[0]);
    if(op==EfiPciIoAttributeOperationGet){CHECK(out&&mask==0);*out=Attributes;return EFI_SUCCESS;}
    if(op==EfiPciIoAttributeOperationSupported){CHECK(out&&mask==0);*out=EFI_PCI_IO_ATTRIBUTE_MEMORY|(Mode==8?0:EFI_PCI_IO_ATTRIBUTE_BUS_MASTER);return EFI_SUCCESS;}
    CHECK(mask==EFI_PCI_IO_ATTRIBUTE_MEMORY&&Claimed&&out==NULL);
    if(op==EfiPciIoAttributeOperationEnable) {
        Enables++;Attributes|=mask;Config[0][4]|=2;
        if(Mode==2)return EFI_DEVICE_ERROR; /* error after a hardware change */
        if(Mode==3)Config[1][4]^=2;
        return EFI_SUCCESS;
    }
    CHECK(op==EfiPciIoAttributeOperationDisable);Disables++;
    if(Mode==4)return EFI_DEVICE_ERROR;
    Attributes&=~mask;Config[0][4]&=(UINT8)~2u;return EFI_SUCCESS;
}
static EFI_STATUS EFIAPI NativeMockBar(EFI_PCI_IO_PROTOCOL *pci,UINT8 index,UINT64 *supports,VOID **out) {
    CHECK(pci==&MockPci[0]);*supports=0;*out=NULL;if(index!=0)return EFI_UNSUPPORTED;
    UINT8 *p=calloc(1,46);CHECK(p);p[0]=0x8a;p[1]=43;p[17]=0x5e;p[41]=1;
    p[22]=p[23]=p[24]=0xff;p[25]=0x5e;*out=p;return EFI_SUCCESS;
}
static EFI_STATUS EFIAPI NativeMockMemory(EFI_PCI_IO_PROTOCOL *pci,EFI_PCI_IO_PROTOCOL_WIDTH width,UINT8 bar,UINT64 offset,UINTN count,VOID *out) {
    CHECK(FinalRegisters&&pci==&MockPci[0]&&width==EfiPciIoWidthUint32&&bar==0&&count==1);
    UINT32 value=0;
    switch(offset){case 0:value=0x193000a1;break;case 0x110100:value=0x10;break;
    case 0x82381c:value=0x88888888;break;case 0x823820:value=8;break;
    case 0x823804:value=0xffffffff;break;case 0x823818:value=Mode==6?1:0;break;case 0x1fa828:break;default:CHECK(FALSE);}
    *(UINT32 *)out=value;return EFI_SUCCESS;
}
static void NativeSetup(unsigned mode) {
    Setup(0);memset(&Native,0,sizeof(Native));memset(&Gu,0,sizeof(Gu));
    Mode=mode;Claims=Closes=Enables=Disables=0;Claimed=FinalRegisters=FALSE;Attributes=0;
    memcpy(Config[0],HybridConfig,256);static EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL output={0};St.ConOut=&output;
    Bs.ProtocolsPerHandle=NativeMockProtocols;Bs.OpenProtocolInformation=NativeMockOpens;
    Bs.LocateHandleBuffer=NativeMockLocate;
    Bs.OpenProtocol=NativeMockOpen;Bs.CloseProtocol=NativeMockClose;
    MockPci[0].Attributes=NativeMockAttributes;MockPci[0].GetBarAttributes=NativeMockBar;
    MockPci[0].Pci.Write=NULL; /* filled below: U requires a transport; backend uses managed API */
}
static EFI_STATUS EFIAPI NativeUnusedWrite(EFI_PCI_IO_PROTOCOL *p,EFI_PCI_IO_PROTOCOL_WIDTH w,UINT32 o,UINTN n,VOID *v) {
    (void)p;(void)w;(void)o;(void)n;(void)v;CHECK(FALSE);return EFI_DEVICE_ERROR;
}
int main(void) {
    for(unsigned mode=0;mode<30;mode++) {
        NativeSetup(mode);MockPci[0].Pci.Write=NativeUnusedWrite;
        EFI_STATUS s=GuHybridObserve(&St,Image);
        if(mode==9||mode==11||mode==12||mode==13||mode==15||mode==19||mode==20){CHECK(s!=EFI_SUCCESS&&Claims==0&&Enables==0);continue;}
        CHECK(s==EFI_SUCCESS&&MemoryReads==0&&Enables==0&&Native.ready);
        s=GuHybridAcquire();
        if(mode==1||mode==2||mode==3||mode==8) {
            CHECK(s!=EFI_SUCCESS);
            if(mode==1||mode==8)CHECK(Enables==0);
            EFI_STATUS rollback=GuHybridAbort();
            if(mode!=3)CHECK(rollback==EFI_SUCCESS&&!Claimed&&Attributes==0&&Config[0][4]==0);
            else CHECK(rollback!=EFI_SUCCESS);
            continue;
        }
        CHECK(s==EFI_SUCCESS&&Gu.owned&&Config[0][4]==2&&Attributes==EFI_PCI_IO_ATTRIBUTE_MEMORY&&GuHybridVerify());
        if(mode==4){CHECK(GuHybridAbort()!=EFI_SUCCESS&&Disables==1&&Claimed);continue;}
        if(mode==7){CHECK(GuHybridAbort()==EFI_SUCCESS&&Enables==1&&Disables==1&&!Claimed);continue;}
        Gu.started=Gu.quiesced=TRUE;Gu.flrs=1;Gu.mmio_writes=1;Gu.profile_verified=Gu.prom_verified=TRUE;
        FinalRegisters=TRUE;MockPci[0].Mem.Read=NativeMockMemory;
        Config[0][0x82]&=(UINT8)~9u; /* tested PCIe sticky-status clear */
        s=GuHybridFinish();
        if(mode==0||mode==10||mode==14||mode==16||mode==17||mode==18||(mode>=25&&mode<=28)) {
            CHECK(s==EFI_SUCCESS&&GuHybridReadyForWindows()&&!Claimed&&!Gu.owned&&Config[0][4]==2&&Attributes==EFI_PCI_IO_ATTRIBUTE_MEMORY&&Disables==0);
            /* Recheck immediately before Windows: reject late decode/cache,
             * Intel or ownership drift even after a successful finish. */
            if(mode==25)Config[0][4]&=(UINT8)~2u;
            if(mode==26)Attributes=0;
            if(mode==27)Config[1][4]^=2;
            if(mode==28)Mode=24;
            if(mode>=25&&mode<=28)CHECK(!GuHybridReadyForWindows());
        }
        else {CHECK(s!=EFI_SUCCESS&&!GuHybridReadyForWindows());if(mode==6)CHECK(Disables==0);}
        CHECK(Enables==1&&Claims==1);
    }
    printf("PASS: native hybrid backend 30 scenarios, %u checks; U6 MEMORY stays enabled and BME off; Intel and post-close/late drift guarded; rollback restores original decode\n",Checks);
    return 0;
}
