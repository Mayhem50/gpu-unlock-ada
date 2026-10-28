/* SPDX-License-Identifier: GPL-2.0-only */
#include "admission.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static EFI_PCI_IO_PROTOCOL pci;
static MSI_ADMISSION state;
static unsigned calls,checks,cases,fail_at,no_output,reenter_at,mutate;
static unsigned variant,table_change_at;
static EFI_STATUS failure;
#define CHECK(x) do {checks++;if(!(x)){fprintf(stderr,"line %d: %s\n",__LINE__,#x);exit(1);}} while(0)
static EFI_STATUS Begin(EFI_PCI_IO_PROTOCOL *p) {
    CHECK(p==&pci);calls++;
    if(calls==table_change_at)p->Pci.Read=NULL;
    if(calls==reenter_at)CHECK(!MsiObservePci(p,&state));
    return calls==fail_at?failure:EFI_SUCCESS;
}
static EFI_STATUS EFIAPI Location(EFI_PCI_IO_PROTOCOL *p,UINTN *s,UINTN *b,UINTN *d,UINTN *f) {
    EFI_STATUS r=Begin(p);if(r!=EFI_SUCCESS || calls==no_output)return r;
    *s=0;*b=1;*d=0;*f=0;if(mutate==1)*b=2;return r;
}
static EFI_STATUS EFIAPI Attributes(EFI_PCI_IO_PROTOCOL *p,EFI_PCI_IO_PROTOCOL_ATTRIBUTE_OPERATION op,UINT64 mask,UINT64 *out) {
    EFI_STATUS r=Begin(p);
    CHECK(mask==0);CHECK(out!=NULL);
    CHECK(op==EfiPciIoAttributeOperationGet || op==EfiPciIoAttributeOperationSupported);
    if(r!=EFI_SUCCESS || calls==no_output)return r;
    *out=op==EfiPciIoAttributeOperationGet?0x300:0x700;
    if(mutate==2 && op==EfiPciIoAttributeOperationGet)*out=3;
    if(mutate==3 && op==EfiPciIoAttributeOperationSupported)*out=0x300;
    if(mutate==4 && op==EfiPciIoAttributeOperationGet)*out=0x700;
    if(mutate==5 && op==EfiPciIoAttributeOperationGet)*out=0xb00;
    if(mutate==6 && calls>4 && op==EfiPciIoAttributeOperationSupported)*out=0xf00;
    if(mutate==16 && op==EfiPciIoAttributeOperationGet)*out=(UINT64)variant<<8;
    return r;
}
static EFI_STATUS EFIAPI Read(EFI_PCI_IO_PROTOCOL *p,EFI_PCI_IO_PROTOCOL_WIDTH width,UINT32 offset,UINTN count,VOID *out) {
    UINT32 *v=out;EFI_STATUS r=Begin(p);
    CHECK(width==EfiPciIoWidthUint32);CHECK(offset==0);CHECK(count==64);
    if(r!=EFI_SUCCESS || calls==no_output)return r;
    memset(v,0,256);v[0]=0x271710de;v[1]=3;v[2]=0x030000a1;v[11]=0x13cd1462;
    if(mutate==7)v[0]^=1;
    if(mutate==8)v[11]^=1;
    if(mutate==9)v[2]^=1;
    if(mutate==10)v[2]=0x020000a1;
    if(mutate==11)v[3]=0x10000;
    if(mutate==12)v[1]=7;
    if(mutate==13 && calls>4)v[63]=1;
    if(mutate==14)p->Pci.Read=NULL;
    if(mutate==15)v[63]=MAX_UINT32; /* partial success, unchanged poison */
    if(mutate==16)v[1]=variant;
    if(mutate==17 && calls>4)v[variant]^=1;
    return r;
}
static VOID Reset(VOID) {
    cases++;memset(&pci,0,sizeof(pci));memset(&state,0,sizeof(state));
    pci.GetLocation=Location;pci.Attributes=Attributes;pci.Pci.Read=Read;
    calls=fail_at=no_output=reenter_at=mutate=variant=table_change_at=0;failure=EFI_DEVICE_ERROR;
}
int main(void) {
    unsigned i,j;
    Reset();CHECK(MsiObservePci(&pci,&state));CHECK(calls==8);CHECK(state.observed);CHECK(state.completed==255);
    CHECK(!MsiObservePci(&pci,&state));CHECK(calls==8);CHECK(state.violation);CHECK(!state.observed);
    for(i=1;i<=8;i++)for(j=0;j<3;j++) {
        Reset();fail_at=i;failure=j==0?EFI_DEVICE_ERROR:j==1?EFI_UNSUPPORTED:EFI_WARN_UNKNOWN_GLYPH;
        CHECK(!MsiObservePci(&pci,&state));CHECK(calls==i);CHECK(!state.observed);
        CHECK(state.status[i-1]==failure);CHECK(state.completed==(((UINTN)1<<(i-1))-1));
        CHECK(!MsiObservePci(&pci,&state));CHECK(calls==i);
    }
    for(i=1;i<=8;i++) {
        Reset();no_output=i;CHECK(!MsiObservePci(&pci,&state));CHECK(!state.observed);
        Reset();reenter_at=i;CHECK(!MsiObservePci(&pci,&state));CHECK(calls==i);CHECK(state.violation);
    }
    for(i=1;i<=15;i++) {Reset();mutate=i;CHECK(!MsiObservePci(&pci,&state));CHECK(!state.observed);}
    for(i=1;i<=8;i++) {
        Reset();table_change_at=i;CHECK(!MsiObservePci(&pci,&state));CHECK(calls==i);CHECK(!state.observed);
        Reset();no_output=i;CHECK(!MsiObservePci(&pci,&state));CHECK(calls==i);
    }
    for(i=0;i<8;i++) {
        Reset();mutate=16;variant=i;CHECK(MsiObservePci(&pci,&state)==(i==3));
    }
    for(i=0;i<64;i++) {
        Reset();mutate=17;variant=i;CHECK(!MsiObservePci(&pci,&state));CHECK(!state.observed);
    }
    Reset();pci.GetLocation=NULL;CHECK(!MsiObservePci(&pci,&state));CHECK(calls==0);
    Reset();pci.Attributes=NULL;CHECK(!MsiObservePci(&pci,&state));CHECK(calls==0);
    Reset();pci.Pci.Read=NULL;CHECK(!MsiObservePci(&pci,&state));CHECK(calls==0);
    Reset();CHECK(!MsiObservePci(NULL,&state));CHECK(!MsiObservePci(&pci,NULL));CHECK(calls==0);
    printf("Admission: %u cases / %u assertions; fake services only\n",cases,checks);return 0;
}
