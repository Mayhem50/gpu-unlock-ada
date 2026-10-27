/* SPDX-License-Identifier: GPL-2.0-only */
#include "port.h"
#include <string.h>

static bool accessible(uint32_t v) {return (v&0xffff0000u)!=0xbadf0000u;}
bool msi_pre_flr_checks(const MsiOps *o,const uint32_t reference[64],MsiPreFlrEvidence *e) {
    static const uint32_t offsets[]={0x1fa7cc,0x9a0148,0x1fa7c4,0x823804};
    static const uint32_t expected[]={0xfffff0ff,0xffffffff,0xffffffff,0xffffffff};
    if(!o||!o->read_mmio||!e)return false;
    memset(e,0,sizeof(*e));
    if(!msi_config_matches(o,reference)||
       !o->read_mmio(o->context,0,&e->chip_before)||e->chip_before!=MSI_CHIP)return false;
    bool good=true;
    /* Preserve all four samples even after an earlier PLM failure. FBPA is
     * required to be accessible, NOT to equal its diagnostic expected value. */
    for(unsigned i=0;i<4;i++) {
        e->attempted_mask|=1u<<i;
        bool transport=o->read_mmio(o->context,offsets[i],&e->plm[i]);
        if(transport)e->transport_mask|=1u<<i;
        bool valid=transport&&accessible(e->plm[i]);
        if(valid)e->valid_mask|=1u<<i;
        if(!valid||(i!=1&&e->plm[i]!=expected[i]))good=false;
    }
    e->readout_valid=o->read_mmio(o->context,0x823818,&e->readout)&&
        e->readout!=0xffffffffu&&accessible(e->readout)&&!(e->readout&0x07ffffffu);
    if(!e->readout_valid)good=false;
    static const uint32_t remaining[]={0x82381c,0x823820,0x840100};
    for(unsigned i=0;i<3;i++) {
        uint32_t v=0;
        if(!o->read_mmio(o->context,remaining[i],&v)||!accessible(v)||v==0xffffffffu||
           (i==0&&v!=MSI_FULL_SS0)||(i==1&&v!=MSI_FULL_SS1)||(i==2&&!(v&0x10)))good=false;
    }
    if(!msi_config_matches(o,reference)||!o->read_mmio(o->context,0,&e->chip_after)||
       e->chip_after!=MSI_CHIP)good=false;
    return good;
}
