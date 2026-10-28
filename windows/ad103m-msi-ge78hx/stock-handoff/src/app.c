/* SPDX-License-Identifier: GPL-2.0-only */
#include "stock.h"
#include "sha256.h"
static EFI_GUID LoadedGuid=EFI_LOADED_IMAGE_PROTOCOL_GUID;
static EFI_GUID FsGuid=EFI_SIMPLE_FILE_SYSTEM_PROTOCOL_GUID;
static EFI_GUID PathGuid=EFI_DEVICE_PATH_PROTOCOL_GUID;
static EFI_GUID InfoGuid=EFI_FILE_INFO_ID;
static const CHAR16 WindowsName[]=L"\\EFI\\Microsoft\\Boot\\bootmgfw.efi";
static BOOLEAN Equal(const VOID *a,const VOID *b,UINTN n) {
    const UINT8 *x=a,*y=b;for(UINTN i=0;i<n;i++)if(x[i]!=y[i])return FALSE;return TRUE;
}
static UINT16 R16(const UINT8 *p){return (UINT16)(p[0]|((UINT16)p[1]<<8));}
static UINT32 R32(const UINT8 *p){return (UINT32)R16(p)|((UINT32)R16(p+2)<<16);}
static UINT64 R64(const UINT8 *p){return R32(p)|((UINT64)R32(p+4)<<32);}
BOOLEAN StockPath(const UINT8 *p,UINTN cap,const STOCK_PROFILE *profile,
                  UINTN *length,BOOLEAN *usb,BOOLEAN *target) {
    if(!p||!profile||!length||!usb||!target||cap>4096)return FALSE;
    *length=0;*usb=FALSE;*target=FALSE;unsigned hd=0;
    for(UINTN at=0,nodes=0;nodes<64&&at+4<=cap;nodes++) {
        UINTN n=R16(p+at+2);if(n<4||n>cap-at)return FALSE;
        UINT8 type=p[at],sub=p[at+1];
        if(type==END_DEVICE_PATH_TYPE) {
            if(n!=4||sub!=END_ENTIRE_DEVICE_PATH_SUBTYPE)return FALSE;
            *length=at+4;return TRUE;
        }
        if(type==MESSAGING_DEVICE_PATH&&sub==MSG_USB_DP) {
            if(n!=sizeof(USB_DEVICE_PATH))return FALSE;*usb=TRUE;
        }
        if(type==MEDIA_DEVICE_PATH&&sub==MEDIA_HARDDRIVE_DP) {
            if(n!=sizeof(HARDDRIVE_DEVICE_PATH)||++hd>1)return FALSE;
            if(p[at+40]==2&&p[at+41]==2&&R32(p+at+4)==profile->partition_number&&
               R64(p+at+8)==profile->start_lba&&R64(p+at+16)==profile->sectors&&
               Equal(p+at+24,profile->partition,16))*target=TRUE;
        }
        at+=n;
    }
    return FALSE;
}
static BOOLEAN LoadedPath(const EFI_DEVICE_PATH_PROTOCOL *p) {
    const UINT8 *b=(const UINT8 *)p;UINTN n=4+sizeof(WindowsName);
    return b&&b[0]==MEDIA_DEVICE_PATH&&b[1]==MEDIA_FILEPATH_DP&&R16(b+2)==n&&
        Equal(b+4,WindowsName,sizeof(WindowsName))&&b[n]==END_DEVICE_PATH_TYPE&&
        b[n+1]==END_ENTIRE_DEVICE_PATH_SUBTYPE&&R16(b+n+2)==4;
}
/* A failed close/free/unload is not retried. Retain the journal and stop.
 * No OS image is unloaded after StartImage has been attempted. */
static BOOLEAN Cleanup(EFI_BOOT_SERVICES *b,STOCK_STATE *s) {
    if(s->file){if(!s->file->Close||s->file->Close(s->file)!=EFI_SUCCESS)goto uncertain;s->file=NULL;}
    if(s->root){if(!s->root->Close||s->root->Close(s->root)!=EFI_SUCCESS)goto uncertain;s->root=NULL;}
    if(s->bytes){if(b->FreePool(s->bytes)!=EFI_SUCCESS)goto uncertain;s->bytes=NULL;}
    if(s->path){if(b->FreePool(s->path)!=EFI_SUCCESS)goto uncertain;s->path=NULL;}
    if(s->handles){if(b->FreePool(s->handles)!=EFI_SUCCESS)goto uncertain;s->handles=NULL;}
    return TRUE;
uncertain:
    s->terminal=TRUE;return FALSE;
}
static EFI_STATUS Say(EFI_SYSTEM_TABLE *t,const CHAR16 *text) {
    return t->ConOut->OutputString(t->ConOut,(CHAR16 *)text);
}
EFI_STATUS StockRun(EFI_HANDLE image,EFI_SYSTEM_TABLE *t,const STOCK_PROFILE *profile,STOCK_STATE *s) {
    if(!s||s->entered)return EFI_ALREADY_STARTED;
    s->entered=TRUE;
    if(s->child||s->handles||s->root||s->file||s->bytes||s->path){s->terminal=TRUE;return EFI_COMPROMISED_DATA;}
    if(!image||!t||!profile||!t->BootServices||!t->ConOut||!t->ConOut->OutputString||
       !t->ConIn||!t->ConIn->ReadKeyStroke)return EFI_INVALID_PARAMETER;
    STOCK_PROFILE pin=*profile; /* callback-independent, private reference */
    UINT8 partition_bits=0,digest_bits=0;
    for(UINTN i=0;i<16;i++)partition_bits|=pin.partition[i];
    for(UINTN i=0;i<32;i++)digest_bits|=pin.digest[i];
    if(!pin.partition_number||!pin.start_lba||!pin.sectors||pin.image_bytes<4096||
       pin.image_bytes>16u*1024u*1024u||!partition_bits||!digest_bits||
       pin.start_lba>MAX_UINT64-pin.sectors)return EFI_INVALID_PARAMETER;
    EFI_BOOT_SERVICES calls=*t->BootServices,*b=&calls;
    if(!b->HandleProtocol||!b->LocateHandleBuffer||!b->AllocatePool||!b->FreePool||
       !b->LoadImage||!b->UnloadImage||!b->StartImage||!b->Stall)return EFI_UNSUPPORTED;
    EFI_STATUS status=EFI_UNSUPPORTED;EFI_HANDLE volume=NULL,own=NULL;
    BOOLEAN output_ok=TRUE;VOID *child_base=NULL;UINT64 child_size=0;
    EFI_LOADED_IMAGE_PROTOCOL *loaded=NULL;EFI_SIMPLE_FILE_SYSTEM_PROTOCOL *fs=NULL;
    EFI_DEVICE_PATH_PROTOCOL *dp=NULL,*base=NULL;UINTN path_bytes=0;
    s->stage=1;
    status=Say(t,L"\r\nMSI STAGE A v0.1 - Windows handoff ONLY. No GPU unlock/release/reset.\r\n");
    output_ok=status==EFI_SUCCESS;
    if(status!=EFI_SUCCESS)goto failed;
    status=b->HandleProtocol(image,&LoadedGuid,(VOID **)&loaded);
    if(status!=EFI_SUCCESS||!loaded||!loaded->DeviceHandle){status=EFI_UNSUPPORTED;goto failed;}
    own=loaded->DeviceHandle;
    status=b->HandleProtocol(own,&PathGuid,(VOID **)&dp);
    UINTN length=0;BOOLEAN usb=FALSE,target=FALSE;
    if(status!=EFI_SUCCESS||!StockPath((VOID *)dp,4096,&pin,&length,&usb,&target)||!usb||target) {
        status=EFI_ACCESS_DENIED;goto failed;
    }
    s->stage=2;UINTN count=0,matches=0;
    status=b->LocateHandleBuffer(ByProtocol,&FsGuid,NULL,&count,&s->handles);
    if(status!=EFI_SUCCESS&&s->handles){s->terminal=TRUE;return status;}
    if(status!=EFI_SUCCESS||!s->handles||!count||count>128){status=EFI_UNSUPPORTED;goto failed;}
    for(UINTN i=0;i<count;i++) {
        dp=NULL;status=b->HandleProtocol(s->handles[i],&PathGuid,(VOID **)&dp);
        if(status!=EFI_SUCCESS||!StockPath((VOID *)dp,4096,&pin,&length,&usb,&target)) {
            status=EFI_COMPROMISED_DATA;goto failed;
        }
        if(target) {
            if(usb||s->handles[i]==own){status=EFI_ACCESS_DENIED;goto failed;}
            matches++;volume=s->handles[i];base=dp;path_bytes=length;
        }
    }
    if(matches!=1){status=EFI_NOT_FOUND;goto failed;}
    status=b->HandleProtocol(volume,&FsGuid,(VOID **)&fs);
    if(status!=EFI_SUCCESS||!fs||!fs->OpenVolume){status=EFI_UNSUPPORTED;goto failed;}
    status=fs->OpenVolume(fs,&s->root);
    if(status!=EFI_SUCCESS&&s->root){s->terminal=TRUE;return status;}
    if(status!=EFI_SUCCESS||!s->root||!s->root->Open||!s->root->Close){status=EFI_UNSUPPORTED;goto failed;}
    s->stage=3;
    status=s->root->Open(s->root,&s->file,(CHAR16 *)WindowsName,EFI_FILE_MODE_READ,0);
    if(status!=EFI_SUCCESS&&s->file){s->terminal=TRUE;return status;}
    if(status!=EFI_SUCCESS||!s->file||!s->file->Read||!s->file->GetInfo||!s->file->Close){status=EFI_UNSUPPORTED;goto failed;}
    UINT64 info_words[128]={0};UINTN info_bytes=sizeof(info_words);
    status=s->file->GetInfo(s->file,&InfoGuid,&info_bytes,info_words);
    EFI_FILE_INFO *info=(VOID *)info_words;
    if(status!=EFI_SUCCESS||info_bytes<sizeof(EFI_FILE_INFO)||info_bytes>sizeof(info_words)||
       info->Size<sizeof(EFI_FILE_INFO)||info->Size>info_bytes||
       (info->Attribute&EFI_FILE_DIRECTORY)||info->FileSize!=pin.image_bytes){status=EFI_COMPROMISED_DATA;goto failed;}
    status=b->AllocatePool(EfiLoaderData,pin.image_bytes,&s->bytes);
    if(status!=EFI_SUCCESS&&s->bytes){s->terminal=TRUE;return status;}
    if(status==EFI_SUCCESS&&!s->bytes){s->terminal=TRUE;return EFI_COMPROMISED_DATA;}
    if(status!=EFI_SUCCESS||!s->bytes){status=EFI_OUT_OF_RESOURCES;goto failed;}
    for(UINTN at=0;at<pin.image_bytes;) {
        UINTN wanted=pin.image_bytes-at;if(wanted>1048576)wanted=1048576;UINTN n=wanted;
        status=s->file->Read(s->file,&n,(UINT8 *)s->bytes+at);
        if(status!=EFI_SUCCESS||n!=wanted){status=EFI_DEVICE_ERROR;goto failed;}at+=n;
    }
    UINT8 tail;UINTN n=1;status=s->file->Read(s->file,&n,&tail);
    if(status!=EFI_SUCCESS||n){status=EFI_COMPROMISED_DATA;goto failed;}
    UINT8 digest[32];GuSha256(s->bytes,pin.image_bytes,digest);
    if(!Equal(digest,pin.digest,32)){status=EFI_CRC_ERROR;goto failed;}
    s->stage=4;
    UINTN node_bytes=4+sizeof(WindowsName),total=path_bytes-4+node_bytes+4;
    status=b->AllocatePool(EfiLoaderData,total,&s->path);
    if(status!=EFI_SUCCESS&&s->path){s->terminal=TRUE;return status;}
    if(status==EFI_SUCCESS&&!s->path){s->terminal=TRUE;return EFI_COMPROMISED_DATA;}
    if(status!=EFI_SUCCESS||!s->path){status=EFI_OUT_OF_RESOURCES;goto failed;}
    UINT8 *full=s->path;for(UINTN i=0;i<path_bytes-4;i++)full[i]=((UINT8 *)base)[i];
    UINT8 *node=full+path_bytes-4;
    node[0]=MEDIA_DEVICE_PATH;node[1]=MEDIA_FILEPATH_DP;node[2]=(UINT8)node_bytes;node[3]=(UINT8)(node_bytes>>8);
    for(UINTN i=0;i<sizeof(WindowsName);i++)node[4+i]=((const UINT8 *)WindowsName)[i];node+=node_bytes;
    node[0]=END_DEVICE_PATH_TYPE;node[1]=END_ENTIRE_DEVICE_PATH_SUBTYPE;node[2]=4;node[3]=0;
    status=b->LoadImage(FALSE,image,(VOID *)s->path,s->bytes,pin.image_bytes,&s->child);
    if(status!=EFI_SUCCESS&&s->child){s->terminal=TRUE;return status;}
    if(status==EFI_SUCCESS&&!s->child){s->terminal=TRUE;return EFI_COMPROMISED_DATA;}
    if(status!=EFI_SUCCESS||!s->child){if(status==EFI_SUCCESS)status=EFI_DEVICE_ERROR;goto failed;}
    loaded=NULL;status=b->HandleProtocol(s->child,&LoadedGuid,(VOID **)&loaded);
    if(status!=EFI_SUCCESS||!loaded||loaded->DeviceHandle!=volume||loaded->ParentHandle!=image||
       !loaded->ImageBase||!loaded->ImageSize||loaded->ImageSize>128u*1024u*1024u||
       loaded->LoadOptionsSize||loaded->LoadOptions||!LoadedPath(loaded->FilePath)) {
        s->terminal=TRUE;return EFI_COMPROMISED_DATA;
    }
    s->child_verified=TRUE;
    child_base=loaded->ImageBase;child_size=loaded->ImageSize;
    if(!Cleanup(b,s))return EFI_DEVICE_ERROR;
    s->stage=5;
    /* Discard previously queued keys, bounded even for faulty firmware. */
    BOOLEAN drained=FALSE;
    for(UINTN i=0;i<64;i++) {
        EFI_INPUT_KEY key;status=t->ConIn->ReadKeyStroke(t->ConIn,&key);
        if(status==EFI_NOT_READY){drained=TRUE;break;}
        if(status!=EFI_SUCCESS)goto failed;
    }
    if(!drained){status=EFI_DEVICE_ERROR;goto failed;}
    status=Say(t,L"A5: Windows image hash/origin verified.\r\nW: start normal Windows. Esc or 30 seconds: ABORT. No unlock exists here.\r\n");
    output_ok=status==EFI_SUCCESS;
    if(status!=EFI_SUCCESS)goto failed;
    BOOLEAN confirm=FALSE;
    for(UINTN i=0;i<300;i++) {
        EFI_INPUT_KEY key;status=t->ConIn->ReadKeyStroke(t->ConIn,&key);
        if(status==EFI_SUCCESS) {
            if(key.UnicodeChar=='w'||key.UnicodeChar=='W'){confirm=TRUE;break;}
            if(key.ScanCode==0x17||key.UnicodeChar==27){status=EFI_ABORTED;goto failed;}
        } else if(status!=EFI_NOT_READY)goto failed;
        status=b->Stall(100000);if(status!=EFI_SUCCESS)goto failed;
    }
    if(!confirm){status=EFI_ABORTED;goto failed;}
    status=Say(t,L"A6: Starting pinned Windows Boot Manager. No GPU transaction was performed.\r\n");
    output_ok=status==EFI_SUCCESS;
    if(status!=EFI_SUCCESS)goto failed;
    /* The prompt and last console callback must not change the staged image's
     * ownership/options/path/base. Refuse without unloading uncertain ownership. */
    if(loaded->DeviceHandle!=volume||loaded->ParentHandle!=image||loaded->ImageBase!=child_base||
       loaded->ImageSize!=child_size||loaded->LoadOptionsSize||loaded->LoadOptions||!LoadedPath(loaded->FilePath)) {
        s->terminal=TRUE;return EFI_COMPROMISED_DATA;
    }
    s->stage=6;s->start_attempted=TRUE;
    status=b->StartImage(s->child,NULL,NULL);
    /* Any returned image may have changed platform state. No further API,
     * console, unload, cleanup, retry or ordinary EFI return is allowed. */
    s->terminal=TRUE;return status==EFI_SUCCESS?EFI_ABORTED:status;
failed:
    if(s->child&&!s->child_verified){s->terminal=TRUE;return status==EFI_SUCCESS?EFI_DEVICE_ERROR:status;}
    if(s->terminal||!Cleanup(b,s))return status==EFI_SUCCESS?EFI_DEVICE_ERROR:status;
    if(s->child) {
        EFI_STATUS unloaded=b->UnloadImage(s->child);
        if(unloaded!=EFI_SUCCESS){s->terminal=TRUE;return unloaded;}s->child=NULL;
    }
    /* Pre-start refusal only. Returning to the normal firmware menu is allowed. */
    if(output_ok) {
        CHAR16 report[]=L"\r\nStage A refused at A0; status 0x0000000000000000.\r\nWindows was not started. Use Windows Boot Manager.\r\n";
        static const CHAR16 hex[]=L"0123456789abcdef";
        for(UINTN i=0;i+1<sizeof(report)/sizeof(report[0]);i++) {
            if(report[i]=='A'&&report[i+1]=='0')report[i+1]=(CHAR16)('0'+s->stage);
            if(report[i]=='0'&&report[i+1]=='x')
                for(UINTN j=0;j<16;j++)report[i+2+j]=hex[((UINT64)status>>(60-4*j))&15];
        }
        (VOID)Say(t,report);
    }
    return status==EFI_SUCCESS?EFI_ABORTED:status;
}
