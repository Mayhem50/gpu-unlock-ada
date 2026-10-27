/* SPDX-License-Identifier: GPL-2.0-only */
#include "port.h"
#include <string.h>

static bool config_matches(const MsiOps *o,const uint32_t reference[64],bool decode_off) {
    for(unsigned i=0;i<64;i++) {
        uint32_t value,expected=reference[i];
        if(i==1&&decode_off)expected&=~7u;
        if(!o->read_config(o->context,i*4,4,&value)||value!=expected)return false;
    }
    return true;
}
bool msi_config_matches(const MsiOps *o,const uint32_t reference[64]) {
    return o&&reference&&msi_capabilities(reference)&&config_matches(o,reference,false);
}
bool msi_reset(const MsiOps *o,MsiSession *s,const uint32_t reference[64]) {
    uint32_t saved[64],v=0,command=0;
    if(!o||!s||!reference||s->flrs||!s->started||!s->dma_retained||!msi_capabilities(reference))return false;
    /* Copy the preflight reference, never promote post-transaction state. */
    uint32_t approved[64];memcpy(approved,reference,sizeof(approved));
    for(unsigned i=0;i<64;i++)if(!o->read_config(o->context,i*4,4,&saved[i]))return false;
    if(memcmp(saved,approved,sizeof(saved))||saved[0]!=MSI_ID||saved[11]!=MSI_SUBSYSTEM||(saved[1]&4)||
       !(saved[(MSI_PCIE+4)/4]&(1u<<28))||(saved[(MSI_PCIE+8)/4]&0x8000))return false;
    for(unsigned tries=0;;tries++) {
        if(!o->read_config(o->context,MSI_PCIE+10,2,&v)||v==0xffff)return false;
        if(!(v&32))break;
        if(tries==100)return false;
        o->stall(o->context,1000);
    }
    if(!config_matches(o,approved,false))return false;
    s->flrs=1;s->state=MSI_RESET;
    bool wrote=o->write_config(o->context,MSI_PCIE+8,2,(saved[(MSI_PCIE+8)/4]&0xffff)|0x8000);
    /* Even a failed FLR write may have taken effect: always enforce quiet time. */
    o->stall(o->context,100000);
    if(!wrote)return false;
    bool ready=false;
    for(unsigned tries=0;tries<=100;tries++) {
        if(o->read_config(o->context,0,4,&v)) {
            if(v==MSI_ID){ready=true;break;}
            if(v!=0&&v!=0xffffffff)return false;
        }
        if(tries<100)o->stall(o->context,10000);
    }
    uint32_t reset_config[64];
    if(!ready)return false;
    for(unsigned i=0;i<64;i++)if(!o->read_config(o->context,i*4,4,&reset_config[i]))return false;
    if(reset_config[0]!=approved[0]||reset_config[2]!=approved[2]||
       reset_config[11]!=approved[11]||!msi_capabilities(reset_config)||
       !o->read_config(o->context,4,2,&command)||command==0xffff||(command&4))return false;
    if(!o->write_config(o->context,4,2,command&~7u)||
       !o->read_config(o->context,4,2,&v)||(v&7))return false;
    /* Restore high BAR DWORDs before low DWORDs while decoding is disabled. */
    for(int offset=0x24;offset>=0x10;offset-=4)
        if(!o->write_config(o->context,(unsigned)offset,4,saved[offset/4]))return false;
    if(!o->write_config(o->context,0x30,4,saved[12]&~1u)||
       !o->write_config(o->context,0x0c,2,saved[3]&0xffff)||
       !o->write_config(o->context,0x3c,1,saved[15]&255)||
       !o->write_config(o->context,MSI_PCIE+8,2,saved[(MSI_PCIE+8)/4]&0x7fff)||
       !o->write_config(o->context,MSI_PCIE+0x28,2,saved[(MSI_PCIE+0x28)/4]&0xffff))return false;
    /* Reject ignored/misdirected restores before enabling any decoding. */
    if(!config_matches(o,approved,true)||
       !o->write_config(o->context,4,2,(approved[1]&0xffff)&~4u))return false;
    if(!o->read_config(o->context,4,2,&v)||v!=((saved[1]&0xffff)&~4u))return false;
    for(unsigned offset=0x10;offset<=0x24;offset+=4)
        if(!o->read_config(o->context,offset,4,&v)||v!=saved[offset/4])return false;
    return msi_post_reset(o,approved);
}
bool msi_post_reset(const MsiOps *o,const uint32_t reference[64]) {
    uint32_t command,value;
    if(!reference||!msi_capabilities(reference)||!config_matches(o,reference,false))return false;
    if(!o->read_config(o->context,4,2,&command)||command==0xffff||(command&7)!=3||
       !o->read_config(o->context,0,4,&value)||value!=MSI_ID||
       !o->read_config(o->context,0x2c,4,&value)||value!=MSI_SUBSYSTEM||
       !o->read_config(o->context,8,4,&value)||value!=0x030000a1||
       !o->read_config(o->context,0x30,4,&value)||(value&1)||
       !o->read_config(o->context,MSI_PM+4,2,&value)||(value&3)||
       !o->read_config(o->context,MSI_MSI+2,2,&value)||(value&1)||
       !o->read_config(o->context,MSI_PCIE+8,2,&value)||(value&0x8000)||
       !o->read_config(o->context,MSI_PCIE+10,2,&value)||(value&32))return false;
    static const uint32_t bars[]={0x5e000000,0xc,0x60,0xc,0x64,0x5001};
    for(unsigned i=0;i<6;i++)
        if(!o->read_config(o->context,0x10+4*i,4,&value)||value!=bars[i])return false;
    static const uint32_t offsets[]={0,0x110100,0x1fa828,0x82381c,0x823820,0x823818,0x823804};
    uint32_t values[7];
    for(unsigned i=0;i<7;i++)if(!o->read_mmio(o->context,offsets[i],&values[i])||
        (i!=6&&values[i]==0xffffffff)||(values[i]&0xffff0000)==0xbadf0000)return false;
    return values[0]==MSI_CHIP&&(values[1]&0x10)&&!(values[2]>>4)&&
           values[3]==MSI_FULL_SS0&&values[4]==MSI_FULL_SS1&&
           !(values[5]&0x07ffffff)&&values[6]==0xffffffff;
}
