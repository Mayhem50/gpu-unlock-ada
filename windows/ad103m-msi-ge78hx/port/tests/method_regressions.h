/* SPDX-License-Identifier: GPL-2.0-only */
#include "../src/reset_internal.h" /* deliberate negative tests of private owner gate */
static uint32_t ProbeOffset,ProbeValue;
static unsigned ProbeMode;
static bool method_read(void *v,uint32_t offset,uint32_t *value) {
    bool ok=read_mmio(v,offset,value);Mock *m=v;
    if(!m->flr_written&&offset==ProbeOffset) {
        *value=ProbeValue;
        if(ProbeMode==1)return false;
        if(ProbeMode==2)m->config[4]^=0x1000000;
    }
    return ok;
}
static bool late_pre_reset_snapshot(void *v,MsiGraph *out) {
    bool result=snapshot(v,out);Mock *m=v;
    if(m->transactions&&!m->flr_written) {
        if(ProbeMode==0)m->fbpa=0xbadf5620;
        else m->safety_regs[5]=1;
    }
    return result;
}
static void no_reset_or_cleanup(void) {
    CHECK(S.state==MSI_STOPPED&&S.terminal_required&&S.dma_retained);
    CHECK(!S.flrs&&!M.flr_written&&!M.freed&&!M.windows);
    CHECK(!M.life.unmaps&&!M.life.frees&&!M.life.claim_closes);
}
static void method_read_case(uint32_t offset,uint32_t value,unsigned mode,bool succeeds) {
    setup(OK);ProbeOffset=offset;ProbeValue=value;ProbeMode=mode;
    MsiOps o=ops();o.read_mmio=method_read;execute_regression(&o);
    if(succeeds) {
        CHECK(M.windows==1&&M.life.resets==1&&M.life.restores==1);
        CHECK(M.life.unmaps==1&&M.life.frees==1&&M.life.claim_closes==1&&!M.life.live_dma);
        CHECK(S.pre_flr.attempted_mask==15&&S.pre_flr.transport_mask==15&&S.pre_flr.valid_mask==15);
    } else no_reset_or_cleanup();
}
static bool ownership_transaction(void *v,MsiProof *proof) {
    bool result=transaction(v,proof);Mock *m=v;MsiOps o=ops();
    switch(ProbeMode) {
        case 0: { /* Calling the private reset from transaction is denied before IO. */
            unsigned reads=m->config_reads,writes=m->config_writes;
            CHECK(!msi_reset_owned(&o,&S,m->observation.config,2));
            CHECK(m->config_reads==reads&&m->config_writes==writes&&!m->life.resets);break;
        }
        case 1:CHECK(!write_config(v,MSI_PCIE+8,2,0x8000));break;
        case 2: {MsiGuard fake={1,MSI_GUARD_FINAL};commit_windows(v,&fake,&S.dma_retained);break;}
        case 3:m->life.resets++;break; /* Already-reset/full-core result. */
        case 4:m->life.restores++;break;
        case 5:m->life.unmaps++;break;
        case 6:m->life.frees++;break;
        case 7:m->life.claim_closes++;break;
        case 8:m->life.live_dma--;break;
        default:CHECK(false);
    }
    return result;
}
static bool unavailable_lifecycle(void *v,MsiLifecycle *out) {
    (void)v;(void)out;return false;
}
static bool lost_lifecycle_after_start(void *v,MsiLifecycle *out) {
    if(((Mock *)v)->transactions)return false;
    return lifecycle(v,out);
}
static bool hidden_cleanup_preparation(void *v) {
    bool result=prepare_claim_close(v);((Mock *)v)->life.frees++;return result;
}
static unsigned RecursiveResetRejections;
static bool recursive_reset_read(void *v,uint32_t offset,uint32_t *value) {
    if(S.state==MSI_RESET_VERIFYING) {
        MsiOps nested=ops();unsigned reads=M.config_reads,writes=M.config_writes;
        CHECK(!msi_reset_owned(&nested,&S,M.observation.config,2));
        CHECK(M.config_reads==reads&&M.config_writes==writes);RecursiveResetRejections++;
    }
    return read_mmio(v,offset,value);
}
static void method_regressions(void) {
    /* FBPA can remain stock, be all ones or another accessible value. */
    const uint32_t allowed[]={0xffffff0f,0xffffffff,0,0x12345678};
    for(unsigned i=0;i<4;i++)method_read_case(0x9a0148,allowed[i],0,true);
    method_read_case(0x9a0148,0xbadf5620,0,false);
    CHECK(S.pre_flr.attempted_mask==15&&S.pre_flr.transport_mask==15&&S.pre_flr.valid_mask==13);
    method_read_case(0x9a0148,0xffffffff,1,false);
    CHECK(S.pre_flr.attempted_mask==15&&S.pre_flr.transport_mask==13);
    const uint32_t required[]={0x1fa7cc,0x1fa7c4,0x823804};
    for(unsigned i=0;i<3;i++) {
        method_read_case(required[i],0,0,false);
        CHECK(S.pre_flr.attempted_mask==15); /* Later samples preserved. */
        method_read_case(required[i],0xbadf0000,0,false);
        method_read_case(required[i],0xffffffff,1,false);
    }
    /* Every effective-rate bit blocks FLR. High unused bits do not. */
    for(unsigned bit=0;bit<27;bit++)method_read_case(0x823818,1u<<bit,0,false);
    method_read_case(0x823818,0xffffffff,0,false);
    method_read_case(0x823818,0xbadf5620,0,false);
    method_read_case(0x823818,0,1,false);
    method_read_case(0x823818,0x08000000,0,true);
    method_read_case(0,0xffffffff,0,false);
    CHECK(!S.pre_flr.attempted_mask); /* Independent chip check, even with all-one PLMs. */
    method_read_case(0x823804,0xffffffff,2,false); /* Identity changes during samples. */
    for(ProbeMode=0;ProbeMode<2;ProbeMode++) {
        setup(OK);MsiOps o=ops();o.snapshot=late_pre_reset_snapshot;execute_regression(&o);
        no_reset_or_cleanup(); /* Repeated proof immediately before FLR catches it. */
    }
    /* Typed boundary refuses complete/self-resetting implementations before staging. */
    setup(OK);MsiOps o=ops();o.pre_reset.boundary=MSI_FULL_TRANSACTION;execute_regression(&o);
    CHECK(S.state==MSI_REFUSED&&!M.stages&&!M.transactions&&!M.config_writes);
    setup(OK);o=ops();o.pre_reset.boundary=0;execute_regression(&o);
    CHECK(S.state==MSI_REFUSED&&!M.stages&&!M.transactions);
    for(ProbeMode=0;ProbeMode<9;ProbeMode++) {
        setup(OK);o=ops();o.pre_reset.run=ownership_transaction;execute_regression(&o);
        if(!ProbeMode) {
            CHECK(M.windows==1&&S.flrs==1&&M.life.resets==1&&M.life.restores==1);
            CHECK(M.life.unmaps==1&&M.life.frees==1&&M.life.claim_closes==1&&!M.life.live_dma);
            unsigned writes=M.config_writes;
            CHECK(!msi_reset_owned(&o,&S,M.observation.config,2)&&M.config_writes==writes);
        } else {
            CHECK(!M.windows&&!M.freed&&!S.flrs&&S.dma_retained&&S.terminal_required);
            if(ProbeMode<3)CHECK(M.life.owner_violation&&!M.life.resets);
        }
    }
    setup(OK);o=ops();o.lifecycle=unavailable_lifecycle;execute_regression(&o);
    CHECK(S.state==MSI_STOPPED&&S.terminal_required&&S.dma_retained&&!M.stages&&!M.transactions);
    setup(OK);o=ops();o.lifecycle=lost_lifecycle_after_start;execute_regression(&o);
    no_reset_or_cleanup();CHECK(S.containment_verified&&!(M.config[1]&4));
    setup(OK);M.life.live_dma=1;o=ops();execute_regression(&o);
    CHECK(S.state==MSI_STOPPED&&S.terminal_required&&S.dma_retained&&!M.stages&&!M.transactions&&!M.freed);
    setup(OK);o=ops();o.prepare_claim_close=hidden_cleanup_preparation;execute_regression(&o);
    CHECK(S.terminal_required&&S.dma_retained&&!M.freed&&!M.windows&&M.life.frees==1);
    setup(OK);o=ops();RecursiveResetRejections=0;o.read_mmio=recursive_reset_read;execute_regression(&o);
    CHECK(M.windows==1&&M.life.resets==1&&M.life.frees==1&&RecursiveResetRejections>0);
    puts("PASS: FBPA/readout pre-FLR and single reset/cleanup ownership regressions.");
}
