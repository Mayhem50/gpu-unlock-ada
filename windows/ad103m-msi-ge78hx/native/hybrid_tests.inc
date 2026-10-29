/* Included by the generated diagnostic application tests. */
static void TestHybrid(void) {
    UINT8 c[256];HYBRID_PCI_STATE state;CHAR8 text[8192];TEXT t;
    Setup(0);memcpy(c,Config[0],256);state=HybridPciState(c);
    CHECK(state.exact&&state.pm_present&&state.mmio_read_allowed&&state.pm_offset==0x40);
    c[4]=0;CHECK(!HybridPciState(c).mmio_read_allowed);
    c[4]=7;CHECK(!HybridPciState(c).mmio_read_allowed);
    c[4]=2;CHECK(HybridPciState(c).mmio_read_allowed);
    for(unsigned i=1;i<=3;i++){c[0x44]=(UINT8)i;CHECK(!HybridPciState(c).mmio_read_allowed);}
    c[0x44]=0;c[0x34]=0x41;CHECK(!HybridPciState(c).capabilities_valid);
    c[0x34]=0x40;c[0x41]=0x40;CHECK(!HybridPciState(c).capabilities_valid);
    c[0x41]=0;c[0x42]=0;CHECK(!HybridPciState(c).mmio_read_allowed);
    c[0x42]=3;c[0x30]=1;CHECK(!HybridPciState(c).mmio_read_allowed);
    c[0x30]=0;c[0x2e]^=1;CHECK(!HybridPciState(c).exact);
    CHECK(!HybridPciState(NULL).mmio_read_allowed);
    Setup(0);St.ConOut=(EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL *)(UINTN)0x1234;
    TextInit(&t,Report,sizeof(Report));EnumerateDisplays(&Bs,&t);HybridBegin(&St,&t);
    CHECK(HybridIntel.complete&&HybridMmioAllowed);
    TextInit(&t,text,sizeof(text));HybridEnd(&St,&t);CHECK(strstr(text,"\"observed_unchanged\":true"));
    Config[1][4]^=2;TextInit(&t,text,sizeof(text));HybridEnd(&St,&t);
    CHECK(strstr(text,"\"pci_config_unchanged\":false")&&strstr(text,"\"observed_unchanged\":false"));
    Config[1][4]^=2;GopInfo.HorizontalResolution++;TextInit(&t,text,sizeof(text));HybridEnd(&St,&t);
    CHECK(strstr(text,"\"gop_interface_and_mode_unchanged\":false"));
    GopInfo.HorizontalResolution--;St.ConsoleOutHandle=NULL;TextInit(&t,text,sizeof(text));HybridEnd(&St,&t);
    CHECK(strstr(text,"\"console_interface_unchanged\":false"));
    Setup(0);Config[0][4]=0;CHECK(HybridCaptureMain(Image,&St)==EFI_SUCCESS);
    CHECK(MemoryReads==0&&PromReads==0&&strstr(Written,"memory_decode_disabled_no_enable"));
    CHECK(strstr(Written,"\"nvidia_config256\":\"")&&FileWrites==1);
    Setup(0);Config[0][0x44]=3;CHECK(HybridCaptureMain(Image,&St)==EFI_SUCCESS);
    CHECK(MemoryReads==0&&PromReads==0&&strstr(Written,"gpu_not_d0_no_power_change"));
    Setup(0);static EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL console={0};St.ConOut=&console;
    CHECK(HybridCaptureMain(Image,&St)==EFI_SUCCESS);
    CHECK(strstr(Written,"\"observed_unchanged\":true"));
    {FILE *f=fopen("build/hybrid-capture.json","wb");CHECK(f!=NULL);CHECK(fwrite(Written,1,strlen(Written),f)==strlen(Written));CHECK(fclose(f)==0);}
}
