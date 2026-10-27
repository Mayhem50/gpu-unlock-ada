/* SPDX-License-Identifier: GPL-2.0-only */
#include "reset_internal.h"
#include <string.h>

static bool ready_ops(const MsiOps *o) {
    return o&&o->observe&&o->snapshot&&o->stage_windows&&o->silence_console&&o->disconnect&&o->claim&&o->restore_decode&&
        o->pre_reset.boundary==MSI_PRE_RESET_RETAINED_V1&&o->pre_reset.run&&o->lifecycle&&
        o->read_config&&o->write_config&&o->stall&&o->read_mmio&&
        o->begin_guard&&o->guard_valid&&o->prepare_dma_release&&o->prepare_claim_close&&o->close_log&&o->commit_windows;
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
static bool lifecycle_checked(const MsiOps *o,MsiSession *s,unsigned resets,unsigned live) {
    if(msi_lifecycle_expected(o,resets,live))return true;
    /* A failed observation is not evidence that no owned resources exist. */
    s->dma_retained=true;s->terminal_required=true;return false;
}
void msi_run(const MsiOps *o,MsiSession *s,const MsiObservation *observation,
             const uint8_t *prom,size_t bytes,const MsiQualification *q,bool consent) {
    MsiGraph base;MsiProof proof={0};MsiGuard guard={0};
    bool log_attempted=false;
    if(!s||s->state!=MSI_NEW)return; /* one attempt per session, no retry */
    s->state=MSI_ENTERED; /* Consume entry before the first external callback. */
    if(!ready_ops(o)||!q||!q->windows_image_reviewed){s->state=MSI_REFUSED;return;}
    MsiOps private_ops=*o;MsiQualification private_q=*q;
    MsiObservation reference;
    if(consent&&!observation){s->state=MSI_REFUSED;return;}
    if(consent)reference=*observation;
    o=&private_ops;q=&private_q;observation=&reference;
    if(!lifecycle_checked(o,s,0,0)){s->state=MSI_STOPPED;return;}
    s->failed_stage=1;
    if(!o->stage_windows(o->context))goto failed;
    if(!lifecycle_checked(o,s,0,0))goto failed;
    s->state=MSI_STAGED;
    if(!consent) {
        s->failed_stage=2;log_attempted=true;s->stock_cleanup_started=true;
        bool closed=o->close_log(o->context);
        if(!closed)s->cleanup_failed=true;
        if(!lifecycle_checked(o,s,0,0)||!closed)goto failed;
        s->guard_attempted=s->guard_retained=true;
        bool began=o->begin_guard(o->context,MSI_GUARD_STOCK,NULL,&guard);
        bool valid=began&&guard.epoch&&guard.purpose==MSI_GUARD_STOCK&&o->guard_valid(o->context,&guard);
        /* Check after the last callback even if lease acquisition failed. */
        if(!lifecycle_checked(o,s,0,0)||!valid)goto failed;
        s->state=MSI_WINDOWS_INVOKED;s->windows_calls=1;o->commit_windows(o->context,&guard,&s->dma_retained);
        /* A returned StartImage is an error, including an EFI_SUCCESS return. */
        goto failed;
    }
    s->failed_stage=3;
    if(!q->ownership_reviewed||!q->console_callbacks_reviewed||!q->assets_verified||
       !msi_profile(observation,prom,bytes)||!unchanged(o,observation)||
       !o->snapshot(o->context,&base)||!msi_graph_stage(&base,&base,0,false))goto failed;
    s->failed_stage=4;
    /* Even a failed silence may have changed console callbacks/state. */
    s->console_may_be_changed=true;
    if(!o->silence_console(o->context))goto failed;
    for(unsigned stage=1;stage<=3;stage++) {
        s->failed_stage=4+stage;
        if(!unchanged(o,observation)||!snapshot(o,&base,stage-1,false))goto failed;
        /* Disconnect may partially mutate even when it returns an error. */
        s->mutated=true;s->stops++;
        if(!o->disconnect(o->context,stage)||!snapshot(o,&base,stage,false))goto failed;
    }
    s->state=MSI_RELEASED;s->failed_stage=8;s->claim_attempted=true;
    if(!o->claim(o->context)||!snapshot(o,&base,3,true)||!restore_decode(o,observation)||
       !lifecycle_checked(o,s,0,0))goto failed;
    s->state=MSI_TRANSACTION;s->failed_stage=9;s->started=true;s->dma_retained=true;s->transactions=1;
    if(!o->pre_reset.run(o->context,&proof)||!proof.completed||proof.marker!=MSI_MARKER||
       !proof.halted||!proof.permissions||!proof.selectors||!proof.dma_live||proof.dma_live>128||
       !lifecycle_checked(o,s,0,proof.dma_live)||
       !clear_master(o)||!msi_config_matches(o,reference.config)||
       !msi_pre_flr_checks(o,reference.config,&s->pre_flr)||!snapshot(o,&base,3,true))goto failed;
    s->failed_stage=10;
    if(!clear_master(o)||!lifecycle_checked(o,s,0,proof.dma_live))goto failed;
    s->state=MSI_RESET_READY;
    if(!msi_reset_owned(o,s,reference.config,proof.dma_live)||!lifecycle_checked(o,s,1,proof.dma_live))goto failed;
    /* One lease spans proof, DMA cleanup, claim/log close and OS transfer.
     * No unguard/reacquire window and no implicit release on any error. */
    s->guard_attempted=s->guard_retained=true;
    if(!o->begin_guard(o->context,MSI_GUARD_FINAL,reference.config,&guard)||
       !guard.epoch||guard.purpose!=MSI_GUARD_FINAL||
       !snapshot(o,&base,3,true)||!msi_post_reset(o,reference.config)||
       !o->guard_valid(o->context,&guard)||!lifecycle_checked(o,s,1,proof.dma_live))goto failed;
    s->failed_stage=11;
    if(!o->prepare_dma_release(o->context,&guard))goto failed;
    s->failed_stage=12;
    if(!o->guard_valid(o->context,&guard)||!o->prepare_claim_close(o->context)||
       !snapshot(o,&base,3,true)||!o->guard_valid(o->context,&guard))goto failed;
    s->state=MSI_READY_TO_COMMIT;s->failed_stage=13;log_attempted=true;
    if(!o->close_log(o->context)){s->cleanup_failed=true;goto failed;}
    s->failed_stage=14;
    if(!snapshot(o,&base,3,true)||!msi_post_reset(o,reference.config)||
       !o->guard_valid(o->context,&guard)||!lifecycle_checked(o,s,1,proof.dma_live))goto failed;
    s->failed_stage=15;s->state=MSI_WINDOWS_INVOKED;s->windows_calls=1;
    o->commit_windows(o->context,&guard,&s->dma_retained);
    /* Returning does not prove Windows, NVIDIA bind or display recovery. */
failed:
    s->containment_verified=false;
    if(s->terminal_required||s->mutated||s->console_may_be_changed||s->guard_attempted||s->stock_cleanup_started) {
        s->state=MSI_STOPPED;
        s->terminal_required=true;
        if(s->mutated)(void)clear_master(o); /* No GPU access for console-only failure. */
    } else s->state=MSI_REFUSED;
    if(!log_attempted) {
        if(!o->close_log(o->context))s->cleanup_failed=true;
    }
    /* No external callback may follow this final containment proof. */
    if(s->mutated)s->containment_verified=clear_master(o);
}
