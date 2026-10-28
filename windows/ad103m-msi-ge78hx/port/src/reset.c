/* SPDX-License-Identifier: GPL-2.0-only */
#include "reset_internal.h"
#include <string.h>

bool msi_stock_retention(bool *retained,const MsiLifecycle *life,bool readable) {
    if(!retained)return false;
    bool clean=readable&&life&&!life->owner_violation&&!life->resets&&!life->restores&&
        !life->unmaps&&!life->frees&&!life->claim_closes&&!life->live_dma;
    if(!clean)*retained=true;
    return clean&&!*retained;
}
bool msi_reset_gate_take(MsiResetGate *gate,bool authorized) {
    if(!gate)return false;
    if(gate->spent||gate->violation||!authorized){gate->violation=true;return false;}
    gate->spent=true;return true;
}
static bool config_matches(const MsiOps *o,const uint32_t reference[64],bool decode_off) {
    for(unsigned i=0;i<64;i++) {
        uint32_t value,expected=reference[i];
        if(i==1&&decode_off)expected&=~7u;
        if(!o->read_config(o->context,i*4,4,&value)||value!=expected)return false;
    }
    return true;
}
bool msi_config_matches(const MsiOps *o,const uint32_t reference[64]) {
    return o&&o->read_config&&reference&&msi_capabilities(reference)&&config_matches(o,reference,false);
}
bool msi_lifecycle_expected(const MsiOps *o,unsigned resets,unsigned live_dma) {
    MsiLifecycle now={0};
    return o&&o->lifecycle&&o->lifecycle(o->context,&now)&&!now.owner_violation&&
        now.resets==resets&&now.restores==resets&&!now.unmaps&&!now.frees&&
        !now.claim_closes&&now.live_dma==live_dma;
}
static bool measured_delay(const MsiOps *o,unsigned usec,MsiDelayEvidence *e) {
    *e=(MsiDelayEvidence){0};e->requested_us=usec;
    bool before=o->clock_us(o->context,&e->before);
    /* Even if the clock is unavailable, attempt the full requested delay.
     * Never turn its return status into elapsed-time evidence. */
    e->service_ok=o->stall(o->context,usec);
    bool after=o->clock_us(o->context,&e->after);
    e->clock_valid=before&&after&&e->after>=e->before;
    return e->service_ok&&e->clock_valid&&e->after-e->before>=usec;
}
bool msi_reset_owned(const MsiOps *o,MsiSession *s,const uint32_t reference[64],unsigned live_dma) {
    uint32_t saved[64],v=0,command=0;
    if(!o||!o->stall||!o->clock_us||!s||s->quiet_uncertain||s->state!=MSI_RESET_READY||!reference||s->flrs||!s->started||!s->dma_retained||!msi_capabilities(reference))return false;
    s->state=MSI_RESET_VERIFYING; /* Spend entry before any callback; no recursive owner entry. */
    /* Copy the preflight reference, never promote post-transaction state. */
    uint32_t approved[64];memcpy(approved,reference,sizeof(approved));
    for(unsigned i=0;i<64;i++)if(!o->read_config(o->context,i*4,4,&saved[i]))return false;
    if(memcmp(saved,approved,sizeof(saved))||saved[0]!=MSI_ID||saved[11]!=MSI_SUBSYSTEM||(saved[1]&4)||
       !(saved[(MSI_PCIE+4)/4]&(1u<<28))||(saved[(MSI_PCIE+8)/4]&0x8000))return false;
    uint64_t begin=0,now=0;
    MsiDelayEvidence poll;
    if(!o->clock_us(o->context,&begin))return false;
    for(;;) {
        if(!o->clock_us(o->context,&now)||now<begin||now-begin>100000)return false;
        if(!o->read_config(o->context,MSI_PCIE+10,2,&v)||v==0xffff)return false;
        if(!o->clock_us(o->context,&now)||now<begin||now-begin>100000)return false;
        if(!(v&32))break;
        if(now-begin==100000)return false;
        unsigned remaining=(unsigned)(100000-(now-begin));
        if(!measured_delay(o,remaining<1000?remaining:1000,&poll))return false;
    }
    if(!config_matches(o,approved,false)||!msi_pre_flr_checks(o,approved,&s->pre_flr)||
       !msi_lifecycle_expected(o,0,live_dma))return false;
    s->flrs=1;s->state=MSI_RESET;
    s->quiet_uncertain=true; /* any failed write may already have reset */
    bool wrote=o->write_config(o->context,MSI_PCIE+8,2,(saved[(MSI_PCIE+8)/4]&0xffff)|0x8000);
    /* Even a failed FLR write may have taken effect: always enforce quiet time. */
    bool delayed=measured_delay(o,100000,&s->quiet);
    s->quiet_uncertain=!s->quiet.clock_valid||s->quiet.after-s->quiet.before<100000;
    if(!wrote||!delayed)return false;
    bool ready=false;
    begin=s->quiet.after;
    for(;;) {
        if(!o->clock_us(o->context,&now)||now<begin||now-begin>1000000)return false;
        bool read=o->read_config(o->context,0,4,&v);
        if(!o->clock_us(o->context,&now)||now<begin||now-begin>1000000)return false;
        if(read) {
            if(v==MSI_ID){ready=true;break;}
            if(v!=0&&v!=0xffffffff)return false;
        }
        if(now-begin==1000000)break;
        unsigned remaining=(unsigned)(1000000-(now-begin));
        if(!measured_delay(o,remaining<10000?remaining:10000,&poll))return false;
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
