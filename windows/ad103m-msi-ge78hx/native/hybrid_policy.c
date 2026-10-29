/* SPDX-License-Identifier: MIT */
#include "hybrid_policy.h"
HYBRID_PCI_STATE HybridPciState(const UINT8 c[256]) {
    HYBRID_PCI_STATE s={0};UINT8 seen[256]={0};UINTN at,steps=0;
    s.reason="identity_or_revision";
    if(!c||!ExactTarget(c,256)||c[8]!=0xa1||(c[14]&0x7f)!=0)return s;
    s.exact=TRUE;s.command=Read16(c+4);s.reason="capability_list_unavailable";
    if(!(Read16(c+6)&0x10))return s;
    at=c[0x34];
    while(at) {
        if(++steps>48||at<0x40||at>0xfc||(at&3)||seen[at]) {
            s.reason="malformed_capability_list";return s;
        }
        seen[at]=1;
        if(c[at]==1) {
            if(s.pm_present||at>0xf8||(Read16(c+at+2)&7)<1||(Read16(c+at+2)&7)>3) {
                s.reason="malformed_pm_capability";return s;
            }
            s.pm_present=TRUE;s.pm_offset=(UINT16)at;s.pmcsr=Read16(c+at+4);
        }
        at=c[at+1];
    }
    s.capabilities_valid=TRUE;
    if(!s.pm_present){s.reason="pm_state_unavailable";return s;}
    if((s.pmcsr&3)!=0){s.reason="gpu_not_d0_no_power_change";return s;}
    if(!(s.command&2)){s.reason="memory_decode_disabled_no_enable";return s;}
    if(s.command&4){s.reason="bus_master_active_no_claim";return s;}
    if(Read32(c+0x30)&1){s.reason="expansion_rom_decode_active";return s;}
    s.mmio_read_allowed=TRUE;s.reason="d0_memory_decode_ready_read_only";return s;
}
