/* SPDX-License-Identifier: GPL-2.0-only */
/* Failed/short timing services and independent CPU-clock evidence; RAM only. */
static unsigned TimingTarget,TimingMode,TimingQuietClocks,TimingPendingReads,TimingIdReads;
static unsigned TimingReadsAtWait,TimingWritesAtWait,TimingMmioAtWait,TimingClosesAtWait,TimingSnapshotsAtWait;
static bool TimingHit;
static uint64_t TimingBefore;
static bool timing_stall(void *v,unsigned usec) {
    Mock *m=v;
    if(usec!=TimingTarget||TimingHit)return stall(v,usec);
    TimingHit=true;
    TimingReadsAtWait=m->config_reads;TimingWritesAtWait=m->config_writes;
    TimingMmioAtWait=m->mmio_reads;TimingClosesAtWait=m->closes;TimingSnapshotsAtWait=m->snapshots;
    if(TimingMode==0||TimingMode==4)return TimingMode==4; /* no elapsed time */
    if(TimingMode==1||TimingMode==3){m->clock+=usec/2;return TimingMode==3;}
    if(TimingMode==2){m->clock+=usec;return false;} /* failure despite full elapsed time */
    if(TimingMode==5){m->clock+=TimingTarget==100000?200000:2000000;return true;}
    m->clock+=usec;return true; /* clock fault, not stall fault */
}
static bool timing_clock(void *v,uint64_t *out) {
    Mock *m=v;
    if(TimingTarget==100000&&S.quiet_uncertain) {
        TimingQuietClocks++;
        if(TimingQuietClocks==1)TimingBefore=m->clock;
        if((TimingMode==6&&TimingQuietClocks==1)||(TimingMode==7&&TimingQuietClocks==2))return false;
        if(TimingMode==8&&TimingQuietClocks==2){*out=TimingBefore-1;return true;}
        if(TimingMode==9&&TimingQuietClocks==2){*out=TimingBefore;return true;}
    }
    return clock_us(v,out);
}
static bool timing_read(void *v,unsigned offset,unsigned width,uint32_t *out) {
    if(offset==MSI_PCIE+10)TimingPendingReads++;
    if(offset==0&&M.flr_written)TimingIdReads++;
    return read_config(v,offset,width,out);
}
static void timing_setup(enum Fault f,unsigned target,unsigned mode) {
    setup(f);TimingTarget=target;TimingMode=mode;
    TimingHit=false;TimingQuietClocks=TimingPendingReads=TimingIdReads=0;
    TimingReadsAtWait=TimingWritesAtWait=TimingMmioAtWait=TimingClosesAtWait=TimingSnapshotsAtWait=0;
    M.clock=1000000; /* permit explicit backwards-clock test without underflow */
}
static MsiOps timing_ops(void) {
    MsiOps o=ops();o.stall=timing_stall;o.clock_us=timing_clock;o.read_config=timing_read;return o;
}
static bool failed_clock(void *v,uint64_t *out){(void)v;(void)out;return false;}
static bool frozen_clock(void *v,uint64_t *out){(void)v;*out=0;return true;}
static bool failed_readiness_clock(void *v,uint64_t *out) {
    if(M.flr_written&&!S.quiet_uncertain)return false;
    return clock_us(v,out);
}
static void timing_regressions(void) {
    /* Possibly effective, reported-failed, and no-effect FLR writes all require
     * measured quiet time. No PCI/log callback may follow an unproven interval. */
    const enum Fault writes[]={OK,FLR_WRITE_FAIL,FLR_FAIL_NO_EFFECT};
    for(unsigned w=0;w<3;w++)for(unsigned mode=0;mode<10;mode++) {
        timing_setup(writes[w],100000,mode);MsiOps o=timing_ops();execute_regression(&o);
        CHECK(TimingHit&&S.flrs==1&&M.reset_gate.spent&&S.quiet.requested_us==100000);
        bool unproven=mode!=2&&mode!=5;
        if(unproven) {
            CHECK(S.quiet_uncertain&&S.terminal_required&&S.dma_retained&&!S.containment_verified);
            CHECK(S.state==MSI_STOPPED&&!M.windows&&!M.freed&&!M.restored_bars);
            CHECK(M.config_reads==TimingReadsAtWait&&M.config_writes==TimingWritesAtWait);
            CHECK(M.mmio_reads==TimingMmioAtWait&&M.closes==TimingClosesAtWait&&M.snapshots==TimingSnapshotsAtWait);
            CHECK(!M.life.unmaps&&!M.life.frees&&!M.life.claim_closes);
        } else {
            CHECK(!S.quiet_uncertain&&S.quiet.clock_valid&&S.quiet.after-S.quiet.before>=100000);
            if(mode==5&&w==0)CHECK(M.windows==1&&!S.dma_retained&&M.life.frees==2);
            else CHECK(S.terminal_required&&S.dma_retained&&S.containment_verified&&!M.windows&&!M.freed);
        }
        unsigned reads=M.config_reads,writes_before=M.config_writes,closes=M.closes;
        msi_run(&o,&S,&M.observation,Prom,1048576,&SyntheticApproval,true);
        CHECK(M.config_reads==reads&&M.config_writes==writes_before&&M.closes==closes);
        if(unproven) {
            S.state=MSI_NEW;S.flrs=0;
            msi_run(&o,&S,&M.observation,Prom,1048576,&SyntheticApproval,true);
            S.state=MSI_RESET_READY;
            CHECK(!msi_reset_owned(&o,&S,M.observation.config,2));
            CHECK(M.config_reads==reads&&M.config_writes==writes_before&&M.closes==closes);
        }
    }
    /* Poll delays must report success AND actually advance the independent
     * clock. Oversleep beyond the whole budget must not issue another poll. */
    for(unsigned target=0;target<2;target++)for(unsigned mode=0;mode<6;mode++) {
        timing_setup(target?RESET_TIMEOUT:PENDING_STUCK,target?10000:1000,mode);
        MsiOps o=timing_ops();execute_regression(&o);
        CHECK(TimingHit&&S.terminal_required&&S.dma_retained&&!M.windows&&!M.freed);
        CHECK(S.flrs==(target?1u:0u)&&!S.quiet_uncertain&&S.containment_verified);
        if(mode==5) {
            if(target)CHECK(TimingIdReads==1);
            else CHECK(TimingPendingReads==1);
        }
    }
    /* Counter rollover is not silently interpreted as a valid quiet interval. */
    timing_setup(OK,100000,10);M.clock=UINT64_MAX-50000;
    MsiOps o=timing_ops();execute_regression(&o);
    CHECK(S.quiet_uncertain&&!S.quiet.clock_valid&&S.terminal_required&&!M.windows&&!M.closes);
    CHECK(M.config_reads==TimingReadsAtWait&&M.config_writes==TimingWritesAtWait);
    /* Missing timing capability refuses admission before staging/mutation. */
    setup(OK);o=ops();o.clock_us=NULL;execute_regression(&o);
    CHECK(S.state==MSI_REFUSED&&!M.stages&&!M.config_reads&&!M.config_writes);
    for(unsigned mode=0;mode<3;mode++) {
        setup(mode<2?PENDING_STUCK:RESET_TIMEOUT);o=ops();
        o.clock_us=mode==0?failed_clock:mode==1?frozen_clock:failed_readiness_clock;
        execute_regression(&o);
        CHECK(S.terminal_required&&S.dma_retained&&S.containment_verified&&!S.quiet_uncertain);
        CHECK(S.flrs==(mode==2?1u:0u)&&!M.windows&&!M.freed&&!M.restored_bars);
    }
    puts("PASS: failed/short/unknown timing and measured FLR silence; no callbacks during uncertain quiet time.");
}
