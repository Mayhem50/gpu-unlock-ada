/* SPDX-License-Identifier: MIT */
#include "core.h"
EFI_STATUS EFIAPI HybridCaptureMain(EFI_HANDLE,EFI_SYSTEM_TABLE *);
VOID HybridRequestDecodeTest(VOID);
static void Say(EFI_SYSTEM_TABLE *st,CHAR16 *message) {
    if(st->ConOut&&st->ConOut->OutputString)st->ConOut->OutputString(st->ConOut,message);
}
EFI_STATUS EFIAPI UefiMain(EFI_HANDLE image,EFI_SYSTEM_TABLE *st) {
    if(!image||!st||!st->BootServices||!st->BootServices->Stall||!st->ConIn||!st->ConIn->ReadKeyStroke)return EFI_INVALID_PARAMETER;
    Say(st,L"\r\nMSI 2717 HYBRID D2 - affichage Intel preserve\r\nD ou Entree : lecture seule. T : decode memoire PCI temporaire, puis restauration.\r\nT : aucun payload, DMA, reset ou unlock. Echap : retour. U desactive.\r\nDelai 30 secondes : retour sans capture.\r\n");
    for(UINTN i=0;i<300;i++) {
        EFI_INPUT_KEY key={0};EFI_STATUS s=st->ConIn->ReadKeyStroke(st->ConIn,&key);
        if(s==EFI_SUCCESS) {
            if(key.UnicodeChar=='d'||key.UnicodeChar=='D'||key.UnicodeChar=='\r')return HybridCaptureMain(image,st);
            if(key.UnicodeChar=='t'||key.UnicodeChar=='T'){HybridRequestDecodeTest();return HybridCaptureMain(image,st);}
            if(key.ScanCode==0x17)return EFI_ABORTED;
            if(key.UnicodeChar=='u'||key.UnicodeChar=='U')Say(st,L"U indisponible dans cet EFI. D pour le diagnostic hybride.\r\n");
        } else if(s!=EFI_NOT_READY)return s;
        s=st->BootServices->Stall(100000);if(EFI_ERROR(s))return s;
    }
    return EFI_TIMEOUT;
}
