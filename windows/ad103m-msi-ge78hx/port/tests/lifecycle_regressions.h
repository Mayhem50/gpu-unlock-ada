/* SPDX-License-Identifier: GPL-2.0-only */
/* F3/F4 review probes converted to normal rejection regressions. */
static bool ResetInjected;
static unsigned ResetEncoding,ResetPhase,StockField;
static void reject_duplicate_reset(void) {
    static const unsigned offsets[]={MSI_PCIE+9,MSI_PCIE+8,MSI_PCIE+8,MSI_PCIE+9};
    static const unsigned widths[]={1,2,4,2};
    static const uint32_t values[]={0x80,0x8000,0x8000,0x80};
    unsigned writes=M.config_writes,effects=M.raw_reset_effects;
    uint64_t resets=M.life.resets;
    CHECK(!write_config(&M,offsets[ResetEncoding],widths[ResetEncoding],values[ResetEncoding]));
    CHECK(M.config_writes==writes&&M.raw_reset_effects==effects&&M.life.resets==resets);
    CHECK(M.reset_gate.spent&&M.reset_gate.violation&&M.life.owner_violation);
}
static bool duplicate_on_read(void *v,unsigned offset,unsigned width,uint32_t *value) {
    if(M.flr_written&&!ResetInjected&&offset==0) {
        ResetInjected=true;
        if(ResetPhase==4)M.life.resets=0; /* Repairing statistics must not rearm authorization. */
        reject_duplicate_reset();
    }
    return read_config(v,offset,width,value);
}
static bool duplicate_on_restore(void *v,unsigned offset,unsigned width,uint32_t value) {
    if(M.flr_written&&!ResetInjected&&offset==0x24){ResetInjected=true;reject_duplicate_reset();}
    return write_config(v,offset,width,value);
}
static void duplicate_during_quiet(void *v,unsigned usec) {
    if(M.reset_gate.spent&&!ResetInjected&&M.clock-M.flr_at<100000) {
        ResetInjected=true;reject_duplicate_reset(); /* Rejected without even a virtual PCI access. */
    }
    stall(v,usec);
}
static void raw_gate_regressions(void) {
    MsiResetGate gate={0};CHECK(!msi_reset_gate_take(NULL,true));
    CHECK(!msi_reset_gate_take(&gate,false)&&gate.violation);
    CHECK(!msi_reset_gate_take(&gate,true));
    gate=(MsiResetGate){0};CHECK(msi_reset_gate_take(&gate,true)&&gate.spent);
    CHECK(!msi_reset_gate_take(&gate,true)&&gate.violation);
    for(ResetEncoding=0;ResetEncoding<4;ResetEncoding++)for(ResetPhase=0;ResetPhase<6;ResetPhase++) {
        setup(ResetPhase==3?FLR_WRITE_FAIL:ResetPhase==5?FLR_FAIL_NO_EFFECT:OK);ResetInjected=false;MsiOps o=ops();
        if(ResetPhase==0||ResetPhase==4)o.read_config=duplicate_on_read;
        if(ResetPhase==1)o.write_config=duplicate_on_restore;
        if(ResetPhase==2||ResetPhase==3||ResetPhase==5)o.stall=duplicate_during_quiet;
        execute_regression(&o);
        CHECK(ResetInjected&&M.raw_reset_effects==(ResetPhase==5?0u:1u)&&M.raw_reset_rejections==1&&S.flrs==1);
        CHECK(S.state==MSI_STOPPED&&S.terminal_required&&S.dma_retained&&!M.freed&&!M.windows);
        CHECK(M.clock-M.flr_at>=100000&&!M.life.unmaps&&!M.life.frees&&!M.life.claim_closes);
        unsigned effects=M.raw_reset_effects;
        S.state=MSI_RESET;M.life.resets=0;reject_duplicate_reset();
        CHECK(M.raw_reset_effects==effects); /* Phase/counter repair is insufficient. */
    }
    for(ResetEncoding=0;ResetEncoding<4;ResetEncoding++) {
        setup(OK);MsiOps o=ops();RawResetHook=reject_duplicate_reset;execute_regression(&o);
        CHECK(!RawResetHook&&M.raw_reset_effects==1&&M.raw_reset_rejections==1);
        CHECK(S.terminal_required&&S.dma_retained&&!M.windows&&!M.freed);
    }
}
static void dirty_stock(Mock *m) {
    switch(StockField) {
        case 0:m->life.resets++;break;
        case 1:m->life.restores++;break;
        case 2:m->life.unmaps++;break;
        case 3:m->life.frees++;break;
        case 4:m->life.claim_closes++;break;
        case 5:m->life.live_dma++;break;
        case 6:m->life.owner_violation=true;break;
        case 7:m->lifecycle_unreadable=true;break;
        case 8:m->reset_gate.spent=true;break;
        case 9:m->reset_gate.violation=true;break;
        default:CHECK(false);
    }
}
static bool dirty_stock_log(void *v){bool result=close_log(v);dirty_stock(v);return result;}
static bool dirty_stock_guard(void *v,unsigned purpose,const uint32_t *ref,MsiGuard *g) {
    bool result=begin_guard(v,purpose,ref,g);dirty_stock(v);return result;
}
static bool dirty_failed_stock_guard(void *v,unsigned purpose,const uint32_t *ref,MsiGuard *g) {
    CHECK(begin_guard(v,purpose,ref,g));dirty_stock(v);return false;
}
static bool dirty_stock_valid(void *v,const MsiGuard *g) {
    bool result=guard_valid(v,g);dirty_stock(v);return result;
}
static void dirty_stock_commit(void *v,const MsiGuard *g,bool *retained) {
    dirty_stock(v);commit_windows(v,g,retained);
}
static void run_stock_case(MsiOps *o) {
    Scenarios++;
    if(!setjmp(Transfer))msi_run(o,&S,NULL,NULL,0,&SyntheticApproval,false);
}
static void stock_lifecycle_regressions(void) {
    for(StockField=0;StockField<10;StockField++)for(unsigned point=0;point<5;point++) {
        setup(OK);MsiOps o=ops();
        if(point==0)o.close_log=dirty_stock_log;
        if(point==1)o.begin_guard=dirty_stock_guard;
        if(point==2)o.guard_valid=dirty_stock_valid;
        if(point==3)o.commit_windows=dirty_stock_commit;
        if(point==4)o.begin_guard=dirty_failed_stock_guard;
        run_stock_case(&o);
        CHECK(S.state==MSI_STOPPED&&S.terminal_required&&S.dma_retained&&!M.windows);
        CHECK(!M.config_reads&&!M.config_writes&&!M.mmio_reads&&!M.raw_reset_effects);
        CHECK(!M.freed&&!M.stops&&!M.transactions&&M.closes==1);
        CHECK(!S.mutated&&!S.containment_verified);
        CHECK(point==0?(!M.guard_active&&!S.guard_attempted):(M.guard_active&&S.guard_retained));
        unsigned closes=M.closes,stages=M.stages;
        msi_run(&o,&S,NULL,NULL,0,&SyntheticApproval,false);
        CHECK(!M.windows&&M.closes==closes&&M.stages==stages); /* No retry. */
        if(point==3) {
            /* Even repairing the journal cannot replay a spent commit. */
            memset(&M.life,0,sizeof(M.life));M.lifecycle_unreadable=false;M.reset_gate=(MsiResetGate){0};
            MsiGuard g={M.guard_epoch,M.guard_purpose};S.state=MSI_WINDOWS_INVOKED;
            commit_windows(&M,&g,&S.dma_retained);CHECK(!M.windows&&!M.freed&&S.dma_retained);
        }
    }
    setup(CLOSE_LOG_FAIL);StockField=5;MsiOps o=ops();o.close_log=dirty_stock_log;run_stock_case(&o);
    CHECK(S.cleanup_failed&&S.dma_retained&&S.terminal_required&&!M.windows&&!M.guard_active);
    setup(CLOSE_LOG_FAIL);o=ops();run_stock_case(&o);
    CHECK(S.cleanup_failed&&S.terminal_required&&!S.dma_retained&&!M.windows&&!M.guard_active);
    CHECK(!M.config_reads&&!M.config_writes&&!M.mmio_reads);
    setup(OK);o=ops();run_stock_case(&o);
    CHECK(M.windows==1&&!S.dma_retained&&!M.config_reads&&!M.config_writes&&!M.mmio_reads);
    CHECK(!M.life.live_dma&&!M.life.resets&&!M.life.frees&&!M.life.unmaps&&!M.life.claim_closes);
}
static void lifecycle_regressions(void) {
    raw_gate_regressions();stock_lifecycle_regressions();
    puts("PASS: F3 single-use raw FLR and F4 clean stock lifecycle regressions.");
}
