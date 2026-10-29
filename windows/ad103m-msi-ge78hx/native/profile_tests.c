/* SPDX-License-Identifier: GPL-2.0-only. Archived values, no device access. */
#include "build/source/profile.h"
#include "build/source/compat/compat.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "build/fixture.h"
GU_PLATFORM Gu;GU_ASSET GuAssets[5];
extern VOID GuSetProfileObservationOnly(VOID);
extern BOOLEAN GuProfileObservationOnly(VOID);
static unsigned checks;
#define CHECK(x) do {checks++;if(!(x)){fprintf(stderr,"native profile line %d\n",__LINE__);exit(1);}} while(0)
VOID GuLog(const char *format,...){(void)format;}
UINT32 GuRead32(UINT32 off){for(unsigned i=0;i<22;i++)if(reg_offsets[i]==off)return reg_values[i];Gu.fault=TRUE;return 0xffffffff;}
EFI_STATUS GuConfigRead(EFI_PCI_IO_PROTOCOL_WIDTH width,UINT32 off,VOID *v){CHECK(width==EfiPciIoWidthUint32&&off<256&&!(off&3));*(UINT32 *)v=config[off/4];return EFI_SUCCESS;}
static EFI_STATUS EFIAPI location(EFI_PCI_IO_PROTOCOL *p,UINTN *s,UINTN *b,UINTN *d,UINTN *f){(void)p;*s=*d=*f=0;*b=1;return EFI_SUCCESS;}
static EFI_STATUS EFIAPI config_read(EFI_PCI_IO_PROTOCOL *p,EFI_PCI_IO_PROTOCOL_WIDTH w,UINT32 o,UINTN n,VOID *v){(void)p;(void)w;(void)o;(void)n;(void)v;return EFI_SUCCESS;}
BOOLEAN GuEntrySelectorsMatch(UINT32 a,UINT32 b){return (BOOLEAN)(a==reg_values[12]&&b==reg_values[13]);}
BOOLEAN GuLatchEntrySelectors(UINT32 a,UINT32 b){return GuEntrySelectorsMatch(a,b);}
VOID *GuAlloc(size_t n){return malloc(n);}VOID GuFree(VOID *p){free(p);}
VOID GuCopyFromIo(VOID *d,const VOID *s,size_t n){(void)d;(void)s;(void)n;Gu.fault=TRUE;}
int main(void){EFI_PCI_IO_PROTOCOL pci={0};pci.GetLocation=location;pci.Pci.Read=config_read;Gu.pci=&pci;Gu.bar_base=GU_BAR0_BASE;Gu.bar_size=0x1000000;Gu.pm_cap=0x60;Gu.msi_cap=0x68;Gu.pcie_cap=0x78;Gu.pcie_version=2;
 CHECK(GuAd104ColdEntry()); /* Exact captured value, not a bitmask. */
 reg_values[12]=0x04203025u;CHECK(GuAd104ColdEntry());
 reg_values[12]=0x04203064u;CHECK(!GuAd104ColdEntry());
 reg_values[12]=0x04003065u;CHECK(GuAd104ColdEntry());
 reg_values[12]=0x04013065u;CHECK(GuAd104ColdEntry());
 UINT32 saved=config[0];config[0]=0x278210deu;CHECK(!GuAd104ColdEntry());config[0]=saved;
 saved=config[11];config[11]=0x51321462u;CHECK(!GuAd104ColdEntry());config[11]=saved;
 saved=config[4];config[4]=0x80000000u;CHECK(!GuAd104ColdEntry());config[4]=saved;
 reg_values[13]=1;CHECK(!GuAd104ColdEntry());reg_values[13]=0;
 CHECK(!GuAd104AssetsValid());
 reg_values[12]=0x05213065u;CHECK(GuAd104ColdEntry());
 reg_values[12]=0x05213064u;CHECK(!GuAd104ColdEntry());
 reg_values[12]=0x05213075u;CHECK(!GuAd104ColdEntry());
 CHECK(!GuProfileObservationOnly());GuSetProfileObservationOnly();CHECK(GuProfileObservationOnly());
 CHECK(GuAd104ColdEntry());
 reg_values[12]=0xffffffffu;CHECK(!GuAd104ColdEntry());
 reg_values[12]=0xbadf5620u;CHECK(!GuAd104ColdEntry());
 reg_values[12]=0x88888888u;CHECK(!GuAd104ColdEntry());
 puts("Native MSI profile: exact captured SS0 admitted; unknown values and AD104 identity refused; CPU tests only.");printf("%u checks\n",checks);return 0;}
