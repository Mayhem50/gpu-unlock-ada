/* SPDX-License-Identifier: GPL-2.0-only */
#include "admission.h"
static BOOLEAN Finish(MSI_ADMISSION *s, UINTN i, EFI_STATUS status) {
    s->status[i]=status;
    if(status!=EFI_SUCCESS || s->violation)return FALSE;
    s->completed|=(UINTN)1<<i;
    return TRUE;
}
static BOOLEAN Stable(EFI_PCI_IO_PROTOCOL *p, EFI_PCI_IO_PROTOCOL_GET_LOCATION l,
                      EFI_PCI_IO_PROTOCOL_ATTRIBUTES a, EFI_PCI_IO_PROTOCOL_CONFIG r) {
    return p->GetLocation==l && p->Attributes==a && p->Pci.Read==r;
}
BOOLEAN MsiObservePci(EFI_PCI_IO_PROTOCOL *pci, MSI_ADMISSION *s) {
    EFI_PCI_IO_PROTOCOL_GET_LOCATION location;
    EFI_PCI_IO_PROTOCOL_ATTRIBUTES attributes;
    EFI_PCI_IO_PROTOCOL_CONFIG read;
    UINTN i,j,k;
    if(!s)return FALSE;
    if(s->entered){s->violation=TRUE;s->observed=FALSE;return FALSE;}
    s->entered=TRUE;s->observed=FALSE;
    if(!pci || !pci->GetLocation || !pci->Attributes || !pci->Pci.Read)return FALSE;
    location=pci->GetLocation;attributes=pci->Attributes;read=pci->Pci.Read;
    for(i=0;i<2;i++) {
        /* Poison every output so a success without outputs cannot admit it. */
        for(j=0;j<4;j++)s->location[i][j]=MAX_UINTN;
        s->supported[i]=s->attributes[i]=MAX_UINT64;
        for(j=0;j<64;j++)s->config[i][j]=MAX_UINT32;
        k=i*4;s->attempted|=(UINTN)1<<k;
        if(!Finish(s,k,location(pci,&s->location[i][0],&s->location[i][1],
                              &s->location[i][2],&s->location[i][3])))return FALSE;
        if(!Stable(pci,location,attributes,read))return FALSE;
        if(s->location[i][0]!=0 || s->location[i][1]!=1 ||
           s->location[i][2]!=0 || s->location[i][3]!=0)return FALSE;
        k++;s->attempted|=(UINTN)1<<k;
        if(!Finish(s,k,attributes(pci,EfiPciIoAttributeOperationSupported,0,&s->supported[i])))return FALSE;
        if(!Stable(pci,location,attributes,read) || s->supported[i]==MAX_UINT64)return FALSE;
        k++;s->attempted|=(UINTN)1<<k;
        if(!Finish(s,k,attributes(pci,EfiPciIoAttributeOperationGet,0,&s->attributes[i])))return FALSE;
        if(!Stable(pci,location,attributes,read) || s->attributes[i]==MAX_UINT64)return FALSE;
        k++;s->attempted|=(UINTN)1<<k;
        if(!Finish(s,k,read(pci,EfiPciIoWidthUint32,0,64,s->config[i])))return FALSE;
        if(!Stable(pci,location,attributes,read))return FALSE;
        for(j=0;j<64;j++)if(s->config[i][j]==MAX_UINT32)return FALSE;
        if(s->config[i][0]!=0x271710deu || s->config[i][11]!=0x13cd1462u ||
           (s->config[i][2]&0xff)!=0xa1 || (s->config[i][2]>>24)!=3 ||
           ((s->config[i][3]>>16)&0xff)!=0)return FALSE;
        /* PCI command bits 0..2 correspond to UEFI attributes 8..10. These
         * are different namespaces. No Set/Enable/Disable call is allowed. */
        if((s->supported[i]&EFI_PCI_DEVICE_ENABLE)!=EFI_PCI_DEVICE_ENABLE ||
           (s->attributes[i]&~s->supported[i]) ||
           (s->attributes[i]&EFI_PCI_DEVICE_ENABLE)!=((s->config[i][1]&7u)<<8) ||
           (s->config[i][1]&4u) || (s->config[i][1]&3u)!=3u)return FALSE;
    }
    for(j=0;j<4;j++)if(s->location[0][j]!=s->location[1][j])return FALSE;
    for(j=0;j<64;j++)if(s->config[0][j]!=s->config[1][j])return FALSE;
    if(s->supported[0]!=s->supported[1] || s->attributes[0]!=s->attributes[1] || s->violation)return FALSE;
    s->observed=TRUE;return TRUE;
}
