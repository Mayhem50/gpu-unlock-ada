/* SPDX-License-Identifier: GPL-2.0-only */
/* F1/F2: former review probes now assert safe rejection in the normal suite. */
static unsigned GuardFault,FinalOffset;
static bool FreedWithMaster,ConsoleSucceeds;
static void late_change(Mock *m) {
    switch(GuardFault) {
        case 0:m->config[1]|=4;break;
        case 1:m->engine_running=true;break;
        case 2:m->guard_epoch++;break;
        case 3:m->guard_broken=true;break;
        case 4:m->graph.boundary[0]^=1;break;
        case 5:m->config[4]^=0x1000000;break;
        case 6:m->graph.exclusive=!m->graph.exclusive;break;
        default:CHECK(GuardFault<14);m->safety_regs[GuardFault-7]^=1;break;
    }
}
static bool master_after_reset_snapshot(void *v,MsiGraph *out) {
    Mock *m=v;bool result=snapshot(v,out);
    if(m->flr_written&&m->restored_bars==6&&!m->freed)m->config[1]|=4;
    return result;
}
static bool record_dma_release(void *v,const MsiGuard *g) {
    bool result=prepare_dma_release(v,g);
    if(((Mock *)v)->freed)FreedWithMaster=(((Mock *)v)->config[1]&4)!=0;
    return result;
}
static bool change_at_free(void *v,const MsiGuard *g) {
    late_change(v);return record_dma_release(v,g);
}
static void change_at_transfer(void *v,const MsiGuard *g,bool *retained) {
    late_change(v);commit_windows(v,g,retained);
}
static bool change_at_claim_preparation(void *v) {
    bool result=prepare_claim_close(v);late_change(v);return result;
}
static bool change_at_log_close(void *v) {
    bool result=close_log(v);late_change(v);return result;
}
static bool change_at_final_mmio(void *v,uint32_t offset,uint32_t *value) {
    bool result=read_mmio(v,offset,value);Mock *m=v;
    if(m->log_closed&&offset==FinalOffset)late_change(m);
    return result;
}
static bool partial_silence(void *v) {
    Mock *m=v;m->silenced=true;m->graph.console_alive=false;
    m->config_reads=m->config_writes=0;
    return ConsoleSucceeds;
}
static bool bad_begin(void *v,unsigned purpose,const uint32_t *reference,MsiGuard *g) {
    if(GuardFault==0)return false; /* No exclusion available. */
    CHECK(begin_guard(v,purpose,reference,g));
    if(GuardFault==1)return false; /* Partial acquisition. */
    if(GuardFault==2)g->epoch=0;
    if(GuardFault==3)g->purpose=99;
    return true;
}
static bool bad_valid(void *v,const MsiGuard *g) {
    (void)v;(void)g;return false;
}
static void require_retained_failure(void) {
    CHECK(S.state==MSI_STOPPED&&S.terminal_required&&S.guard_attempted&&S.guard_retained);
    CHECK(S.dma_retained&&!M.freed&&!M.windows&&!FreedWithMaster);
}
static void followup_regressions(void) {
    /* Original F1, path 1: a valid graph does not authorize freeing DMA. */
    setup(OK);FreedWithMaster=false;MsiOps o=ops();
    o.snapshot=master_after_reset_snapshot;o.prepare_dma_release=record_dma_release;execute_regression(&o);
    require_retained_failure();CHECK(M.guard_active);

    /* Refuse late BME, engine, epoch, ownership, BAR or boundary changes even
     * after the orchestrator's last guard_valid callback. Atomic action checks
     * are essential; simply adding an earlier readback would miss these. */
    for(GuardFault=0;GuardFault<14;GuardFault++) {
        setup(OK);o=ops();o.prepare_dma_release=change_at_free;execute_regression(&o);
        require_retained_failure();CHECK(M.guard_active);
        MsiGuard replay={M.guard_epoch,M.guard_purpose};
        CHECK(!prepare_dma_release(&M,&replay)&&!M.freed); /* Even if cleanup repaired BME. */
        setup(OK);o=ops();o.commit_windows=change_at_transfer;execute_regression(&o);
        require_retained_failure();CHECK(M.guard_active);
        replay=(MsiGuard){M.guard_epoch,M.guard_purpose};M.fault=RETURNED_WINDOWS;
        commit_windows(&M,&replay,&S.dma_retained);CHECK(!M.windows&&!M.freed);
        setup(OK);o=ops();o.prepare_claim_close=change_at_claim_preparation;execute_regression(&o);
        require_retained_failure();CHECK(M.guard_active);
        setup(OK);o=ops();o.close_log=change_at_log_close;execute_regression(&o);
        require_retained_failure();CHECK(M.guard_active);
    }
    const unsigned offsets[]={0,0x110100,0x1fa828,0x82381c,0x823820,0x823818,0x823804};
    /* Original F1, path 2, extended to each final MMIO callback and two faults. */
    for(GuardFault=0;GuardFault<2;GuardFault++)for(unsigned i=0;i<7;i++) {
        setup(OK);FinalOffset=offsets[i];o=ops();o.read_mmio=change_at_final_mmio;
        execute_regression(&o);require_retained_failure();CHECK(M.guard_active);
    }
    for(GuardFault=0;GuardFault<4;GuardFault++) {
        setup(OK);o=ops();o.begin_guard=bad_begin;execute_regression(&o);
        require_retained_failure();
        CHECK(M.guard_active==(GuardFault!=0));
    }
    setup(OK);o=ops();o.guard_valid=bad_valid;execute_regression(&o);require_retained_failure();
    /* Same refusal on the stock/skip path, without virtual GPU config access. */
    setup(OK);o=ops();o.begin_guard=bad_begin;GuardFault=1;Scenarios++;
    msi_run(&o,&S,NULL,NULL,0,&SyntheticApproval,false);
    CHECK(!M.windows&&S.terminal_required&&S.guard_retained&&M.guard_active);
    CHECK(!M.config_reads&&!M.config_writes&&!M.transactions);

    /* Original F2: partial console failure and a falsely successful silence
     * are both terminal, tracked separately from GPU mutation. No rollback is
     * claimed or attempted automatically; no GPU cleanup is triggered. */
    for(unsigned mode=0;mode<2;mode++) {
        setup(OK);ConsoleSucceeds=mode!=0;o=ops();o.silence_console=partial_silence;
        execute_regression(&o);
        CHECK(S.state==MSI_STOPPED&&S.console_may_be_changed&&S.terminal_required&&!S.mutated);
        CHECK(!M.graph.console_alive&&!M.stops&&!M.transactions&&!M.windows);
        CHECK(!M.config_reads&&!M.config_writes&&!M.mmio_reads&&M.closes==1);
    }
    /* Missing mandatory safety primitives refuse before console/GPU mutation. */
    setup(OK);o=ops();o.begin_guard=NULL;execute_regression(&o);
    CHECK(S.state==MSI_REFUSED&&!M.stages&&!M.stops&&!M.silenced);
    setup(OK);o=ops();o.guard_valid=NULL;execute_regression(&o);
    CHECK(S.state==MSI_REFUSED&&!M.stages&&!M.stops&&!M.silenced);
    puts("PASS: F1 guarded actions and F2 partial-console regressions (CPU only).");
}
