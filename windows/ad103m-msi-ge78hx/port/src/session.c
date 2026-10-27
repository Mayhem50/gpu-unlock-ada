/* SPDX-License-Identifier: GPL-2.0-only */
#include "port.h"
#include <string.h>

static bool ready_ops(const MsiOps *o) {
    return o&&o->observe&&o->snapshot&&o->stage_windows&&o->silence_console&&o->disconnect&&o->claim&&o->restore_decode&&
        o->transaction&&o->read_config&&o->write_config&&o->stall&&o->read_mmio&&
        o->release_dma&&o->close_claim&&o->close_log&&o->start_windows;
}
static bool unchanged(const MsiOps *o,const MsiObservation *baseline) {
    MsiObservation now;
    if(!o->observe(o->context,&now))return false;
    return !memcmp(now.config,baseline->config,sizeof(now.config))&&
        !memcmp(now.reg,baseline->reg,sizeof(now.reg))&&now.bar0_base==baseline->bar0_base&&
        now.bar0_bytes==baseline->bar0_bytes&&now.segment==baseline->segment&&now.bus==baseline->bus&&
        now.device==baseline->device&&now.function==baseline->function;
}
static bool snapshot(const MsiOps *o,const MsiGraph *base,unsigned stage,bool exclusive) {
    MsiGraph now;
    return o->snapshot(o->context,&now)&&msi_graph_stage(base,&now,stage,exclusive);
}
static bool clear_master(const MsiOps *o) {
    uint32_t command=0,after=0;
    if(!o->read_config(o->context,4,2,&command)||command==0xffff)return false;
    if((command&4)&&!o->write_config(o->context,4,2,command&~4u))return false;
    return o->read_config(o->context,4,2,&after)&&after!=0xffff&&!(after&4);
}
static bool restore_decode(const MsiOps *o,const MsiObservation *base) {
    uint32_t now[64];unsigned missing;
    for(unsigned i=0;i<64;i++)if(!o->read_config(o->context,i*4,4,&now[i]))return false;
    missing=(base->config[1]&~now[1])&3;
    if(now[1]&4)return false;
    for(unsigned i=0;i<64;i++)if((now[i]|(i==1?missing:0))!=base->config[i])return false;
    return (!missing||o->restore_decode(o->context,missing))&&unchanged(o,base);
}
static bool payload_readbacks(const MsiOps *o) {
    static const uint32_t offsets[]={0x82381c,0x823820,0x823804,0x1fa7c4,0x1fa7cc,0x840100};
    static const uint32_t expected[]={MSI_FULL_SS0,MSI_FULL_SS1,0xffffffff,0xffffffff,0xfffff0ff,0x10};
    for(unsigned i=0;i<6;i++) {
        uint32_t v;
        if(!o->read_mmio(o->context,offsets[i],&v))return false;
        if(i==5) {
            if(v==0xffffffff||(v&0xffff0000)==0xbadf0000||!(v&0x10))return false;
        } else if(v!=expected[i])return false;
    }
    return true;
}
void msi_run(const MsiOps *o,MsiSession *s,const MsiObservation *observation,
             const uint8_t *prom,size_t bytes,const MsiQualification *q,bool consent) {
    MsiGraph base;MsiProof proof={0};
    bool log_attempted=false;
    if(!s||s->state!=MSI_NEW)return; /* one attempt per session, no retry */
    if(!ready_ops(o)||!q||!q->windows_image_reviewed){s->state=MSI_REFUSED;return;}
    s->failed_stage=1;
    if(!o->stage_windows(o->context))goto failed;
    s->state=MSI_STAGED;
    if(!consent) {
        s->failed_stage=2;log_attempted=true;
        if(!o->close_log(o->context))goto failed;
        s->state=MSI_WINDOWS_INVOKED;s->windows_calls=1;o->start_windows(o->context);
        /* A returned StartImage is an error, including an EFI_SUCCESS return. */
        s->state=MSI_STOPPED;return;
    }
    s->failed_stage=3;
    if(!q->ownership_reviewed||!q->console_callbacks_reviewed||!q->assets_verified||
       !msi_profile(observation,prom,bytes)||!unchanged(o,observation)||
       !o->snapshot(o->context,&base)||!msi_graph_stage(&base,&base,0,false))goto failed;
    s->failed_stage=4;
    if(!o->silence_console(o->context))goto failed;
    for(unsigned stage=1;stage<=3;stage++) {
        s->failed_stage=4+stage;
        if(!unchanged(o,observation)||!snapshot(o,&base,stage-1,false))goto failed;
        /* Disconnect may partially mutate even when it returns an error. */
        s->mutated=true;s->stops++;
        if(!o->disconnect(o->context,stage)||!snapshot(o,&base,stage,false))goto failed;
    }
    s->state=MSI_RELEASED;s->failed_stage=8;s->claim_attempted=true;
    if(!o->claim(o->context)||!snapshot(o,&base,3,true)||!restore_decode(o,observation))goto failed;
    s->state=MSI_TRANSACTION;s->failed_stage=9;s->started=true;s->dma_retained=true;s->transactions=1;
    if(!o->transaction(o->context,&proof)||!proof.completed||proof.marker!=MSI_MARKER||
       !proof.halted||!proof.permissions||!proof.selectors||!proof.dma_live||proof.dma_live>128||
       !payload_readbacks(o)||!snapshot(o,&base,3,true))goto failed;
    s->failed_stage=10;
    if(!clear_master(o)||!msi_reset(o,s)||!snapshot(o,&base,3,true))goto failed;
    s->failed_stage=11;
    if(!o->release_dma(o->context))goto failed;
    s->dma_retained=false;
    s->failed_stage=12;
    if(!o->close_claim(o->context)||!snapshot(o,&base,3,false))goto failed;
    s->state=MSI_CLEAN;s->failed_stage=13;log_attempted=true;
    if(!o->close_log(o->context))goto failed;
    s->failed_stage=14;
    if(!snapshot(o,&base,3,false)||!msi_post_reset(o))goto failed;
    s->failed_stage=15;s->state=MSI_WINDOWS_INVOKED;s->windows_calls=1;
    o->start_windows(o->context);
    /* Returning does not prove Windows, NVIDIA bind or display recovery. */
failed:
    if(s->mutated) {
        s->containment_verified=clear_master(o);
        s->state=MSI_STOPPED;
    } else s->state=MSI_REFUSED;
    if(!log_attempted)(void)o->close_log(o->context);
}
