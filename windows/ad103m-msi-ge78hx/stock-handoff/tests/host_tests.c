/* SPDX-License-Identifier: GPL-2.0-only */
/* Execute the real stock adapter against a fake UEFI table. No hardware APIs. */
#include "../src/app.c"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <setjmp.h>
static unsigned Checks,Cases,Calls,Loads,Starts,Unloads,FileCloses,RootCloses,PoolFrees,Allocations,Keys;
#define CHECK(x) do {Checks++;if(!(x)){fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#x);exit(1);}}while(0)
enum {NORMAL,NO_TARGET,TWO_TARGETS,BAD_PATH,OWN_NOT_USB,INFO_SHORT,INFO_BIG,FILE_SIZE_BAD,
 DIRECTORY_FILE,SHORT_READ,TAIL_READ,CORRUPT_IMAGE,ALLOC_FAIL,LOAD_FAIL,LOAD_DIRTY,
 CHILD_WRONG_VOLUME,CHILD_WRONG_PARENT,CHILD_OPTIONS,CHILD_PATH,CHILD_NO_BASE,
 FILE_CLOSE_FAIL,ROOT_CLOSE_FAIL,POOL_FREE_FAIL,UNLOAD_FAIL,ESCAPE,TIMEOUT,INPUT_FAIL,
 STALL_FAIL,CONSOLE_FAIL,ROOT_DIRTY,FILE_DIRTY,PARTITION_MISMATCH,GEOMETRY_MISMATCH,
 ROOT_NO_CLOSE,FILE_NO_CLOSE,DIRTY_ALLOC,START_ERROR,REENTRY,MUTATE_PROFILE,NO_RETURN,
 LATE_CHILD_OPTIONS,LATE_CHILD_BASE,LATE_CHILD_PARENT,LATE_CHILD_PATH,
 QUEUED_CONFIRM,OTHER_KEY,CONFIRM_OUTPUT_FAIL,FINAL_OUTPUT_FAIL,MUTATE_FUNCTIONS,
 ALLOC_NULL,LOAD_NULL,FAULT_COUNT};
static unsigned Fault;
static STOCK_PROFILE Profile;
static STOCK_STATE State;
static EFI_BOOT_SERVICES Boot;
static EFI_SYSTEM_TABLE Table;
static EFI_SIMPLE_TEXT_INPUT_PROTOCOL Input;
static EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL Output;
static EFI_LOADED_IMAGE_PROTOCOL OwnImage,ChildImage;
static EFI_SIMPLE_FILE_SYSTEM_PROTOCOL Fs;
static EFI_FILE_PROTOCOL Root,File;
static UINT8 UsbPath[10],EspPath[52],ChildPath[4+sizeof(WindowsName)+4],Data[8192];
static VOID *Blocks[8];static BOOLEAN Alive[8];
static UINTN Position;
static jmp_buf Transfer;
#define SELF ((EFI_HANDLE)(UINTN)1)
#define USB ((EFI_HANDLE)(UINTN)2)
#define ESP ((EFI_HANDLE)(UINTN)3)
#define OTHER ((EFI_HANDLE)(UINTN)4)
#define CHILD ((EFI_HANDLE)(UINTN)5)
static void Probe(void) {
    Calls++;
    if(Fault==REENTRY) {
        unsigned before=Calls;STOCK_STATE previous=State;
        CHECK(StockRun(SELF,&Table,&Profile,&State)==EFI_ALREADY_STARTED);
        CHECK(Calls==before&&!memcmp(&previous,&State,sizeof(State)));
    }
}
static EFI_STATUS EFIAPI Handle(EFI_HANDLE h,EFI_GUID *guid,VOID **out) {
    Probe();*out=NULL;
    if(Equal(guid,&LoadedGuid,16)) {
        if(h==SELF)*out=&OwnImage;else if(h==CHILD)*out=&ChildImage;
    } else if(Equal(guid,&PathGuid,16)) {
        if(h==USB)*out=UsbPath;else if(h==ESP||h==OTHER)*out=EspPath;
    } else if(Equal(guid,&FsGuid,16)&&h==ESP)*out=&Fs;
    if(Fault==MUTATE_PROFILE&&h==SELF)memset(Profile.digest,0,32);
    if(Fault==MUTATE_FUNCTIONS&&h==SELF)Boot.StartImage=NULL;
    return *out?EFI_SUCCESS:EFI_NOT_FOUND;
}
static EFI_STATUS EFIAPI Allocate(EFI_MEMORY_TYPE type,UINTN bytes,VOID **out) {
    Probe();CHECK(type==EfiLoaderData&&Allocations<8);*out=NULL;
    if(Fault==ALLOC_FAIL)return EFI_OUT_OF_RESOURCES;
    if(Fault==ALLOC_NULL&&Allocations==1)return EFI_SUCCESS;
    *out=calloc(1,bytes);CHECK(*out);Blocks[Allocations]=*out;Alive[Allocations++]=TRUE;
    return Fault==DIRTY_ALLOC?EFI_DEVICE_ERROR:EFI_SUCCESS;
}
static EFI_STATUS EFIAPI Free(VOID *p) {
    Probe();PoolFrees++;
    if(Fault==POOL_FREE_FAIL)return EFI_DEVICE_ERROR;
    for(unsigned i=0;i<Allocations;i++)if(Blocks[i]==p&&Alive[i]){Alive[i]=FALSE;free(p);return EFI_SUCCESS;}
    CHECK(FALSE);return EFI_INVALID_PARAMETER;
}
static EFI_STATUS EFIAPI Locate(EFI_LOCATE_SEARCH_TYPE type,EFI_GUID *guid,VOID *key,UINTN *n,EFI_HANDLE **out) {
    Probe();CHECK(type==ByProtocol&&Equal(guid,&FsGuid,16)&&!key);
    EFI_STATUS status=Allocate(EfiLoaderData,3*sizeof(EFI_HANDLE),(VOID **)out);
    if(status!=EFI_SUCCESS)return status;
    *n=Fault==TWO_TARGETS?3:Fault==NO_TARGET?1:2;
    (*out)[0]=USB;(*out)[1]=ESP;(*out)[2]=OTHER;return EFI_SUCCESS;
}
static EFI_STATUS EFIAPI OpenVolume(EFI_SIMPLE_FILE_SYSTEM_PROTOCOL *self,EFI_FILE_PROTOCOL **out) {
    Probe();CHECK(self==&Fs);*out=&Root;return Fault==ROOT_DIRTY?EFI_DEVICE_ERROR:EFI_SUCCESS;
}
static EFI_STATUS EFIAPI OpenFile(EFI_FILE_PROTOCOL *self,EFI_FILE_PROTOCOL **out,CHAR16 *name,UINT64 mode,UINT64 attr) {
    Probe();CHECK(self==&Root&&mode==EFI_FILE_MODE_READ&&!attr&&Equal(name,WindowsName,sizeof(WindowsName)));
    *out=&File;return Fault==FILE_DIRTY?EFI_DEVICE_ERROR:EFI_SUCCESS;
}
static EFI_STATUS EFIAPI Close(EFI_FILE_PROTOCOL *self) {
    Probe();
    if(self==&File){FileCloses++;CHECK(FileCloses==1);return Fault==FILE_CLOSE_FAIL?EFI_DEVICE_ERROR:EFI_SUCCESS;}
    CHECK(self==&Root);RootCloses++;CHECK(RootCloses==1);return Fault==ROOT_CLOSE_FAIL?EFI_DEVICE_ERROR:EFI_SUCCESS;
}
static EFI_STATUS EFIAPI Info(EFI_FILE_PROTOCOL *self,EFI_GUID *guid,UINTN *n,VOID *out) {
    Probe();CHECK(self==&File&&Equal(guid,&InfoGuid,16)&&*n>=sizeof(EFI_FILE_INFO));
    EFI_FILE_INFO *info=out;info->Size=sizeof(*info);info->FileSize=sizeof(Data);info->Attribute=0;
    if(Fault==FILE_SIZE_BAD)info->FileSize++;
    if(Fault==DIRECTORY_FILE)info->Attribute=EFI_FILE_DIRECTORY;
    *n=Fault==INFO_SHORT?4:Fault==INFO_BIG?2048:sizeof(*info);return EFI_SUCCESS;
}
static EFI_STATUS EFIAPI Read(EFI_FILE_PROTOCOL *self,UINTN *n,VOID *out) {
    Probe();CHECK(self==&File);
    if(Position==sizeof(Data)){CHECK(*n==1);*n=Fault==TAIL_READ?1:0;return EFI_SUCCESS;}
    CHECK(*n<=sizeof(Data)-Position);
    if(Fault==SHORT_READ)(*n)--;
    memcpy(out,Data+Position,*n);Position+=*n;
    if(Fault==CORRUPT_IMAGE)((UINT8 *)out)[0]^=1;
    return EFI_SUCCESS;
}
static EFI_STATUS EFIAPI Load(BOOLEAN policy,EFI_HANDLE parent,EFI_DEVICE_PATH_PROTOCOL *path,VOID *data,UINTN bytes,EFI_HANDLE *child) {
    Probe();Loads++;CHECK(!policy&&parent==SELF&&bytes==sizeof(Data)&&Equal(data,Data,bytes));
    CHECK(Equal(path,EspPath,sizeof(EspPath)-4));
    CHECK(LoadedPath((VOID *)((UINT8 *)path+sizeof(EspPath)-4)));
    if(Fault==LOAD_FAIL){*child=NULL;return EFI_DEVICE_ERROR;}
    if(Fault==LOAD_NULL){*child=NULL;return EFI_SUCCESS;}
    *child=CHILD;return Fault==LOAD_DIRTY?EFI_DEVICE_ERROR:EFI_SUCCESS;
}
static EFI_STATUS EFIAPI Unload(EFI_HANDLE child) {
    Probe();Unloads++;CHECK(child==CHILD&&!Starts&&Unloads==1);return Fault==UNLOAD_FAIL?EFI_DEVICE_ERROR:EFI_SUCCESS;
}
static EFI_STATUS EFIAPI Start(EFI_HANDLE child,UINTN *n,CHAR16 **exit_data) {
    Probe();Starts++;CHECK(child==CHILD&&!n&&!exit_data&&Starts==1&&State.start_attempted);
    CHECK(!State.bytes&&!State.path&&!State.file&&!State.root&&!State.handles);
    if(Fault==NO_RETURN)longjmp(Transfer,1);
    return Fault==START_ERROR?EFI_DEVICE_ERROR:EFI_SUCCESS;
}
static EFI_STATUS EFIAPI SayMock(EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL *self,CHAR16 *text) {
    Probe();CHECK(self==&Output&&text&&!Starts);
    if(text[0]=='A'&&text[1]=='6') {
        if(Fault==LATE_CHILD_OPTIONS)ChildImage.LoadOptionsSize=1;
        if(Fault==LATE_CHILD_BASE)ChildImage.ImageBase=NULL;
        if(Fault==LATE_CHILD_PARENT)ChildImage.ParentHandle=USB;
        if(Fault==LATE_CHILD_PATH)ChildPath[4]^=1;
    }
    if((Fault==CONFIRM_OUTPUT_FAIL&&text[0]=='A'&&text[1]=='5')||
       (Fault==FINAL_OUTPUT_FAIL&&text[0]=='A'&&text[1]=='6'))return EFI_DEVICE_ERROR;
    return Fault==CONSOLE_FAIL?EFI_DEVICE_ERROR:EFI_SUCCESS;
}
static EFI_STATUS EFIAPI ReadKey(EFI_SIMPLE_TEXT_INPUT_PROTOCOL *self,EFI_INPUT_KEY *key) {
    Probe();CHECK(self==&Input);memset(key,0,sizeof(*key));Keys++;
    if(Fault==QUEUED_CONFIRM){key->UnicodeChar='W';return EFI_SUCCESS;}
    if(Keys==1||Fault==TIMEOUT||Fault==STALL_FAIL)return EFI_NOT_READY; /* initial drain */
    if(Fault==INPUT_FAIL)return EFI_DEVICE_ERROR;
    key->UnicodeChar=Fault==ESCAPE||Fault==UNLOAD_FAIL?27:Fault==OTHER_KEY&&Keys==2?'x':'W';return EFI_SUCCESS;
}
static EFI_STATUS EFIAPI StallMock(UINTN usec) {
    Probe();CHECK(usec==100000&&!Starts);return Fault==STALL_FAIL?EFI_DEVICE_ERROR:EFI_SUCCESS;
}
static void Setup(unsigned fault) {
    for(unsigned i=0;i<Allocations;i++)if(Alive[i])free(Blocks[i]);
    memset(Blocks,0,sizeof(Blocks));memset(Alive,0,sizeof(Alive));
    Calls=Loads=Starts=Unloads=FileCloses=RootCloses=PoolFrees=Allocations=Keys=0;Position=0;
    memset(&State,0,sizeof(State));memset(&Profile,0,sizeof(Profile));memset(&Boot,0,sizeof(Boot));
    memset(&Table,0,sizeof(Table));memset(&Input,0,sizeof(Input));memset(&Output,0,sizeof(Output));
    memset(&OwnImage,0,sizeof(OwnImage));memset(&ChildImage,0,sizeof(ChildImage));
    memset(&Fs,0,sizeof(Fs));memset(&Root,0,sizeof(Root));memset(&File,0,sizeof(File));
    memset(UsbPath,0,sizeof(UsbPath));memset(EspPath,0,sizeof(EspPath));memset(ChildPath,0,sizeof(ChildPath));
    Profile.partition_number=1;Profile.start_lba=2048;Profile.sectors=614400;Profile.image_bytes=sizeof(Data);
    for(UINTN i=0;i<16;i++)Profile.partition[i]=(UINT8)(i+1);
    for(UINTN i=0;i<sizeof(Data);i++)Data[i]=(UINT8)(i*7);
    GuSha256(Data,sizeof(Data),Profile.digest);
    UsbPath[0]=3;UsbPath[1]=5;UsbPath[2]=6;UsbPath[6]=0x7f;UsbPath[7]=0xff;UsbPath[8]=4;
    EspPath[0]=1;EspPath[1]=1;EspPath[2]=6;
    UINT8 *hd=EspPath+6;hd[0]=4;hd[1]=1;hd[2]=42;
    memcpy(hd+4,&Profile.partition_number,4);memcpy(hd+8,&Profile.start_lba,8);memcpy(hd+16,&Profile.sectors,8);
    memcpy(hd+24,Profile.partition,16);hd[40]=hd[41]=2;
    EspPath[48]=0x7f;EspPath[49]=0xff;EspPath[50]=4;
    ChildPath[0]=4;ChildPath[1]=4;ChildPath[2]=(UINT8)(4+sizeof(WindowsName));
    memcpy(ChildPath+4,WindowsName,sizeof(WindowsName));
    ChildPath[4+sizeof(WindowsName)]=0x7f;ChildPath[5+sizeof(WindowsName)]=0xff;ChildPath[6+sizeof(WindowsName)]=4;
    OwnImage.DeviceHandle=USB;ChildImage.DeviceHandle=ESP;ChildImage.ParentHandle=SELF;
    ChildImage.ImageBase=Data;ChildImage.ImageSize=sizeof(Data);ChildImage.FilePath=(VOID *)ChildPath;
    Boot.HandleProtocol=Handle;Boot.LocateHandleBuffer=Locate;Boot.AllocatePool=Allocate;Boot.FreePool=Free;
    Boot.LoadImage=Load;Boot.UnloadImage=Unload;Boot.StartImage=Start;Boot.Stall=StallMock;
    Table.BootServices=&Boot;Table.ConIn=&Input;Table.ConOut=&Output;
    Input.ReadKeyStroke=ReadKey;Output.OutputString=SayMock;
    Fs.OpenVolume=OpenVolume;Root.Open=OpenFile;Root.Close=Close;File.Read=Read;File.GetInfo=Info;File.Close=Close;
    Fault=fault;
    if(fault==BAD_PATH)EspPath[2]=0;
    if(fault==OWN_NOT_USB)UsbPath[0]=1;
    if(fault==PARTITION_MISMATCH)hd[24]^=1;
    if(fault==GEOMETRY_MISMATCH)hd[8]^=1;
    if(fault==CHILD_WRONG_VOLUME)ChildImage.DeviceHandle=USB;
    if(fault==CHILD_WRONG_PARENT)ChildImage.ParentHandle=USB;
    if(fault==CHILD_OPTIONS)ChildImage.LoadOptionsSize=1;
    if(fault==CHILD_PATH)ChildPath[4]^=1;
    if(fault==CHILD_NO_BASE)ChildImage.ImageBase=NULL;
    if(fault==ROOT_NO_CLOSE)Root.Close=NULL;
    if(fault==FILE_NO_CLOSE)File.Close=NULL;
}
int main(void) {
    for(unsigned f=0;f<FAULT_COUNT;f++) {
        Setup(f);Cases++;EFI_STATUS result=EFI_SUCCESS;
        if(!setjmp(Transfer))result=StockRun(SELF,&Table,&Profile,&State);
        CHECK(Starts<=1&&Loads<=1&&Unloads<=1);
        BOOLEAN success=f==NORMAL||f==REENTRY||f==MUTATE_PROFILE||f==START_ERROR||f==NO_RETURN||f==OTHER_KEY||f==MUTATE_FUNCTIONS;
        CHECK(Starts==(success?1u:0u));
        if(Starts) {
            CHECK(State.start_attempted&&!Unloads);
            if(f!=NO_RETURN)CHECK(State.terminal&&result!=EFI_SUCCESS);
        } else CHECK(result!=EFI_SUCCESS);
        unsigned calls=Calls;CHECK(StockRun(SELF,&Table,&Profile,&State)==EFI_ALREADY_STARTED&&Calls==calls);
        if(!State.terminal)for(unsigned i=0;i<Allocations;i++)CHECK(!Alive[i]);
        if(f==CORRUPT_IMAGE||f==SHORT_READ||f==FILE_SIZE_BAD)CHECK(!Loads);
        if(f==FILE_CLOSE_FAIL)CHECK(FileCloses==1&&!RootCloses&&!PoolFrees);
        if(f==LOAD_DIRTY)CHECK(State.terminal&&!Unloads&&!PoolFrees);
        if(f>=CHILD_WRONG_VOLUME&&f<=CHILD_NO_BASE)CHECK(State.terminal&&!Unloads&&!PoolFrees&&!State.child_verified);
        if(f>=LATE_CHILD_OPTIONS&&f<=LATE_CHILD_PATH)CHECK(State.terminal&&!Unloads&&!Starts);
        if(f==QUEUED_CONFIRM)CHECK(Keys==64&&Unloads==1&&!Starts);
    }
    /* Profile/path boundaries and standard SHA vectors, without firmware calls. */
    Setup(NORMAL);UINTN length;BOOLEAN usb,target;
    CHECK(StockPath(EspPath,sizeof(EspPath),&Profile,&length,&usb,&target)&&target&&!usb&&length==sizeof(EspPath));
    for(UINTN n=0;n<sizeof(EspPath);n++)CHECK(!StockPath(EspPath,n,&Profile,&length,&usb,&target));
    UINT8 digest[32];GuSha256("abc",3,digest);
    CHECK(GuHashMatches("abc",3,"ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"));
    CHECK(!GuHashMatches("abc",3,"ca7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"));
    for(unsigned f=0;f<8;f++) {
        Setup(NORMAL);Cases++;
        if(f==0)Boot.StartImage=NULL;
        if(f==1)Boot.LoadImage=NULL;
        if(f==2)Boot.UnloadImage=NULL;
        if(f==3)Profile.image_bytes=1;
        if(f==4)Profile.image_bytes=16u*1024u*1024u+1;
        if(f==5)memset(Profile.digest,0,32);
        if(f==6)memset(Profile.partition,0,16);
        if(f==7)Profile.start_lba=MAX_UINT64;
        CHECK(StockRun(SELF,&Table,&Profile,&State)!=EFI_SUCCESS&&!Calls&&!Starts&&!Loads);
    }
    printf("PASS: %u stock adapter cases / %u assertions; real StockRun, fake UEFI only; no GPU calls.\n",Cases,Checks);
    return 0;
}
