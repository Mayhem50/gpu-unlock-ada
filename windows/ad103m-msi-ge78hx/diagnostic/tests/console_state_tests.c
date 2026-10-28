/* SPDX-License-Identifier: MIT. Fake firmware and CPU RAM only. */
#include "../src/console_state.h"
#include <Protocol/LoadedImage.h>
#include <Protocol/SimpleTextInEx.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static CS_CAPTURE C;
static struct {UINT8 image[24576];UINTN interfaces[4][6],outputs[2][10];} A;
static EFI_LOADED_IMAGE_PROTOCOL Image;
static EFI_SYSTEM_TABLE St;static EFI_BOOT_SERVICES Bs;
static EFI_GUID Loaded=EFI_LOADED_IMAGE_PROTOCOL_GUID,Input=EFI_SIMPLE_TEXT_INPUT_PROTOCOL_GUID,Ex=EFI_SIMPLE_TEXT_INPUT_EX_PROTOCOL_GUID;
static EFI_GUID Output=EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL_GUID;
static UINTN Checks,Cases,Mode,Calls,MapCalls;
static UINTN ExCalls;
static UINTN OtherCalls[4];
static EFI_GUID Pointer={0x31878c87,0x0b75,0x11d5,{0x9a,0x4f,0,0x90,0x27,0x3f,0xc1,0x4d}};
static EFI_GUID Absolute={0x8d59d32b,0xc655,0x4ae9,{0x9b,0x15,0xf2,0x59,4,0x99,0x2a,0x43}};
#define CHECK(x) do{Checks++;if(!(x)){fprintf(stderr,"console FAIL %d: %s\n",__LINE__,#x);exit(1);}}while(0)
static EFI_HANDLE Handles[]={(EFI_HANDLE)(UINTN)1,(EFI_HANDLE)(UINTN)2};
static const UINT8 File[]={4,6,20,0,0x7d,0x49,0x8a,0x62,0xf6,0x2b,0x64,0x42,0x87,0x41,6,0x9d,0xbd,0x33,0x99,0xd6,127,255,4,0};
static void Put16(UINT8 *p,UINT16 v){p[0]=(UINT8)v;p[1]=(UINT8)(v>>8);}
static void Put32(UINT8 *p,UINT32 v){for(unsigned i=0;i<4;i++)p[i]=(UINT8)(v>>(8*i));}
static EFI_STATUS EFIAPI Handle(EFI_HANDLE h,EFI_GUID *g,VOID **out) {
 Calls++;*out=NULL;
 if(BytesEqual(g,&Loaded,16)) {
  if(Mode==12)return EFI_DEVICE_ERROR;
  if(h==Handles[0]||Mode==14){*out=&Image;return EFI_SUCCESS;}return EFI_UNSUPPORTED;
 }
 if(BytesEqual(g,&Input,16)){OtherCalls[0]++;*out=Mode==26&&OtherCalls[0]==2?A.interfaces[2]:A.interfaces[0];return Mode==13?EFI_DEVICE_ERROR:EFI_SUCCESS;}
 if(BytesEqual(g,&Ex,16)){
  ExCalls++;*out=A.interfaces[1];
  if(Mode==24&&ExCalls==2)*out=A.interfaces[2];
  if(Mode==25&&ExCalls==2)return EFI_UNSUPPORTED;
  return Mode==17?EFI_UNSUPPORTED:EFI_SUCCESS;
 }
 if(BytesEqual(g,&Output,16)){*out=A.outputs[(UINTN)h==4?1:0];return EFI_SUCCESS;}
 if(BytesEqual(g,&Pointer,16)||BytesEqual(g,&Absolute,16)){
  UINTN index=BytesEqual(g,&Pointer,16)?2:3;OtherCalls[index]++;
  if(Mode==25+index){*out=OtherCalls[index]==2?A.interfaces[1]:A.interfaces[index];return EFI_SUCCESS;}
  return EFI_UNSUPPORTED;
 }
 return EFI_UNSUPPORTED;
}
static EFI_STATUS EFIAPI MemoryMap(UINTN *bytes,EFI_MEMORY_DESCRIPTOR *map,UINTN *key,UINTN *stride,UINT32 *version) {
 UINTN base=(UINTN)&A&~(UINTN)4095,end=((UINTN)&A+sizeof(A)+4095)&~(UINTN)4095;
 Calls++;MapCalls++;CHECK(*bytes==CS_MAP_CAP);
 if(Mode==5)return EFI_BUFFER_TOO_SMALL;
 if(Mode==15&&MapCalls==1)CsCapture(&C,&St,Handles,1);
 memset(map,0,sizeof(*map));map->Type=Mode==7?EfiMemoryMappedIO:EfiBootServicesData;
 map->PhysicalStart=base;map->NumberOfPages=(end-base)/4096;map->Attribute=Mode==8?(EFI_MEMORY_WB|EFI_MEMORY_RP):Mode==9?0:EFI_MEMORY_WB;
 *bytes=sizeof(*map);*stride=Mode==6?8:sizeof(*map);*version=EFI_MEMORY_DESCRIPTOR_VERSION;*key=Mode==10?MapCalls:1;
 if(MapCalls==2) {
  if(Mode==11)map->Attribute|=EFI_MEMORY_RO;
  if(Mode==16)A.image[0x2a0]^=1;
  if(Mode==18)A.image[0x5344]^=1;
  if(Mode==19)A.interfaces[0][2]^=1;
  if(Mode==21)A.outputs[0][1]^=1;
  if(Mode==22)St.ConOut=NULL;
  if(Mode==23)St.ConsoleInHandle=(EFI_HANDLE)(UINTN)5;
 }
 return EFI_SUCCESS;
}
static void Setup(UINTN mode) {
 Mode=mode;Calls=MapCalls=ExCalls=0;memset(OtherCalls,0,sizeof(OtherCalls));Cases++;memset(&C,0,sizeof(C));memset(&A,0,sizeof(A));memset(&Image,0,sizeof(Image));
 memset(&St,0,sizeof(St));memset(&Bs,0,sizeof(Bs));Bs.HandleProtocol=Handle;Bs.GetMemoryMap=MemoryMap;St.BootServices=&Bs;
 St.ConsoleInHandle=Handles[1];St.ConIn=(EFI_SIMPLE_TEXT_INPUT_PROTOCOL *)A.interfaces[0];
 St.ConsoleOutHandle=(EFI_HANDLE)(UINTN)3;St.StandardErrorHandle=(EFI_HANDLE)(UINTN)4;
 St.ConOut=(EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL *)A.outputs[0];St.StdErr=(EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL *)A.outputs[1];
 for(UINTN i=0;i<19648;i++)A.image[0x2a0+i]=(UINT8)i;
 Put16(A.image,0x5a4d);Put32(A.image+60,64);Put32(A.image+64,0x4550);Put16(A.image+68,0x8664);Put16(A.image+70,1);Put16(A.image+84,240);
 Put16(A.image+88,0x20b);Put32(A.image+144,24256);Put32(A.image+196,6);
 Put32(A.image+336,19648);Put32(A.image+340,0x2a0);Put32(A.image+364,0x20000000);
 memcpy(A.image+0x5a00,File,sizeof(File));Image.ImageBase=A.image;Image.ImageSize=24256;Image.ImageCodeType=EfiBootServicesCode;Image.FilePath=(EFI_DEVICE_PATH_PROTOCOL *)(A.image+0x5a00);
 for(UINTN i=0;i<4;i++)for(UINTN j=0;j<6;j++)A.interfaces[i][j]=(UINTN)(A.image+0x2a0+j);
 A.interfaces[0][0]=(UINTN)(A.image+0x3230);A.interfaces[0][1]=(UINTN)(A.image+0x32d4);
 const UINTN offsets[]={0x4188,0x4218,0x43dc,0x444c,0x44a8,0x4590,0x461c,0x4694,0x472c};
 for(unsigned i=0;i<2;i++)for(unsigned j=0;j<9;j++)A.outputs[i][j]=(UINTN)(A.image+offsets[j]);
 if(mode==20)A.outputs[0][1]^=1;
 A.interfaces[0][2]=1;A.interfaces[1][2]=3; /* Opaque, deliberately unreadable event cookies. */
 if(mode==1)A.image[0x2a0]^=1;if(mode==2)A.image[0x5a00]^=1;
 if(mode==3)Image.ImageSize--;if(mode==4)Image.ImageCodeType=EfiRuntimeServicesCode;
}
static void CaptureTests(void) {
 static CHAR8 json[16384];TEXT t;
 for(UINTN mode=0;mode<29;mode++) {
  Setup(mode);CsCapture(&C,&St,Handles,mode==14?2:1);
  if(mode==0||mode==17||mode==19)CHECK(C.slots_complete);else CHECK(!C.slots_complete);
  if(mode==0) {
   CHECK(C.pinned&&C.map_equal&&C.slots_equal&&C.final_equal);CHECK(C.words[0][0][2]==1);
   CHECK(C.slot_masks[0]==((1u<<CS_SLOT_COUNT)-1));
   TextInit(&t,json,sizeof(json));CsJson(&t,&C);CHECK(!t.failed);CHECK(strstr(json,"\"event_objects_dereferenced\":false"));
   FILE *f=fopen("build/console-state-host.json","wb");CHECK(f);CHECK(fwrite(json,1,t.len,f)==t.len);CHECK(fclose(f)==0);
   UINTN calls=Calls;CsCapture(&C,&St,Handles,1);CHECK(C.violation&&!C.slots_complete&&Calls==calls);
  }
  if(mode==14)CHECK(!strcmp(C.reason,"ambiguous_console_image"));
  if(mode==15)CHECK(C.violation&&C.slot_masks[0]==0);
  if(mode==16)CHECK(!strcmp(C.reason,"console_code_changed"));
  if(mode==18)CHECK(!C.final_equal);
  if(mode==19)CHECK(!C.interface_equal[0]);
  if(mode==20)CHECK(!C.binding_verified&&C.slot_masks[0]==0);
  if(mode==21||mode==22)CHECK(!C.binding_verified);
  if(mode==23)CHECK(!C.binding_verified);
  if(mode>=24)CHECK(!C.interfaces_revalidated&&!strcmp(C.reason,"input_protocol_changed"));
  if(mode==17)CHECK(!C.interface_read[1]);
 }
}
static void MapTests(void) {
 EFI_MEMORY_DESCRIPTOR map[2];memset(map,0,sizeof(map));
 map[0].Type=EfiBootServicesData;map[0].PhysicalStart=0x1000;map[0].NumberOfPages=1;map[0].Attribute=EFI_MEMORY_WB;
 CHECK(CsReadable((UINT8 *)map,sizeof(map[0]),sizeof(map[0]),1,0x1000,4096));
 CHECK(!CsReadable((UINT8 *)map,sizeof(map[0]),sizeof(map[0]),1,0x1fff,2));
 CHECK(!CsReadable((UINT8 *)map,sizeof(map[0]),sizeof(map[0]),2,0x1000,1));
 CHECK(!CsReadable((UINT8 *)map,sizeof(map[0])-1,sizeof(map[0]),1,0x1000,1));
 CHECK(!CsReadable((UINT8 *)map,sizeof(map[0]),8,1,0x1000,1));
 map[0].Attribute=EFI_MEMORY_WB|EFI_MEMORY_WC;CHECK(!CsReadable((UINT8 *)map,sizeof(map[0]),sizeof(map[0]),1,0x1000,1));
 map[0].Attribute=EFI_MEMORY_WB;
 map[1]=map[0];CHECK(!CsReadable((UINT8 *)map,sizeof(map),sizeof(map[0]),1,0x1000,1));
 map[1].PhysicalStart=0x2000;CHECK(CsReadable((UINT8 *)map,sizeof(map),sizeof(map[0]),1,0x1800,4096));
 map[1].PhysicalStart=0x3000;CHECK(!CsReadable((UINT8 *)map,sizeof(map),sizeof(map[0]),1,0x1800,4096));
 map[1].PhysicalStart=MAX_UINT64-4095;map[1].NumberOfPages=2;
 CHECK(!CsReadable((UINT8 *)map,sizeof(map),sizeof(map[0]),1,0x1000,1));
 CHECK(!CsReadable((UINT8 *)map,sizeof(map),sizeof(map[0]),1,MAX_UINTN-1,8));
}
int main(void){CaptureTests();MapTests();printf("PASS: %zu console cases / %zu checks; fake services, opaque events never dereferenced\n",Cases,Checks);return 0;}
