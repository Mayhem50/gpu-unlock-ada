/* SPDX-License-Identifier: GPL-2.0-only */
/* Former review counterexamples now assert rejection/containment, in the normal
 * CPU-only suite. Included after the mock implementation in host_tests.c. */
static unsigned DriftIndex,DriftMask,RejectedDecodeEnables;
static bool MutateCallerReference,RejectFinalContainment;
static bool drift_transaction(void *v,MsiProof *proof) {
    bool result=transaction(v,proof);Mock *m=v;
    m->config[DriftIndex]^=DriftMask;
    if(MutateCallerReference)m->observation.config[DriftIndex]^=DriftMask;
    return result;
}
static bool late_master_on_close(void *v) {
    bool result=close_log(v);
    CHECK(!S.containment_verified);
    ((Mock *)v)->config[1]|=4;
    return result;
}
static bool containment_read(void *v,unsigned offset,unsigned width,uint32_t *value) {
    Mock *m=v;
    if(m->log_closed&&RejectFinalContainment&&offset==4)return false;
    return read_config(v,offset,width,value);
}
static bool ignored_restore(void *v,unsigned offset,unsigned width,uint32_t value) {
    Mock *m=v;
    if(m->flr_written&&offset==4&&(value&3))RejectedDecodeEnables++;
    bool result=write_config(v,offset,width,value);
    if(offset==DriftIndex*4&&m->flr_written)m->config[DriftIndex]^=DriftMask;
    return result;
}
static bool final_control_drift(void *v) {
    bool result=close_log(v);((Mock *)v)->config[DriftIndex]^=DriftMask;return result;
}
static void execute_regression(MsiOps *o) {
    Scenarios++;
    if(!setjmp(Transfer))msi_run(o,&S,&M.observation,Prom,1048576,&SyntheticApproval,true);
}
static void capability_regressions(void) {
    setup(OK);MsiObservation valid=M.observation;
    CHECK(msi_capabilities(valid.config));
    /* Every bit of each pinned capability header, plus pointer/header/status.
     * This includes wrong IDs, loops, duplicate/missing caps, bad layout and
     * newly enabled MSI. No malformed pointer is used as an array index. */
    const unsigned headers[]={MSI_PM/4,MSI_MSI/4,MSI_PCIE/4,0xb4/4,13};
    for(unsigned i=0;i<sizeof(headers)/sizeof(headers[0]);i++)for(unsigned bit=0;bit<32;bit++) {
        M.observation=valid;M.observation.config[headers[i]]^=1u<<bit;
        CHECK(!msi_capabilities(M.observation.config));
    }
    M.observation=valid;M.observation.config[3]^=1u<<16;CHECK(!msi_capabilities(M.observation.config));
    M.observation=valid;M.observation.config[1]&=~(1u<<20);CHECK(!msi_capabilities(M.observation.config));
    M.observation=valid;M.observation.config[MSI_PCIE/4]^=1;
    CHECK(!msi_profile(&M.observation,Prom,1048576)); /* Original R3. */
    memcpy(M.config,M.observation.config,sizeof(M.config));MsiOps o=ops();
    execute_regression(&o);
    CHECK(S.state==MSI_REFUSED&&!M.stops&&!M.transactions&&!S.flrs&&!M.windows);
}
static void reference_regressions(void) {
    const unsigned indices[]={3,4,5,6,7,8,9,12,15,(MSI_PCIE+8)/4,
        (MSI_PCIE+0x28)/4,(MSI_PM+4)/4,MSI_MSI/4};
    for(unsigned i=0;i<sizeof(indices)/sizeof(indices[0]);i++) {
        setup(OK);DriftIndex=indices[i];DriftMask=DriftIndex==(MSI_PCIE+8)/4?0x20:1;
        MsiOps o=ops();o.transaction=drift_transaction;execute_regression(&o);
        CHECK(S.state==MSI_STOPPED&&S.terminal_required&&S.dma_retained&&S.containment_verified);
        CHECK(!M.windows&&!S.flrs&&!M.restored_bars&&!M.freed&&!M.mmio_reads);
    }
    /* The original R1 also remains rejected if the caller's reference mutates. */
    setup(OK);DriftIndex=(MSI_PCIE+8)/4;DriftMask=0x20;MutateCallerReference=true;
    MsiOps o=ops();o.transaction=drift_transaction;execute_regression(&o);MutateCallerReference=false;
    CHECK(!M.windows&&!S.flrs&&S.dma_retained&&S.terminal_required);
    /* A successful write return cannot hide a bad BAR/control restore. */
    const unsigned restores[]={4,5,6,7,8,9,12,(MSI_PCIE+8)/4,(MSI_PCIE+0x28)/4};
    for(unsigned i=0;i<sizeof(restores)/sizeof(restores[0]);i++) {
        setup(OK);DriftIndex=restores[i];DriftMask=DriftIndex==(MSI_PCIE+8)/4?0x20:0x10;
        RejectedDecodeEnables=0;o=ops();o.write_config=ignored_restore;execute_regression(&o);
        CHECK(!RejectedDecodeEnables&&!M.windows&&!M.freed&&S.dma_retained&&S.terminal_required);
        CHECK(S.flrs==1&&!(M.config[1]&7));
    }
    setup(OK);DriftIndex=(MSI_PCIE+8)/4;DriftMask=0x20;
    o=ops();o.close_log=final_control_drift;execute_regression(&o);
    CHECK(!M.windows&&S.terminal_required&&S.containment_verified);
}
static void cleanup_regressions(void) {
    for(unsigned mode=0;mode<3;mode++) {
        setup(TX_FAIL);MsiOps o=ops();o.close_log=late_master_on_close;o.read_config=containment_read;
        RejectFinalContainment=mode==1;
        if(mode==2)M.fault=CLOSE_LOG_FAIL; /* Successful transaction, failing final close. */
        execute_regression(&o);
        CHECK(S.state==MSI_STOPPED&&S.terminal_required&&!M.windows&&M.closes==1);
        CHECK(S.dma_retained&&!M.freed);
        if(mode<2)CHECK(!S.flrs);
        if(mode==1)CHECK(!S.containment_verified&&(M.config[1]&4));
        else CHECK(S.containment_verified&&!(M.config[1]&4));
        if(mode==2)CHECK(S.cleanup_failed);
    }
    RejectFinalContainment=false;
}
static void reentry_regressions(void) {
    /* Probe every mock operation, including staging, IO, cleanup and handoff. */
    ProbeReentry=true;Reentries=0;
    run(OK,true,&SyntheticApproval);
    CHECK(S.state==MSI_WINDOWS_INVOKED&&M.stages==1&&M.windows==1&&M.transactions==1);
    run(DECODE_LOST,true,&SyntheticApproval);
    CHECK(S.state==MSI_WINDOWS_INVOKED&&M.stages==1&&M.windows==1);
    run(TX_FAIL,true,&SyntheticApproval);
    CHECK(S.terminal_required&&S.containment_verified&&M.stages==1&&M.closes==1);
    run(STAGE_FAIL,true,&SyntheticApproval);CHECK(S.state==MSI_REFUSED&&M.stages==1&&M.closes==1);
    run(OK,false,&SyntheticApproval);CHECK(M.stages==1&&M.windows==1);
    CHECK(Reentries>100);ProbeReentry=false;
}
static void regression_tests(void) {
    capability_regressions();reference_regressions();cleanup_regressions();reentry_regressions();
    puts("PASS: R1-R4 rejection, cleanup and reentrancy regressions (CPU only).");
}
