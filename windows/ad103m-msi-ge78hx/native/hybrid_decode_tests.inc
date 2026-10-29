static unsigned DecodeScenario,DecodeWrites,DecodeClaims,DecodeCloses;
static EFI_STATUS EFIAPI DecodeOpen(EFI_HANDLE h,EFI_GUID *guid,VOID **out,EFI_HANDLE agent,EFI_HANDLE controller,UINT32 attributes) {
    CHECK((UINTN)h==1&&BytesEqual(guid,&PciGuid,16)&&agent==Image&&controller==h);
    CHECK(attributes==(EFI_OPEN_PROTOCOL_BY_DRIVER|EFI_OPEN_PROTOCOL_EXCLUSIVE));DecodeClaims++;
    if(DecodeScenario==1)return EFI_ACCESS_DENIED;
    *out=&MockPci[0];return EFI_SUCCESS;
}
static EFI_STATUS EFIAPI DecodeClose(EFI_HANDLE h,EFI_GUID *guid,EFI_HANDLE agent,EFI_HANDLE controller) {
    CHECK((UINTN)h==1&&BytesEqual(guid,&PciGuid,16)&&agent==Image&&controller==h);DecodeCloses++;
    return DecodeScenario==6?EFI_DEVICE_ERROR:EFI_SUCCESS;
}
static EFI_STATUS EFIAPI DecodeWrite(EFI_PCI_IO_PROTOCOL *pci,EFI_PCI_IO_PROTOCOL_WIDTH width,UINT32 offset,UINTN count,VOID *value) {
    CHECK(pci==&MockPci[0]&&width==EfiPciIoWidthUint16&&offset==4&&count==1);DecodeWrites++;
    UINT16 command=*(UINT16 *)value;CHECK(command==(DecodeWrites==1?2:0));
    if(DecodeScenario==4&&DecodeWrites==2)return EFI_DEVICE_ERROR;
    Config[0][4]=(UINT8)command;Config[0][5]=0;
    if(DecodeScenario==2&&DecodeWrites==1)return EFI_DEVICE_ERROR;
    if(DecodeScenario==3&&DecodeWrites==1)Config[0][9]^=1;
    if(DecodeScenario==5&&DecodeWrites==1)Config[1][4]^=2;
    return EFI_SUCCESS;
}
static EFI_STATUS EFIAPI DecodeBar(EFI_PCI_IO_PROTOCOL *pci,UINT8 index,UINT64 *supports,VOID **out) {
    CHECK(pci==&MockPci[0]);*supports=0;*out=NULL;
    if(index!=0)return EFI_UNSUPPORTED;
    UINT8 *p=calloc(1,46);CHECK(p);p[0]=0x8a;p[1]=43;p[17]=0x5e;p[41]=1;
    p[22]=p[23]=p[24]=0xff;p[25]=0x5e;*out=p;return EFI_SUCCESS;
}
static void SetupDecode(unsigned scenario) {
    Setup(0);DecodeScenario=scenario;DecodeWrites=DecodeClaims=DecodeCloses=0;
    memcpy(Config[0],HybridCapturedConfig,256);
    MockPci[0].Pci.Write=DecodeWrite;MockPci[0].GetBarAttributes=DecodeBar;
    Bs.OpenProtocol=DecodeOpen;Bs.CloseProtocol=DecodeClose;
    static EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL console={0};St.ConOut=&console;
}
static void TestHybridDecode(void) {
    TEXT t;
    for(unsigned scenario=0;scenario<7;scenario++) {
        SetupDecode(scenario);TextInit(&t,Report,sizeof(Report));EnumerateDisplays(&Bs,&t);HybridBegin(&St,&t);
        CHECK(HybridIntel.complete&&!HybridMmioAllowed);HybridRequestDecodeTest();HybridDecodeBegin(&St,Image,&t);
        if(scenario==0||scenario==4||scenario==6)CHECK(Decode.enabled&&HybridMmioAllowed);
        else CHECK(!Decode.enabled&&!HybridMmioAllowed);
        HybridDecodeEnd(&St,&t);
        CHECK(DecodeWrites==(scenario==1?0u:2u));CHECK(MemoryReads==0&&PromReads==0);
        CHECK(DecodeCloses==(scenario==1?0u:1u));
        CHECK(HybridDecodeSafe()==(scenario!=3&&scenario!=4&&scenario!=6));
        if(scenario==0||scenario==2||scenario==5)CHECK(Decode.restored&&Config[0][4]==0);
    }
    SetupDecode(0);Config[0][4]=4;TextInit(&t,Report,sizeof(Report));EnumerateDisplays(&Bs,&t);HybridBegin(&St,&t);
    HybridRequestDecodeTest();HybridDecodeBegin(&St,Image,&t);HybridDecodeEnd(&St,&t);
    CHECK(DecodeWrites==0&&DecodeClaims==0&&HybridDecodeSafe());
    SetupDecode(0);TextInit(&t,Report,sizeof(Report));EnumerateDisplays(&Bs,&t);HybridBegin(&St,&t);
    HybridDecodeBegin(&St,Image,&t);HybridDecodeEnd(&St,&t);CHECK(DecodeWrites==0&&DecodeClaims==0);
    SetupDecode(0);HybridRequestDecodeTest();CHECK(HybridCaptureMain(Image,&St)==EFI_SUCCESS);
    CHECK(DecodeWrites==2&&Decode.restored&&Decode.closed&&MemoryReads==25&&PromReads==257);
    CHECK(strstr(Written,"\"original_config_restored\":true")&&strstr(Written,"\"observed_unchanged\":true"));
    {FILE *f=fopen("build/decode-capture.json","wb");CHECK(f);CHECK(fwrite(Written,1,strlen(Written),f)==strlen(Written));CHECK(fclose(f)==0);}
}
