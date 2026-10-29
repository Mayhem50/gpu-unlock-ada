/* SPDX-License-Identifier: MIT. Host-only refusal and map tests. */
#include "../src/console_state.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static CS_CAPTURE C;
static UINTN Checks,ServiceCalls;
static EFI_STATUS EFIAPI TrapMap(UINTN *a,EFI_MEMORY_DESCRIPTOR *b,UINTN *c,UINTN *d,UINT32 *e) {
 (void)a;(void)b;(void)c;(void)d;(void)e;ServiceCalls++;return EFI_DEVICE_ERROR;
}
static EFI_STATUS EFIAPI TrapHandle(EFI_HANDLE a,EFI_GUID *b,VOID **c) {
 (void)a;(void)b;(void)c;ServiceCalls++;return EFI_DEVICE_ERROR;
}
#define CHECK(x) do{Checks++;if(!(x)){fprintf(stderr,"console FAIL %d: %s\n",__LINE__,#x);exit(1);}}while(0)
static void CaptureTests(void) {
 static CHAR8 json[16384];TEXT t;FILE *f;EFI_SYSTEM_TABLE st;EFI_BOOT_SERVICES bs;EFI_HANDLE handle=(EFI_HANDLE)(UINTN)1;
 /* Invalid borrowed objects would fault if either pointers or firmware were read. */
 memset(&C,0,sizeof(C));
 CsCapture(&C,(EFI_SYSTEM_TABLE *)(UINTN)1,(EFI_HANDLE *)(UINTN)1,1);
 CHECK(C.entered&&!C.slots_complete&&!C.pinned&&!C.binding_verified);
 CHECK(!C.interfaces_revalidated&&!C.map_called[0]&&!C.map_called[1]);
 CHECK(!strcmp(C.reason,"interface_lifetime_unproven"));
 for(unsigned i=0;i<4;i++)CHECK(!C.interface_read[i]);
 TextInit(&t,json,sizeof(json));CsJson(&t,&C);CHECK(!t.failed);
 CHECK(strstr(json,"\"reason\":\"interface_lifetime_unproven\""));
 CHECK(strstr(json,"\"interfaces_current_known\":false"));
 f=fopen("build/console-state-host.json","wb");CHECK(f);CHECK(fwrite(json,1,t.len,f)==t.len);CHECK(fclose(f)==0);
 CsCapture(&C,(EFI_SYSTEM_TABLE *)(UINTN)1,(EFI_HANDLE *)(UINTN)1,1);
 CHECK(C.violation&&!C.slots_complete&&!strcmp(C.reason,"reentry_refused"));
 CsCapture(NULL,(EFI_SYSTEM_TABLE *)(UINTN)1,(EFI_HANDLE *)(UINTN)1,1);
 memset(&C,0,sizeof(C));memset(&st,0,sizeof(st));memset(&bs,0,sizeof(bs));
 bs.GetMemoryMap=TrapMap;bs.HandleProtocol=TrapHandle;st.BootServices=&bs;
 CsCapture(&C,&st,&handle,1);
 CHECK(ServiceCalls==0&&!C.slots_complete&&!strcmp(C.reason,"interface_lifetime_unproven"));
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
int main(void){CaptureTests();MapTests();printf("PASS: %zu console refusal/map checks; no firmware access\n",Checks);return 0;}
