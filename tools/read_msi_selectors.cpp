// Targeted Windows read, PMXDRV wire layout from physicsdolphin ssdiag.
// No register write API and no --reapply path. GPL-2.0-only.
#define UNICODE
#define _UNICODE
#include <windows.h>
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <cwchar>
static bool forward(HANDLE h,DWORD code,unsigned char (&request)[24]){
 unsigned char input[16]={};auto address=(uint64_t)request;memcpy(input,&address,8);DWORD returned=0;
 return DeviceIoControl(h,code,input,sizeof(input),nullptr,0,&returned,nullptr)!=FALSE;
}
int wmain(int argc,wchar_t **argv){
 if(argc==2&&wcscmp(argv[1],L"--check-device")==0){
  HANDLE existing=CreateFileW(L"\\\\.\\PMXDRV",GENERIC_READ|GENERIC_WRITE,FILE_SHARE_READ|FILE_SHARE_WRITE,nullptr,OPEN_EXISTING,0,nullptr);
  if(existing!=INVALID_HANDLE_VALUE){CloseHandle(existing);fprintf(stderr,"PMXDRV already exists; refuse takeover\n");return 20;}
  DWORD error=GetLastError();if(error==ERROR_FILE_NOT_FOUND||error==ERROR_PATH_NOT_FOUND)return 0;
  fprintf(stderr,"PMXDRV availability check failed: %lu\n",error);return 21;
 }
 if(argc!=2){fwprintf(stderr,L"Expected one OS-discovered BAR0 physical base\n");return 2;}
 wchar_t *end=nullptr;uint64_t base=wcstoull(argv[1],&end,10);
 if(!base||!end||*end||(base&4095)||base>UINT64_MAX-0x1000000){fprintf(stderr,"Invalid BAR0\n");return 2;}
 HANDLE h=CreateFileW(L"\\\\.\\PMXDRV",GENERIC_READ|GENERIC_WRITE,FILE_SHARE_READ|FILE_SHARE_WRITE,nullptr,OPEN_EXISTING,0,nullptr);
 if(h==INVALID_HANDLE_VALUE){fprintf(stderr,"Open PMXDRV failed: %lu\n",GetLastError());return 3;}
 unsigned char request[24]={};uint32_t size=24,pages=4096;memcpy(request,&size,4);memcpy(request+4,&base,8);memcpy(request+12,&pages,4);
 if(!forward(h,0x222ab8,request)){fprintf(stderr,"Map IOCTL failed: %lu\n",GetLastError());CloseHandle(h);return 4;}
 uint64_t va=0;memcpy(&va,request+16,8);
 if(!va||va==0xbeef){fprintf(stderr,"Mapping rejected\n");CloseHandle(h);return 5;}
 const volatile uint32_t *mmio=(const volatile uint32_t*)(uintptr_t)va;
 int result=0;uint32_t chip=0,plm=0,ss0=0,ss1=0,rate=0,chipAfter=0;
 __try {
  chip=mmio[0];
  if(chip!=0x193000a1){fprintf(stderr,"Unexpected chip: %08x, stop before selector reads\n",chip);result=6;}
  else {plm=mmio[0x823804/4];ss0=mmio[0x82381c/4];ss1=mmio[0x823820/4];rate=mmio[0x823818/4];chipAfter=mmio[0];if(chipAfter!=chip)result=7;}
 } __except(EXCEPTION_EXECUTE_HANDLER){fprintf(stderr,"MMIO read exception: %lx\n",GetExceptionCode());result=8;}
 memset(request,0,sizeof(request));memcpy(request,&size,4);memcpy(request+16,&va,8);
 if(!forward(h,0x222abc,request)){fprintf(stderr,"Unmap IOCTL failed: %lu\n",GetLastError());result=9;}
 CloseHandle(h);
 if(result)return result;
 printf("{\"bar0\":\"0x%llx\",\"chip\":\"%08x\",\"FEAT_PLM\":\"%08x\",\"SS0\":\"%08x\",\"SS1\":\"%08x\",\"READOUT_1\":\"%08x\",\"chip_after\":\"%08x\",\"register_writes\":0,\"unmapped\":true}\n",base,chip,plm,ss0,ss1,rate,chipAfter);
 return 0;
}
